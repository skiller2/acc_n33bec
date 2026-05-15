
#include <stdio.h>
#include <stdint.h>

typedef struct { uint64_t id; int64_t ts; int ok;} log_t;

void log_store_init(){ FILE*f=fopen("/fs/logs.dat","ab"); if(f) fclose(f); }

void log_add(uint64_t id,int64_t ts,int ok){
 FILE*f=fopen("/fs/logs.dat","ab"); log_t l={id,ts,ok}; fwrite(&l,sizeof(l),1,f); fclose(f);
}
