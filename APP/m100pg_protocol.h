#ifndef M100PG_PROTOCOL_H
#define M100PG_PROTOCOL_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t temp;
    uint8_t hum;
    uint32_t mq2;
    float pitch;
    float roll;
    float yaw;
    int32_t hr;
    int32_t spo2;
    uint8_t led;
} m100pg_upload_data_t;

typedef enum {
    M100PG_DOWNLINK_CMD_NONE = 0,
    M100PG_DOWNLINK_CMD_LED_ON,
    M100PG_DOWNLINK_CMD_LED_OFF,
    M100PG_DOWNLINK_CMD_LED_WHITE,
    M100PG_DOWNLINK_CMD_LED_RED,
    M100PG_DOWNLINK_CMD_LED_GREEN
} m100pg_downlink_cmd_t;

uint16_t m100pg_protocol_build_upload(char *buffer, uint16_t buffer_size,
                                      const m100pg_upload_data_t *data); // 拼接上传帧
uint8_t m100pg_protocol_parse_downlink(const uint8_t *data, uint16_t len,
                                       m100pg_downlink_cmd_t *cmd);      // 解析下发命令

#ifdef __cplusplus
}
#endif

#endif /* M100PG_PROTOCOL_H */
