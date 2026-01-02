#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

void relay_control_init(void);
void relay_set_state(bool on);
void relay_set_channel(uint8_t channel, bool on);
bool relay_get_state(void);
bool relay_get_channel_state(uint8_t channel);
void relay_toggle(void);
void relay_toggle_channel(uint8_t channel);

#endif
