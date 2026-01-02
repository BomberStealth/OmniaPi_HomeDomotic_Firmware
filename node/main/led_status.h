#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <stdint.h>

void led_status_init(void);
void led_status_update(void);
void led_blink(uint8_t count, uint32_t interval_ms);

#endif
