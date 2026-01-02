#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

// Message types (compatible with Gateway Arduino)
#define MSG_HEARTBEAT       0x01
#define MSG_HEARTBEAT_ACK   0x02
#define MSG_COMMAND         0x20
#define MSG_COMMAND_ACK     0x21

// Command actions
#define CMD_OFF             0x00
#define CMD_ON              0x01
#define CMD_TOGGLE          0x02

void espnow_handler_init(uint8_t wifi_channel);
bool espnow_is_gateway_known(void);
uint32_t espnow_get_last_heartbeat_time(void);

#endif
