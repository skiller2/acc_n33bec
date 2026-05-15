
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"
static volatile int bits=0;
static volatile uint64_t data=0;
static int64_t last=0;
static void (*cb)(uint64_t);
static int D0,D1;

static void IRAM_ATTR isr(void* arg){
 int pin=(int)arg;
 int64_t now=esp_timer_get_time();
 if(now-last>30000){bits=0;data=0;}
 last=now;
 data<<=1;
 if(pin==D1)data|=1;
 bits++;
 if(bits==26){ cb(data); bits=0; data=0; }
}

void wiegand_init(int d0,int d1,void(*f)(uint64_t)){
 D0=d0;D1=d1;cb=f;
 gpio_set_direction(D0,GPIO_MODE_INPUT);
 gpio_set_direction(D1,GPIO_MODE_INPUT);
 gpio_set_intr_type(D0,GPIO_INTR_NEGEDGE);
 gpio_set_intr_type(D1,GPIO_INTR_NEGEDGE);
 gpio_install_isr_service(0);
 gpio_isr_handler_add(D0,isr,(void*)D0);
 gpio_isr_handler_add(D1,isr,(void*)D1);
}
