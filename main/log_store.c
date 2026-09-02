
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include <sys/time.h>
#include <esp_log.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_http_server.h"

#define MAX_LOGS 1000

static const char *TAG = "logs";
static SemaphoreHandle_t s_log_mutex = NULL;

#define LOG_MAGIC 0x4C4F4731UL // "LOG1"
#define LOG_VERSION 1

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t write_index;
    uint32_t count;
} log_header_t;

typedef struct
{
    uint8_t event_id;
    uint8_t port_id;
    uint16_t reserved;
    uint64_t ts;
    uint64_t value;
} log_t;

void log_store_init(void)
{
    if (!s_log_mutex)
    {
        s_log_mutex = xSemaphoreCreateMutex();

        if (!s_log_mutex)
        {
            ESP_LOGE(TAG, "Error creating mutex");
            return;
        }
    }

    FILE *f = fopen("/fs/logs.dat", "rb");

    bool create_file = true;

    if (f)
    {
        log_header_t hdr;

        if (fread(&hdr, sizeof(hdr), 1, f) == 1)
        {
            if (hdr.magic == LOG_MAGIC &&
                hdr.version == LOG_VERSION &&
                hdr.write_index < MAX_LOGS &&
                hdr.count <= MAX_LOGS)
            {
                create_file = false;
            }
            else
            {
                ESP_LOGW(TAG,
                         "Invalid log header. Recreating file");
            }
        }

        fclose(f);
    }

    if (create_file)
    {
        f = fopen("/fs/logs.dat", "wb");

        if (!f)
        {
            ESP_LOGE(TAG, "Cannot create logs.dat");
            return;
        }

        log_header_t hdr = {
            .magic = LOG_MAGIC,
            .version = LOG_VERSION,
            .reserved = 0,
            .write_index = 0,
            .count = 0};

        fwrite(&hdr, sizeof(hdr), 1, f);
        fflush(f);

        fclose(f);

        ESP_LOGI(TAG, "New log file created");
    }
}

uint64_t getTimeStamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t epoch_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
    return epoch_us;
}

