#include "pwm_motor.h"

#define PWM_MOTOR_TIMER_HANDLE        htim3
#define PWM_MOTOR_TIMER_CHANNEL       TIM_CHANNEL_1
#define PWM_MOTOR_PWM_PERIOD          3599U
#define PWM_MOTOR_PWM_COMPARE_MAX     (PWM_MOTOR_PWM_PERIOD + 1U)

#define PWM_MOTOR_AIN1_GPIO_PORT      GPIOA
#define PWM_MOTOR_AIN1_PIN            GPIO_PIN_12
#define PWM_MOTOR_AIN2_GPIO_PORT      GPIOA
#define PWM_MOTOR_AIN2_PIN            GPIO_PIN_11
#define PWM_MOTOR_STBY_GPIO_PORT      GPIOB
#define PWM_MOTOR_STBY_PIN            GPIO_PIN_15

#define PWM_MOTOR_SPEED_MAX_PERCENT   100U
#define PWM_MOTOR_ENABLED             1U
#define PWM_MOTOR_DISABLED            0U

static uint8_t pwm_motor_speed_percent = 0U;
static uint8_t pwm_motor_enabled = PWM_MOTOR_DISABLED;
static pwm_motor_dir_t pwm_motor_direction = PWM_MOTOR_DIR_FORWARD;

/**
 * @brief       写入 PWM 比较值
 * @param       compare TIM 比较值
 * @retval      无
 */
static void pwm_motor_set_compare(uint32_t compare)
{
    if (compare > PWM_MOTOR_PWM_COMPARE_MAX) {
        compare = PWM_MOTOR_PWM_COMPARE_MAX;
    }

    __HAL_TIM_SET_COMPARE(&PWM_MOTOR_TIMER_HANDLE, PWM_MOTOR_TIMER_CHANNEL, compare);
}

/**
 * @brief       将百分比转换为 TIM 比较值
 * @param       percent 速度百分比
 * @retval      TIM 比较值
 */
static uint32_t pwm_motor_percent_to_compare(uint8_t percent)
{
    if (percent > PWM_MOTOR_SPEED_MAX_PERCENT) {
        percent = PWM_MOTOR_SPEED_MAX_PERCENT;
    }

    return (PWM_MOTOR_PWM_COMPARE_MAX * (uint32_t)percent) / PWM_MOTOR_SPEED_MAX_PERCENT;
}

/**
 * @brief       写入 TB6612FNG A 通道输入脚
 * @param       ain1 AIN1 电平
 * @param       ain2 AIN2 电平
 * @retval      无
 */
static void pwm_motor_write_inputs(GPIO_PinState ain1, GPIO_PinState ain2)
{
    HAL_GPIO_WritePin(PWM_MOTOR_AIN1_GPIO_PORT, PWM_MOTOR_AIN1_PIN, ain1);
    HAL_GPIO_WritePin(PWM_MOTOR_AIN2_GPIO_PORT, PWM_MOTOR_AIN2_PIN, ain2);
}

/**
 * @brief       按当前方向配置 TB6612FNG 输入脚
 * @param       direction 目标方向
 * @retval      无
 */
static void pwm_motor_apply_direction(pwm_motor_dir_t direction)
{
    if (direction == PWM_MOTOR_DIR_REVERSE) {
        pwm_motor_write_inputs(GPIO_PIN_RESET, GPIO_PIN_SET);
    } else {
        pwm_motor_write_inputs(GPIO_PIN_SET, GPIO_PIN_RESET);
    }
}

/**
 * @brief       使能 TB6612FNG，保持当前停止或方向状态
 * @param       无
 * @retval      无
 */
static void pwm_motor_enable_driver(void)
{
    HAL_GPIO_WritePin(PWM_MOTOR_STBY_GPIO_PORT, PWM_MOTOR_STBY_PIN, GPIO_PIN_SET);
    pwm_motor_enabled = PWM_MOTOR_ENABLED;
}

/**
 * @brief       初始化电机驱动并保持安全停止
 * @param       无
 * @retval      0 成功，非 0 失败
 */
