#include "fan_control.h"
#include "dht11.h"
#include "pwm_motor.h"

#define FAN_CONTROL_GEAR_MAX              3U
#define FAN_CONTROL_TEMP_ON_C             30U
#define FAN_CONTROL_TEMP_OFF_C            28U
#define FAN_CONTROL_AUTO_GEAR             1U
#define FAN_CONTROL_INVALID_HOLD_MS       3000U

static const uint8_t fan_control_gear_to_percent[] = {
    0U, 33U, 66U, 100U
};

static uint8_t fan_control_manual_gear = 0U;
static uint8_t fan_control_output_gear = 0U;
static uint8_t fan_control_auto_active = 0U;
static uint8_t fan_control_manual_off_override = 0U;
static uint32_t fan_control_invalid_since = 0UL;

/**
 * @brief       裁剪风扇档位到协议支持范围
 * @param       gear 输入档位
 * @retval      0..3 档位
 */
static uint8_t fan_control_clamp_gear(uint8_t gear)
{
    return (gear > FAN_CONTROL_GEAR_MAX) ? FAN_CONTROL_GEAR_MAX : gear;
}

/**
 * @brief       把最终档位应用到底层 PWM 电机驱动
 * @param       gear 0..3 档位
 * @retval      无
 */
static void fan_control_apply_output(uint8_t gear)
{
    gear = fan_control_clamp_gear(gear);
    fan_control_output_gear = gear;
    pwm_motor_set_speed(fan_control_gear_to_percent[gear]);
}

/**
 * @brief       按手动基础档位与高温自动状态计算最终输出
 * @param       无
 * @retval      无
 */
static void fan_control_refresh_output(void)
{
    uint8_t target_gear = fan_control_manual_gear;

    if (fan_control_manual_off_override != 0U) {
        fan_control_apply_output(0U);
        return;
    }
    if ((fan_control_auto_active != 0U) && (target_gear < FAN_CONTROL_AUTO_GEAR)) {
        target_gear = FAN_CONTROL_AUTO_GEAR;
    }
    fan_control_apply_output(target_gear);
}

/**
 * @brief       根据最近一次有效温度更新自动状态
 * @param       temp_c DHT11 温度，单位摄氏度
 * @retval      无
 */
static void fan_control_update_valid_temp(uint8_t temp_c)
{
    fan_control_invalid_since = 0UL;

    if (fan_control_manual_off_override != 0U) {
        fan_control_auto_active = 0U;
        if (temp_c <= FAN_CONTROL_TEMP_OFF_C) {
            fan_control_manual_off_override = 0U;
        }
        return;
    }
    if (temp_c >= FAN_CONTROL_TEMP_ON_C) {
        fan_control_auto_active = 1U;
        return;
    }
    if (temp_c <= FAN_CONTROL_TEMP_OFF_C) {
        fan_control_auto_active = 0U;
    }
}

/**
 * @brief       处理 DHT11 连续无效时的自动状态保持窗口
 * @param       now_ms 当前系统 tick
 * @retval      无
 */
static void fan_control_update_invalid_temp(uint32_t now_ms)
{
    if (fan_control_manual_off_override != 0U) {
        fan_control_auto_active = 0U;
        fan_control_invalid_since = 0UL;
        return;
    }
    if (fan_control_auto_active == 0U) {
        fan_control_invalid_since = 0UL;
        return;
    }

    if (fan_control_invalid_since == 0UL) {
        fan_control_invalid_since = now_ms;
        return;
    }
    if ((uint32_t)(now_ms - fan_control_invalid_since) >= FAN_CONTROL_INVALID_HOLD_MS) {
        fan_control_auto_active = 0U;
        fan_control_invalid_since = 0UL;
    }
}

/**
 * @brief       初始化风扇仲裁状态，保持手动 0 档和自动关闭
 * @param       无
 * @retval      无
 */
void fan_control_init(void)
{
    fan_control_manual_gear = 0U;
    fan_control_output_gear = 0U;
    fan_control_auto_active = 0U;
    fan_control_manual_off_override = 0U;
    fan_control_invalid_since = 0UL;
    fan_control_apply_output(0U);
}

/**
 * @brief       设置手动基础档位并刷新最终输出
 * @param       gear 0..3 档位，超过 3 按 3 处理
 * @retval      无
 */
void fan_control_set_manual_gear(uint8_t gear)
{
    fan_control_manual_gear = fan_control_clamp_gear(gear);

    if (fan_control_manual_gear == 0U) {
        if ((fan_control_auto_active != 0U) ||
            ((dht11_is_valid() != 0U) &&
             (dht11_get_temperature() >= FAN_CONTROL_TEMP_ON_C))) {
            fan_control_manual_off_override = 1U;
            fan_control_auto_active = 0U;
            fan_control_invalid_since = 0UL;
        }
    } else {
        fan_control_manual_off_override = 0U;
    }
    fan_control_refresh_output();
}

/**
 * @brief       获取手动基础档位
 * @param       无
 * @retval      0..3 档位
 */
uint8_t fan_control_get_manual_gear(void)
{
    return fan_control_manual_gear;
}

/**
 * @brief       获取最终输出档位
 * @param       无
 * @retval      0..3 档位
 */
uint8_t fan_control_get_output_gear(void)
{
    return fan_control_output_gear;
}

/**
 * @brief       获取温度自动风扇状态
 * @param       无
 * @retval      1 自动高温覆盖有效，0 无自动覆盖
 */
uint8_t fan_control_is_auto_active(void)
{
    return fan_control_auto_active;
}

/**
 * @brief       获取温度自动启动阈值
 * @param       无
 * @retval      摄氏度阈值
 */
uint8_t fan_control_get_temp_on_threshold(void)
{
    return FAN_CONTROL_TEMP_ON_C;
}

/**
 * @brief       获取温度自动恢复阈值
 * @param       无
 * @retval      摄氏度阈值
 */
uint8_t fan_control_get_temp_off_threshold(void)
{
    return FAN_CONTROL_TEMP_OFF_C;
}

/**
 * @brief       温度自动风扇周期任务，读取 DHT11 缓存并刷新输出
 * @param       无
 * @retval      无
 */
void fan_control_task(void)
{
    uint32_t now_ms = HAL_GetTick();

    if (dht11_is_valid() != 0U) {
        fan_control_update_valid_temp(dht11_get_temperature());
    } else {
        fan_control_update_invalid_temp(now_ms);
    }
    fan_control_refresh_output();
}
