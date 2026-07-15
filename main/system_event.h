#include <time.h>

typedef enum {
    SYSTEM_EVENT_ETHERNET_CONNECTED,
    SYSTEM_EVENT_ETHERNET_DISCONNECTED,
    SYSTEM_EVENT_RTC_CONNECTED,
    SYSTEM_EVENT_RTC_DISCONNECTED,
    SYSTEM_EVENT_ETHERNET_GOT_IP
} system_event_type_t;

typedef struct
{
    system_event_type_t type;
    time_t timestamp; // Optional timestamp for the event
} system_event_t;