uint8_t pwm_motor_init(void)
{
    pwm_motor_enabled = PWM_MOTOR_DISABLED;
    pwm_motor_speed_percent = 0U;
    pwm_motor_direction = PWM_MOTOR_DIR_FORWARD;

    pwm_motor_write_inputs(GPIO_PIN_RESET, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWM_MOTOR_STBY_GPIO_PORT, PWM_MOTOR_STBY_PIN, GPIO_PIN_RESET);
    pwm_motor_set_compare(0U);

    if (HAL_TIM_PWM_Start(&PWM_MOTOR_TIMER_HANDLE, PWM_MOTOR_TIMER_CHANNEL) != HAL_OK) {
        return 1U;
    }

    pwm_motor_apply_direction(pwm_motor_direction);
    pwm_motor_enable_driver();
    pwm_motor_set_compare(0U);

    return 0U;
}

/**
 * @brief       滑行停止电机
 * @param       无
 * @retval      无
 */
void pwm_motor_stop(void)
{
    pwm_motor_speed_percent = 0U;
    pwm_motor_set_compare(0U);
    pwm_motor_write_inputs(GPIO_PIN_RESET, GPIO_PIN_RESET);
}

/**
 * @brief       设置电机速度百分比
 * @param       percent 速度百分比，超过 100 按 100 处理
 * @retval      无
 */
void pwm_motor_set_speed(uint8_t percent)
{
    if (percent > PWM_MOTOR_SPEED_MAX_PERCENT) {
        percent = PWM_MOTOR_SPEED_MAX_PERCENT;
    }

    pwm_motor_speed_percent = percent;

    if (percent == 0U) {
        pwm_motor_stop();
        return;
    }

    if (pwm_motor_enabled == PWM_MOTOR_DISABLED) {
        pwm_motor_enable_driver();
    }

    pwm_motor_apply_direction(pwm_motor_direction);
    pwm_motor_set_compare(pwm_motor_percent_to_compare(percent));
}

/**
 * @brief       安全切换电机方向
 * @param       direction 目标方向
 * @retval      无
 */
void pwm_motor_set_direction(pwm_motor_dir_t direction)
{
    uint8_t requested_speed = pwm_motor_speed_percent;

    if ((direction != PWM_MOTOR_DIR_FORWARD) && (direction != PWM_MOTOR_DIR_REVERSE)) {
        direction = PWM_MOTOR_DIR_FORWARD;
    }

    if (direction == pwm_motor_direction) {
        pwm_motor_apply_direction(pwm_motor_direction);
        return;
    }

    pwm_motor_set_compare(0U);
    pwm_motor_write_inputs(GPIO_PIN_RESET, GPIO_PIN_RESET);
    pwm_motor_direction = direction;
    pwm_motor_apply_direction(pwm_motor_direction);

    if ((requested_speed > 0U) && (pwm_motor_enabled == PWM_MOTOR_ENABLED)) {
        pwm_motor_speed_percent = requested_speed;
        pwm_motor_set_compare(pwm_motor_percent_to_compare(requested_speed));
    } else {
        pwm_motor_speed_percent = 0U;
    }
}

/**
 * @brief       设置带方向的速度百分比
 * @param       percent -100 到 100，正数正转，负数反转，0 停止
 * @retval      无
 */
void pwm_motor_set_signed_speed(int16_t percent)
{
    if (percent == 0) {
        pwm_motor_stop();
        return;
    }

    if (percent < 0) {
        pwm_motor_set_direction(PWM_MOTOR_DIR_REVERSE);
        if (percent < -((int16_t)PWM_MOTOR_SPEED_MAX_PERCENT)) {
            percent = (int16_t)PWM_MOTOR_SPEED_MAX_PERCENT;
        } else {
            percent = (int16_t)(-percent);
        }
    } else {
        pwm_motor_set_direction(PWM_MOTOR_DIR_FORWARD);
        if (percent > (int16_t)PWM_MOTOR_SPEED_MAX_PERCENT) {
            percent = (int16_t)PWM_MOTOR_SPEED_MAX_PERCENT;
        }
    }

    pwm_motor_set_speed((uint8_t)percent);
}
