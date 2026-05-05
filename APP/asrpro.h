#ifndef ASRPRO_H
#define ASRPRO_H

#include "bsp_system.h"

#ifndef ASRPRO_ENABLE_COMMAND_EXECUTION
#define ASRPRO_ENABLE_COMMAND_EXECUTION    1U
#endif

#ifndef ASRPRO_ENABLE_USART1_DEBUG
#define ASRPRO_ENABLE_USART1_DEBUG         0U
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint8_t asrpro_init(void);                                      // 初始化 USART1 语音命令接收
void asrpro_task(void);                                         // 消费并执行 ASRPro 命令
void asrpro_uart_rx_cplt_callback(UART_HandleTypeDef *huart);   // USART1 单字节接收完成回调

#ifdef __cplusplus
}
#endif

#endif /* ASRPRO_H */
