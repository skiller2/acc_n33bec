#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "card_store.h"
#include <stdbool.h>
#include <errno.h>

#define MAX_OPEN_STREAM_FILES SHARD_COUNT
#define SHARD_COUNT 32
#define SHARD_BUF_SIZE 48

static const char *TAG = "card_store";
static const char *SHARD_DIR = "/fs/cards";
static uint16_t stream_open_count = 0;
static FILE *stream_files[SHARD_COUNT] = {0};

typedef struct
{
    uint64_t cards[SHARD_BUF_SIZE];
    uint16_t count;
} shard_buffer_t;

static shard_buffer_t shard_buffers[SHARD_COUNT];

static int card_cmp(const void *a, const void *b)
{
    uint64_t aa = *(const uint64_t *)a;
    uint64_t bb = *(const uint64_t *)b;

    if (aa < bb)
        return -1;

    if (aa > bb)
        return 1;

    return 0;
}

static inline uint16_t card_shard(uint64_t id)
{
    /*
    id ^= id >> 33;
    id *= 0xff51afd7ed558ccdULL;
    id ^= id >> 33;

    return (uint16_t)(id & SHARD_MASK);
    */
    // return (uint16_t)(id & (SHARD_COUNT - 1));
    return (uint16_t)((id ^ (id >> 32)) & (SHARD_COUNT - 1));
}

static void stream_close_all_files(void)
{
    for (int i = 0; i < SHARD_COUNT; i++)
    {
        if (stream_files[i])
        {
            fflush(stream_files[i]);

            fclose(stream_files[i]);

            stream_files[i] = NULL;
        }
    }

    stream_open_count = 0;
}

static void shard_path(uint16_t shard, char *path, size_t path_size)
{
    snprintf(path, path_size, "/fs/cards/sh%04u.dat", shard);
}

static bool file_binary_search(const char *path, uint64_t id)
{
    FILE *f = fopen(path, "rb");

    if (!f)
        return false;

    fseek(f, 0, SEEK_END);

    size_t count = ftell(f) / sizeof(uint64_t);

    size_t low = 0;
    size_t high = count;

    while (low < high)
    {
        size_t mid = low + ((high - low) >> 1);

        uint64_t value;

        fseek(f, mid * sizeof(uint64_t), SEEK_SET);

        if (fread(&value, sizeof(value), 1, f) != 1)
        {
            fclose(f);
            return false;
        }

        if (value < id)
            low = mid + 1;
        else
            high = mid;
    }

    if (low >= count)
    {
        fclose(f);
        return false;
    }

    uint64_t value;

    fseek(f, low * sizeof(uint64_t), SEEK_SET);

    fread(&value, sizeof(value), 1, f);

    fclose(f);

    return value == id;
}

void card_mem_stream_init(void)
{
    stream_close_all_files();

    memset(shard_buffers, 0, sizeof(shard_buffers));

    DIR *dir = opendir(SHARD_DIR);

    if (!dir)
        return;

    struct dirent *e;

    while ((e = readdir(dir)) != NULL)
    {
        if (strncmp(e->d_name, "sh", 2) != 0)
            continue;

        char path[270];

        snprintf(path,
                 sizeof(path),
                 "%s/%s",
                 SHARD_DIR,
                 e->d_name);

        unlink(path);
    }

    closedir(dir);
}

static void card_mem_sort_shard(uint16_t shard)
{
    char path[64];

    shard_path(shard, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f)
        return;

    fseek(f, 0, SEEK_END);

    size_t count = ftell(f) / sizeof(uint64_t);

    fseek(f, 0, SEEK_SET);

    if (count == 0)
    {
        fclose(f);
        return;
    }

    uint64_t *cards = malloc(count * sizeof(uint64_t));

    if (!cards)
    {
        fclose(f);
        return;
    }

    if (fread(cards, sizeof(uint64_t), count, f) != count)
    {
        free(cards);
        fclose(f);
        return;
    }

    fclose(f);

    qsort(cards, count, sizeof(uint64_t), card_cmp);

    size_t unique_count = 0;

    for (size_t i = 0; i < count; i++)
    {
        if (unique_count == 0 ||
            cards[i] != cards[unique_count - 1])
        {
            cards[unique_count++] = cards[i];
        }
    }

    f = fopen(path, "wb");

    if (f)
    {
        fwrite(cards,
               sizeof(uint64_t),
               unique_count,
               f);

        fclose(f);
    }

    free(cards);
}

