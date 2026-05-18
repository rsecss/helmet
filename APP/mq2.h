#ifndef MQ2_H
#define MQ2_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

void mq2_task(void);              // MQ2 传感器任务函数，基于 DMA 的方法
float mq2_get_ppm(void);          // 获取最近一次 LPG 曲线折算值
float mq2_get_trend_index(void);  // 获取归一化烟雾趋势指数
float mq2_get_rs_r0_ratio(void);  // 获取最近一次 Rs/R0 比值
float mq2_get_voltage(void);      // 获取最近一次 ADC 电压
float mq2_get_r0(void);           // 获取当前 R0 校准值
uint8_t mq2_is_calibrated(void);  // 获取 MQ2 清洁空气校准状态
uint8_t mq2_is_trend_alarm(void); // 获取 MQ2 趋势异常报警状态

#ifdef __cplusplus
}
#endif

#endif /* MQ2_H */
