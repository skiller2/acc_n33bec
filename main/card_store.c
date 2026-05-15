
#include <stdint.h>
#include <stdio.h>
#define MAX 20000
static uint64_t cards[MAX];
static int count=0;

void card_store_init(){
 FILE*f=fopen("/fs/cards.dat","rb");
 if(!f)return;
 while(fread(&cards[count],8,1,f))count++;
 fclose(f);
}

int card_exists(uint64_t id){
 for(int i=0;i<count;i++) if(cards[i]==id) return 1;
 return 0;
}

void card_add(uint64_t id){
 cards[count++]=id;
 FILE*f=fopen("/fs/cards.dat","ab"); fwrite(&id,8,1,f); fclose(f);
}
