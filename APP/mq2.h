#ifndef MQ2_H
#define MQ2_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

void mq2_task(void);    // MQ2 传感器任务函数，基于 DMA 的方法
void mq2_task1(void);   // MQ2 传感器任务函数，常规读取方法，可以注释掉

#ifdef __cplusplus
}
#endif

#endif /* MQ2_H */
