#ifndef BSP_SYSTEM_H
#define BSP_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* 头文件 */
#include "main.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* 外设头文件 */
#include "adc.h"
#include "tim.h"
#include "sys.h"

/* 应用层头文件 */
#include "mq2.h"                        // MQ2 烟雾传感器模块
#include "dht11.h"                      // DHT11 温湿度传感器模块
#include "scheduler.h"


/* 全局变量，数组声明 */
extern uint32_t dma_buff[30];


#ifdef __cplusplus
}
#endif

#endif /* BSP_SYSTEM_H */
