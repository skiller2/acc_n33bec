
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

typedef struct
{
    uint8_t event_id;
    uint64_t ts;
    int port_id;
    uint64_t value;
} log_t;

void log_store_init()
{
    FILE *f = fopen("/fs/logs.dat", "ab");
    if (f)
        fclose(f);
}

uint64_t getTimeStamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t epoch_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
    return epoch_us;
}

void log_add(uint8_t event_id, int port_id, uint64_t value, int64_t ts)
{
    if (ts==0)
        ts=getTimeStamp();

    FILE *f = fopen("/fs/logs.dat", "r+b");
    if (!f) {
        f = fopen("/fs/logs.dat", "wb");
        if (!f) return;
    }

    // Count existing logs
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    int log_count = file_size / sizeof(log_t);

    if (log_count >= MAX_LOGS) {
        // Circular buffer: read all logs, shift left by 1, add new at end
        log_t *logs = malloc(log_count * sizeof(log_t));
        rewind(f);
        fread(logs, sizeof(log_t), log_count, f);

        // Shift logs left (remove oldest)
        for (int i = 0; i < log_count - 1; i++) {
            logs[i] = logs[i + 1];
        }

        // Add new log at the end
        
        logs[log_count - 1] = (log_t){event_id, ts, port_id, value};

        // Rewrite entire file
        rewind(f);
        fwrite(logs, sizeof(log_t), log_count, f);

        free(logs);
    } else {
        // Just append new log
        fseek(f, 0, SEEK_END);

        log_t l = {event_id, ts, port_id, value};
        fwrite(&l, sizeof(l), 1, f);
    }

    fclose(f);
}


esp_err_t log_read_all_json(httpd_req_t *req)
{
    FILE *f = fopen("/fs/logs.dat", "rb");

    httpd_resp_set_type(req, "application/json");

    if (!f)
    {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    // Inicio del array
    esp_err_t err = httpd_resp_send_chunk(req, "[", 1);
    if (err != ESP_OK)
    {
        fclose(f);
        return err;
    }

    log_t e;
    bool first = true;
    char temp[128];

    while (fread(&e, sizeof(e), 1, f) == 1)
    {
        int len = snprintf(
            temp,
            sizeof(temp),
            "%s{\"event_id\":%u,\"value\":%llu,\"ts\":%llu,\"port_id\":%d}",
            first ? "" : ",",
            (unsigned int)e.event_id,
            (unsigned long long)e.value,
            (unsigned long long)e.ts,
            e.port_id
        );

        if (len < 0 || len >= sizeof(temp))
        {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }

        err = httpd_resp_send_chunk(req, temp, len);

        if (err != ESP_OK)
        {
            fclose(f);
            return err;
        }

        first = false;
    }

    // Cierre del JSON
    err = httpd_resp_send_chunk(req, "]", 1);

    fclose(f);

    if (err != ESP_OK)
        return err;

    // Fin de chunked response
    return httpd_resp_send_chunk(req, NULL, 0);
}