#ifndef M100PG_H
#define M100PG_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       初始化 USART2 空闲 DMA 接收
 * @retval      0 成功，非 0 失败
 */
uint8_t m100pg_init(void);

/**
 * @brief       调度器任务：消费 USART2 接收缓存并转发到调试口
 * @param       无
 * @retval      无
 */
void m100pg_task(void);

/**
 * @brief       设置 USART2 到 USART1 的调试转发开关
 * @param       enabled 1 开启，0 关闭
 * @retval      无
 */
void m100pg_set_debug_forward(uint8_t enabled);

/**
 * @brief       通过 USART2 向 4G 模块发送原始数据
 * @param       data 数据指针
 * @param       len 数据长度
 * @retval      0 成功，非 0 失败
 */
uint8_t m100pg_send_bytes(const uint8_t *data, uint16_t len);

/**
 * @brief       USART 空闲接收回调入口，仅处理 USART2
 * @param       huart UART 句柄
 * @param       size 本次 DMA 接收长度
 * @retval      无
 */
void m100pg_rx_event_callback(UART_HandleTypeDef *huart, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif /* M100PG_H */