bool card_mem_add(uint64_t id)
{
    uint16_t shard = card_shard(id);

    char path[64];
    shard_path(shard, path, sizeof(path));

    if (file_binary_search(path, id))
    {
        return false; // already exists
    }

    FILE *f = fopen(path, "ab");
    if (!f)
    {
        return false;
    }

    bool ok = fwrite(&id, sizeof(id), 1, f) == 1;

    fclose(f);

    if (!ok)
    {
        return false;
    }

    card_mem_sort_shard(shard);

    return true;
}

static FILE *stream_get_file(uint16_t shard)
{
    if (stream_files[shard])
        return stream_files[shard];

    if (stream_open_count >= MAX_OPEN_STREAM_FILES)
    {
        for (int i = 0; i < SHARD_COUNT; i++)
        {
            if (stream_files[i])
            {
                fflush(stream_files[i]);

                fclose(stream_files[i]);

                stream_files[i] = NULL;

                stream_open_count--;

                break;
            }
        }
    }

    char path[64];
    snprintf(path, sizeof(path), "/fs/cards/sh%04u.dat", shard);

    stream_files[shard] = fopen(path, "ab");
    if (stream_files[shard])
    {

        static char io_bufs[SHARD_COUNT][128];

        setvbuf(stream_files[shard],
                io_bufs[shard],
                _IOFBF,
                sizeof(io_bufs[shard]));

        stream_open_count++;
    }
    return stream_files[shard];
}

static bool flush_shard(uint16_t shard)
{
    shard_buffer_t *sb = &shard_buffers[shard];

    if (sb->count == 0)
        return true;

    FILE *f = stream_get_file(shard);

    if (!f)
    {
        ESP_LOGE(TAG, "cannot open shard %u", shard);
        return false;
    }

    size_t wr = fwrite(
        sb->cards,
        sizeof(uint64_t),
        sb->count,
        f);

    if (wr != sb->count)
    {
        ESP_LOGE(TAG,
                 "fwrite failed shard=%u errno=%d ferror=%d",
                 shard,
                 errno,
                 ferror(f));

        clearerr(f);
        return false;
    }

    sb->count = 0;

    return true;
}

bool card_mem_stream_add(uint64_t id)
{
    uint16_t shard = card_shard(id);
    shard_buffer_t *sb = &shard_buffers[shard];

    if (sb->count >= SHARD_BUF_SIZE)
    {
        if (!flush_shard(shard))
            return false;
    }

    sb->cards[sb->count++] = id;

    return true;
}

bool card_mem_stream_flush(void)
{
    for (uint16_t shard = 0;
         shard < SHARD_COUNT;
         shard++)
    {
        if (!flush_shard(shard))
        {
            ESP_LOGW(TAG,
                     "flush failed shard=%u",
                     shard);
        }
    }

    stream_close_all_files();

    return true;
}

static bool card_exists(uint64_t id)
{
    uint16_t shard = card_shard(id);

    char path[64];

    shard_path(
        shard,
        path,
        sizeof(path));

    return file_binary_search(
        path,
        id);
}

int card_mem_exists(uint64_t id)
{
    return card_exists(id) ? 1 : 0;
}

void card_mem_del(uint64_t id)
{
    uint16_t shard = card_shard(id);

    char path[64];
    shard_path(shard, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f)
        return;

    fseek(f, 0, SEEK_END);
    size_t count = ftell(f) / sizeof(uint64_t);
    if (count == 0)
    {
        fclose(f);
        return;
    }

    uint64_t *cards = malloc(count * sizeof(uint64_t));
    if (!cards)
    {
        fclose(f);
        return;
    }

    fseek(f, 0, SEEK_SET);
    if (fread(cards, sizeof(uint64_t), count, f) != count)
    {
        free(cards);
        fclose(f);
        return;
    }
    fclose(f);

    size_t new_count = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (cards[i] != id)
        {
            cards[new_count++] = cards[i];
        }
    }

    if (new_count == count)
    {
        free(cards);
        return;
    }

    if (new_count == 0)
    {
        free(cards);
        remove(path);
        return;
    }

    f = fopen(path, "wb");
    if (!f)
    {
        free(cards);
        return;
    }

    fwrite(cards, sizeof(uint64_t), new_count, f);
    fclose(f);
    free(cards);
}

