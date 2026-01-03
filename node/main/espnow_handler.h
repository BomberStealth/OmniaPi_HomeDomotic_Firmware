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

// OTA message types
#define MSG_OTA_BEGIN       0x10
#define MSG_OTA_READY       0x11
#define MSG_OTA_DATA        0x12
#define MSG_OTA_ACK         0x13
#define MSG_OTA_END         0x14
#define MSG_OTA_DONE        0x15
#define MSG_OTA_ERROR       0x1F

void espnow_handler_init(uint8_t wifi_channel);
bool espnow_is_gateway_known(void);
uint32_t espnow_get_last_heartbeat_time(void);

#endif