void log_add(uint8_t event_id,
             uint8_t port_id,
             uint64_t value,
             int64_t ts)
{
    if (!s_log_mutex)
        return;

    if (ts == 0)
        ts = getTimeStamp();

    if (xSemaphoreTake(s_log_mutex,
                       pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Timeout waiting log mutex");
        return;
    }

    FILE *f = fopen("/fs/logs.dat", "r+b");

    if (!f)
    {
        ESP_LOGE(TAG, "Cannot open logs.dat");
        xSemaphoreGive(s_log_mutex);
        return;
    }

    log_header_t hdr;

    if (fread(&hdr,
              sizeof(hdr),
              1,
              f) != 1)
    {
        ESP_LOGE(TAG, "Cannot read header");
        fclose(f);
        xSemaphoreGive(s_log_mutex);
        return;
    }

    if (hdr.magic != LOG_MAGIC ||
        hdr.version != LOG_VERSION ||
        hdr.write_index >= MAX_LOGS ||
        hdr.count > MAX_LOGS)
    {
        ESP_LOGE(TAG,
                 "Invalid log header "
                 "(magic=%08lX ver=%u idx=%lu count=%lu)",
                 (unsigned long)hdr.magic,
                 (unsigned)hdr.version,
                 (unsigned long)hdr.write_index,
                 (unsigned long)hdr.count);

        fclose(f);
        remove("/fs/logs.dat");
        xSemaphoreGive(s_log_mutex);

        log_store_init();

        return;
    }

    log_t entry = {
        .event_id = event_id,
        .port_id = port_id,
        .ts = (uint64_t)ts,
        .value = value};

    long offset =
        sizeof(log_header_t) +
        ((long)hdr.write_index * sizeof(log_t));

    if (fseek(f, offset, SEEK_SET) != 0)
    {
        ESP_LOGE(TAG, "fseek failed");
        fclose(f);
        xSemaphoreGive(s_log_mutex);
        return;
    }

    if (fwrite(&entry,
               sizeof(entry),
               1,
               f) != 1)
    {
        ESP_LOGE(TAG, "Error writing log entry");
        fclose(f);
        xSemaphoreGive(s_log_mutex);
        return;
    }

    /*
     * Actualizar cabecera
     */

    hdr.write_index =
        (hdr.write_index + 1) % MAX_LOGS;

    if (hdr.count < MAX_LOGS)
        hdr.count++;

    if (fseek(f, 0, SEEK_SET) != 0)
    {
        ESP_LOGE(TAG, "Header seek failed");
        fclose(f);
        xSemaphoreGive(s_log_mutex);
        return;
    }

    if (fwrite(&hdr,
               sizeof(hdr),
               1,
               f) != 1)
    {
        ESP_LOGE(TAG, "Header write failed");
        fclose(f);
        xSemaphoreGive(s_log_mutex);
        return;
    }

    fflush(f);
    fclose(f);

    xSemaphoreGive(s_log_mutex);
}

esp_err_t log_read_all_json(httpd_req_t *req)
{
    if (!s_log_mutex)
    {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }

    if (xSemaphoreTake(s_log_mutex,
                       pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        httpd_resp_set_status(req,
                              "500 Internal Server Error");

        return httpd_resp_sendstr(
            req,
            "{\"error\":\"mutex timeout\"}");
    }

    FILE *f = fopen("/fs/logs.dat", "rb");

    httpd_resp_set_type(req, "application/json");

    if (!f)
    {
        xSemaphoreGive(s_log_mutex);
        return httpd_resp_sendstr(req, "[]");
    }

    log_header_t hdr;

    if (fread(&hdr, sizeof(hdr),
              1,
              f) != 1)
    {
        fclose(f);
        xSemaphoreGive(s_log_mutex);

        return httpd_resp_sendstr(req, "[]");
    }

    if (hdr.magic != LOG_MAGIC ||
        hdr.version != LOG_VERSION ||
        hdr.write_index >= MAX_LOGS ||
        hdr.count > MAX_LOGS)
    {
        ESP_LOGE(TAG,
                 "Invalid log header "
                 "(magic=%08lX ver=%u idx=%lu count=%lu)",
                 (unsigned long)hdr.magic,
                 (unsigned)hdr.version,
                 (unsigned long)hdr.write_index,
                 (unsigned long)hdr.count);

        fclose(f);
        xSemaphoreGive(s_log_mutex);

        return httpd_resp_sendstr(req, "[]");
    }

    esp_err_t err =
        httpd_resp_send_chunk(req, "[", 1);

    if (err != ESP_OK)
    {
        fclose(f);
        xSemaphoreGive(s_log_mutex);
        return err;
    }

    bool first = true;
    char temp[128];
    log_t e;

    uint32_t start;

    if (hdr.count < MAX_LOGS)
    {
        start = 0;
    }
    else
    {
        start = hdr.write_index;
    }

    for (uint32_t i = 0; i < hdr.count; i++)
    {
        uint32_t index =
            (start + i) % MAX_LOGS;

        long offset =
            sizeof(log_header_t) +
            ((long)index * sizeof(log_t));

        if (fseek(f, offset, SEEK_SET) != 0)
        {
            fclose(f);
            xSemaphoreGive(s_log_mutex);
            return ESP_FAIL;
        }

        if (fread(&e,
                  sizeof(log_t),
                  1,
                  f) != 1)
        {
            fclose(f);
            xSemaphoreGive(s_log_mutex);
            return ESP_FAIL;
        }

        int len = snprintf(
            temp,
            sizeof(temp),
            "%s{\"event_id\":%u,"
            "\"value\":%llu,"
            "\"ts\":%llu,"
            "\"port_id\":%u}",
            first ? "" : ",",
            (unsigned)e.event_id,
            (unsigned long long)e.value,
            (unsigned long long)e.ts,
            (unsigned)e.port_id);

        if (len < 0 || len >= sizeof(temp))
        {
            fclose(f);
            xSemaphoreGive(s_log_mutex);

            httpd_resp_send_chunk(req,
                                  NULL,
                                  0);

            return ESP_FAIL;
        }

        err = httpd_resp_send_chunk(req, temp, len);

        if (err != ESP_OK)
        {
            fclose(f);
            xSemaphoreGive(s_log_mutex);
            return err;
        }

        first = false;
    }

    err = httpd_resp_send_chunk(req, "]", 1);

    fclose(f);
    xSemaphoreGive(s_log_mutex);

    if (err != ESP_OK)
        return err;

    return httpd_resp_send_chunk(req, NULL, 0);
}