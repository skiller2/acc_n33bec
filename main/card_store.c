#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX_CARDS 10000
//EXT_RAM_BSS_ATTR uint64_t cardsx[MAX_CARDS];
//uint64_t *cards = cardsx;
static int count = 0;
static uint64_t *cards = NULL;
static const char *TAG = "card_store";

static void card_sync_task(void *pvParameters)
{
    uint64_t *local_cards = (uint64_t *)pvParameters;
    int local_count = count;

    FILE *f = fopen("/fs/cards.dat", "wb");
    if (f)
    {
        for (int i = 0; i < local_count; i++)
            fwrite(&local_cards[i], sizeof(uint64_t), 1, f);
        fclose(f);
    }

    free(local_cards);
    vTaskDelete(NULL);
}

void card_mem_sync(void)
{
    if (!cards || count <= 0)
        return;

    uint64_t *copy = malloc(count * sizeof(uint64_t));
    if (!copy)
        return;

    memcpy(copy, cards, count * sizeof(uint64_t));

    if (xTaskCreate(card_sync_task, "card_sync", 4096, copy, 5, NULL) != pdTRUE)
        free(copy);
}

static int card_uint64_cmp(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

void card_mem_sort(void)
{
    if (count > 1)
        qsort(cards, count, sizeof(uint64_t), card_uint64_cmp);
}


void card_store_init()
{
    int64_t t_start = esp_timer_get_time();
    cards = calloc(MAX_CARDS, sizeof(uint64_t));

    //cards = heap_caps_malloc(MAX * sizeof(uint64_t), MALLOC_CAP_SPIRAM);
    if (!cards)
        return;

    FILE *f = fopen("/fs/cards.dat", "rb");
    if (!f)
        return;
    while (count < MAX_CARDS && fread(&cards[count], 8, 1, f))
        count++;
    fclose(f);

    card_mem_sort();
    int64_t dt_us = esp_timer_get_time() - t_start;

    ESP_LOGI(TAG, "Card list updated: %d cards added, time=%lldus", count, (long long)dt_us);


}

void card_truncate(void)
{
    count = 0;

    FILE *f = fopen("/fs/cards.dat", "wb");
    if (f)
        fclose(f);
}

int card_exists(uint64_t id)
{
    for (int i = 0; i < count; i++)
        if (cards[i] == id)
            return 1;
    return 0;
}

void card_add(uint64_t id)
{
    if (count >= MAX_CARDS)
    {
        ESP_LOGW("card_store", "card store full (%d/%d), cannot add card %llu", count, MAX_CARDS, id);
        return;
    }

    if (!card_exists(id))
    {
        cards[count++] = id;
        FILE *f = fopen("/fs/cards.dat", "ab");
        fwrite(&id, 8, 1, f);
        fclose(f);
    }
}

void card_del(uint64_t id)
{
    FILE *f = fopen("/fs/cards.dat", "wb");

    size_t new_count = 0;
    for (int i = 0; i < count; i++)
    {
        if (cards[i] != id)
        {
            fwrite(&cards[i], sizeof(uint64_t), 1, f);
            cards[new_count++] = cards[i];
        }
    }
    count = new_count;
    fclose(f);
}






int card_mem_exists(uint64_t id)
{
    if (count == 0)
        return 0;
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (cards[mid] == id)
            return 1;
        if (cards[mid] < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

void card_mem_add(uint64_t id)
{
    if (count >= MAX_CARDS)
    {
        ESP_LOGW("card_store", "card store full (%d/%d), cannot mem-add card %llu", count, MAX_CARDS, id);
        return;
    }

    if (card_mem_exists(id))
        return;

    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (cards[mid] < id)
            lo = mid + 1;
        else
            hi = mid;
    }

    memmove(&cards[lo + 1], &cards[lo], (count - lo) * sizeof(uint64_t));
    cards[lo] = id;
    count++;
}

void card_mem_del(uint64_t id)
{
    if (count == 0)
        return;

    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (cards[mid] == id) {
            memmove(&cards[mid], &cards[mid + 1], (count - mid - 1) * sizeof(uint64_t));
            count--;
            return;
        }
        if (cards[mid] < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
}

void card_mem_batch_add(const uint64_t *ids, size_t n)
{
    if (!ids || n == 0)
        return;

    size_t space = MAX_CARDS - count;
    if (space == 0)
    {
        ESP_LOGW("card_store", "card store full, cannot batch add %zu cards", n);
        return;
    }

    size_t to_add = n < space ? n : space;
    memcpy(&cards[count], ids, to_add * sizeof(uint64_t));
    count += to_add;
    card_mem_sort();
}


esp_err_t http_send_cards(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    esp_err_t err = httpd_resp_send_chunk(req, "[", 1);
    if (err != ESP_OK)
    {
        return httpd_resp_send_chunk(req, NULL, 0);
    }

    char temp[128];
    for (int i = 0; i < count; i++)
    {
        int len = snprintf(temp, sizeof(temp),
                           "%s{\"card\":%llu}",
                           i ? "," : "",
                           (unsigned long long)cards[i]);

        if (len < 0 || len >= (int)sizeof(temp))
        {
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }

        err = httpd_resp_send_chunk(req, temp, len);
        if (err != ESP_OK)
        {
            httpd_resp_send_chunk(req, NULL, 0);
            return err;
        }
    }

    err = httpd_resp_send_chunk(req, "]", 1);
    if (err != ESP_OK)
    {
        return httpd_resp_send_chunk(req, NULL, 0);
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}
