#ifndef HELMET_ALARM_H
#define HELMET_ALARM_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

void helmet_alarm_set_base_led(rgb_led_color_t color);  // 设置非报警状态下的 LED 颜色
void helmet_alarm_task(void);                           // 本地安全报警输出任务

#ifdef __cplusplus
}
#endif

#endif /* HELMET_ALARM_H */
