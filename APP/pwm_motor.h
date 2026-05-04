#ifndef PWM_MOTOR_H
#define PWM_MOTOR_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PWM_MOTOR_DIR_FORWARD = 0,
    PWM_MOTOR_DIR_REVERSE
} pwm_motor_dir_t;

uint8_t pwm_motor_init(void);                         // 初始化并保持安全停止
void pwm_motor_stop(void);                            // 滑行停止
void pwm_motor_set_speed(uint8_t percent);            // 设置速度百分比
void pwm_motor_set_direction(pwm_motor_dir_t direction); // 设置方向
void pwm_motor_set_signed_speed(int16_t percent);     // 设置带方向速度

#ifdef __cplusplus
}
#endif

#endif /* PWM_MOTOR_H */
