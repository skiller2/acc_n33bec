
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"

#define MAX_LOGS 1000

typedef struct
{
    uint64_t id;
    int64_t ts;
    int reader_id;
    int ok;
} log_t;

void log_store_init()
{
    FILE *f = fopen("/fs/logs.dat", "ab");
    if (f)
        fclose(f);
}

void log_add(uint64_t id, int64_t ts, int reader_id,int ok)
{
    if (ts==0)
        ts=esp_timer_get_time();

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
        logs[log_count - 1] = (log_t){id, ts, reader_id, ok};

        // Rewrite entire file
        rewind(f);
        fwrite(logs, sizeof(log_t), log_count, f);

        free(logs);
    } else {
        // Just append new log
        fseek(f, 0, SEEK_END);
        log_t l = {id, ts, reader_id, ok};
        fwrite(&l, sizeof(l), 1, f);
    }

    fclose(f);
}


char* log_read_all_json()
{
    FILE *f = fopen("/fs/logs.dat", "rb");

    size_t buf_size = 8192;
    char *json = malloc(buf_size);
    strcpy(json, "[");

    log_t e;
    int first = 1;

    while (fread(&e, sizeof(e), 1, f)) {
        char temp[128];
        snprintf(temp, sizeof(temp),
            "%s{\"card\":%llu,\"ts\":%lld,\"reader\":%d}",
            first ? "" : ",",
            e.id, e.ts, e.reader_id);

        if (strlen(json) + strlen(temp) + 2 > buf_size) {
            buf_size *= 2;
            json = realloc(json, buf_size);
        }

        strcat(json, temp);
        first = 0;
    }

    strcat(json, "]");
    fclose(f);

    return json;
}

