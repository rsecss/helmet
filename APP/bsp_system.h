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
#include <stdlib.h>
#include <stdbool.h>

/* 外设头文件 */
#include "adc.h"
#include "tim.h"
#include "i2c.h"

/* 应用层头文件 */
#include "mq2.h"                        // MQ2 烟雾传感器模块
#include "dht11.h"                      // DHT11 温湿度传感器模块
#include "mpu6050.h"                    // MPU6050 六轴传感器模块
#include "max30102.h"                   // MAX30102 心率血氧传感器模块
#include "m100pg_protocol.h"            // M100PG 上下行协议模块
#include "m100pg.h"                     // M100PG 4G 联网模块
#include "m100pg_bsp.h"                 // M100PG 协议库 ↔ 板级硬件桥接
#include "asrpro.h"                     // ASRPro 离线语音控制模块
#include "rgb_led.h"                    // 三色 LED 模块
#include "helmet_alarm.h"               // 本地安全报警输出仲裁
#include "pwm_motor.h"                  // PWM 电机驱动模块
#include "st7735.h"                     // ST7735 彩色显示屏模块
#include "lcd_app.h"                    // LCD 传感器数据显示页面
#include "scheduler.h"

/* 全局变量，数组声明 */
extern uint32_t dma_buff[30];


#ifdef __cplusplus
}
#endif

#endif /* BSP_SYSTEM_H */