void card_mem_batch_add(const uint64_t *ids, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        uint16_t shard = card_shard(ids[i]);

        char path[64];
        shard_path(shard, path, sizeof(path));

        FILE *f = fopen(path, "ab");
        if (!f)
            continue;

        fwrite(&ids[i], sizeof(uint64_t), 1, f);
        fclose(f);
    }
}

void card_mem_sort(void)
{
    for (uint16_t shard = 0; shard < SHARD_COUNT; shard++)
    {
        char path[64];

        shard_path(shard, path, sizeof(path));

        FILE *f = fopen(path, "rb");

        if (!f)
            continue;

        fseek(f, 0, SEEK_END);

        size_t count = ftell(f) / sizeof(uint64_t);

        fseek(f, 0, SEEK_SET);

        if (count == 0)
        {
            fclose(f);
            continue;
        }

        uint64_t *cards = malloc(count * sizeof(uint64_t));

        if (!cards)
        {
            fclose(f);
            continue;
        }

        if (fread(cards, sizeof(uint64_t), count, f) != count)
        {
            free(cards);
            fclose(f);
            continue;
        }

        fclose(f);

        qsort(cards, count, sizeof(uint64_t), card_cmp);

        size_t unique_count = 0;

        for (size_t i = 0; i < count; i++)
        {
            if (unique_count == 0 ||
                cards[i] != cards[unique_count - 1])
            {
                cards[unique_count++] = cards[i];
            }
        }

        if (unique_count == 0)
        {
            free(cards);
            remove(path);
            continue;
        }

        f = fopen(path, "wb");

        if (!f)
        {
            free(cards);
            continue;
        }

        fwrite(cards, sizeof(uint64_t), unique_count, f);

        fclose(f);

        free(cards);
    }
}

void card_mem_sync(void)
{
    card_mem_sort();
}

void card_store_init(void)
{
    struct stat st;

    if (stat("/fs/cards", &st) != 0)
    {
        mkdir("/fs/cards", 0777);
    }

    ESP_LOGI(TAG,
             "card store initialized");
}

esp_err_t http_send_cards(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    esp_err_t err = httpd_resp_send_chunk(req, "[", 1);
    if (err != ESP_OK)
        return err;

    DIR *dir = opendir(SHARD_DIR);
    if (!dir)
    {
        httpd_resp_send_chunk(req, "]", 1);
        return httpd_resp_send_chunk(req, NULL, 0);
    }

    bool first = true;
    struct dirent *entry;

    uint64_t cards[32];

    // char outbuf[2048];
    char *outbuf = malloc(4096);
    if (!outbuf)
    {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    size_t outlen = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strncmp(entry->d_name, "sh", 2) != 0)
            continue;

        if (strstr(entry->d_name, ".tmp"))
            continue;

        char path[PATH_MAX];

        snprintf(
            path,
            sizeof(path),
            "%s/%s",
            SHARD_DIR,
            entry->d_name);

        FILE *f = fopen(path, "rb");

        if (!f)
            continue;

        while (1)
        {
            size_t rd = fread(
                cards,
                sizeof(uint64_t),
                sizeof(cards) / sizeof(cards[0]),
                f);

            if (rd == 0)
                break;

            for (size_t i = 0; i < rd; i++)
            {
                char json[48];

                int len = snprintf(
                    json,
                    sizeof(json),
                    "%s{\"card\":%llu}",
                    first ? "" : ",",
                    (unsigned long long)cards[i]);

                if (len <= 0)
                    continue;

                if (outlen + len >= 2048)
                {
                    err = httpd_resp_send_chunk(
                        req,
                        outbuf,
                        outlen);

                    if (err != ESP_OK)
                    {
                        fclose(f);
                        closedir(dir);
                        httpd_resp_send_chunk(req, NULL, 0);
                        free(outbuf);
                        return err;
                    }

                    outlen = 0;
                }

                memcpy(outbuf + outlen, json, len);
                outlen += len;

                first = false;
            }
        }

        fclose(f);
    }

    closedir(dir);

    if (outlen)
    {
        err = httpd_resp_send_chunk(
            req,
            outbuf,
            outlen);

        if (err != ESP_OK)
        {
            httpd_resp_send_chunk(req, NULL, 0);
            free(outbuf);
            return err;
        }
    }
    free(outbuf);

    err = httpd_resp_send_chunk(req, "]", 1);

    if (err == ESP_OK)
        err = httpd_resp_send_chunk(req, NULL, 0);

    return err;
}