#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"

#define MAX 10000
static uint64_t *cards = NULL;
static int count = 0;

void card_store_init()
{
    cards = calloc(MAX, sizeof(uint64_t));

    //cards = heap_caps_malloc(MAX * sizeof(uint64_t), MALLOC_CAP_SPIRAM);
    if (!cards)
        return;

    FILE *f = fopen("/fs/cards.dat", "rb");
    if (!f)
        return;
    while (count < MAX && fread(&cards[count], 8, 1, f))
        count++;
    fclose(f);
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
    if (count >= MAX)
    {
        ESP_LOGW("card_store", "card store full (%d/%d), cannot add card %llu", count, MAX, id);
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

static const char *TAG = "card_store";

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
