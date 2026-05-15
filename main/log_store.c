
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint64_t id;
    int64_t ts;
    int ok;
} log_t;

void log_store_init()
{
    FILE *f = fopen("/fs/logs.dat", "ab");
    if (f)
        fclose(f);
}

void log_add(uint64_t id, int64_t ts, int ok)
{
    FILE *f = fopen("/fs/logs.dat", "ab");
    log_t l = {id, ts, ok};
    fwrite(&l, sizeof(l), 1, f);
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
            "%s{\"card\":%llu,\"ts\":%lld}",
            first ? "" : ",",
            e.id, e.ts);

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

