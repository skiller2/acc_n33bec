#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void card_add(uint64_t);
void card_del(uint64_t);
int card_exists(uint64_t);
char *card_read_all_json(void);

#ifdef __cplusplus
}
#endif