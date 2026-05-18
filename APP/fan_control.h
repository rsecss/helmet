#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

void fan_control_init(void);                      // 初始化风扇仲裁状态
void fan_control_task(void);                      // 温度自动风扇周期任务
void fan_control_set_manual_gear(uint8_t gear);   // 设置手动基础档位
uint8_t fan_control_get_manual_gear(void);        // 获取手动基础档位
uint8_t fan_control_get_output_gear(void);        // 获取最终输出档位
uint8_t fan_control_is_auto_active(void);         // 获取温度自动状态
uint8_t fan_control_get_temp_on_threshold(void);  // 获取启动阈值
uint8_t fan_control_get_temp_off_threshold(void); // 获取恢复阈值

#ifdef __cplusplus
}
#endif

#endif /* FAN_CONTROL_H */
