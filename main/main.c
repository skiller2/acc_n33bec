
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <stdint.h>

extern void fs_init();
extern void http_init();
extern void ws_init();
extern void wiegand_init(int,int,void(*cb)(uint64_t));
extern void card_store_init();
extern void log_store_init();
extern void process_card(uint64_t);

static QueueHandle_t queue;

typedef struct { uint64_t card; } evt_t;

void wiegand_cb(uint64_t c){ evt_t e={.card=c}; xQueueSendFromISR(queue,&e,NULL);} 

void worker(void* p){ evt_t e;
 while(1){
  if(xQueueReceive(queue,&e,portMAX_DELAY)){
    process_card(e.card);
  }
 }
}

void app_main(){
 fs_init();
 card_store_init();
 log_store_init();
 http_init();
 ws_init();
 queue=xQueueCreate(64,sizeof(evt_t));
 wiegand_init(4,5,wiegand_cb);
 xTaskCreate(worker,"worker",4096,NULL,5,NULL);
}
