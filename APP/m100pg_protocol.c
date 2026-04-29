#include "m100pg_protocol.h"

#define M100PG_CMD_LED_ON       "LED_ON"
#define M100PG_CMD_LED_OFF      "LED_OFF"
#define M100PG_CMD_LED_WHITE    "LED_WHITE"
#define M100PG_CMD_LED_RED      "LED_RED"
#define M100PG_CMD_LED_GREEN    "LED_GREEN"

static uint8_t m100pg_protocol_match_cmd(const uint8_t *data, uint16_t len,
                                         const char *text)
{
    uint16_t text_len = (uint16_t)strlen(text);

    return ((len == text_len) && (memcmp(data, text, text_len) == 0)) ? 1U : 0U;
}

/**
 * @brief       拼接 4G 上行传感器数据帧
 * @param       buffer 输出缓冲区
 * @param       buffer_size 输出缓冲区长度
 * @param       data 上传数据快照
 * @retval      帧长度，0 表示参数错误或缓冲区不足
 */
uint16_t m100pg_protocol_build_upload(char *buffer, uint16_t buffer_size,
                                      const m100pg_upload_data_t *data)
{
    int written;

    if ((buffer == NULL) || (data == NULL) || (buffer_size == 0U))
        return 0;

    written = snprintf(buffer, buffer_size,
                       "UP,temp=%u,hum=%u,mq2=%lu,pitch=%.1f,roll=%.1f,yaw=%.1f,hr=%ld,spo2=%ld,led=%u\r\n",
                       data->temp,
                       data->hum,
                       (unsigned long)data->mq2,
                       data->pitch,
                       data->roll,
                       data->yaw,
                       (long)data->hr,
                       (long)data->spo2,
                       data->led);

    if ((written <= 0) || ((uint16_t)written >= buffer_size))
        return 0;

    return (uint16_t)written;
}

/**
 * @brief       解析 4G 下发控制命令
 * @param       data 接收数据
 * @param       len 接收长度
 * @param       cmd 命令输出
 * @retval      1 解析到合法命令，0 未匹配
 */
uint8_t m100pg_protocol_parse_downlink(const uint8_t *data, uint16_t len,
                                       m100pg_downlink_cmd_t *cmd)
{
    if (cmd != NULL)
        *cmd = M100PG_DOWNLINK_CMD_NONE;

    if ((data == NULL) || (cmd == NULL) || (len == 0U))
        return 0;

    while ((len > 0U) && ((data[0] == ' ') || (data[0] == '\t') ||
                          (data[0] == '\r') || (data[0] == '\n'))) {
        data++;
        len--;
    }

    while ((len > 0U) && ((data[len - 1U] == ' ') || (data[len - 1U] == '\t') ||
                          (data[len - 1U] == '\r') || (data[len - 1U] == '\n'))) {
        len--;
    }

    if (m100pg_protocol_match_cmd(data, len, M100PG_CMD_LED_ON) != 0U) {
        *cmd = M100PG_DOWNLINK_CMD_LED_ON;
        return 1;
    }

    if (m100pg_protocol_match_cmd(data, len, M100PG_CMD_LED_OFF) != 0U) {
        *cmd = M100PG_DOWNLINK_CMD_LED_OFF;
        return 1;
    }

    if (m100pg_protocol_match_cmd(data, len, M100PG_CMD_LED_WHITE) != 0U) {
        *cmd = M100PG_DOWNLINK_CMD_LED_WHITE;
        return 1;
    }

    if (m100pg_protocol_match_cmd(data, len, M100PG_CMD_LED_RED) != 0U) {
        *cmd = M100PG_DOWNLINK_CMD_LED_RED;
        return 1;
    }

    if (m100pg_protocol_match_cmd(data, len, M100PG_CMD_LED_GREEN) != 0U) {
        *cmd = M100PG_DOWNLINK_CMD_LED_GREEN;
        return 1;
    }

    return 0;
}
