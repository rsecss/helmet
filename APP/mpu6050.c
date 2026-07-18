#include "mpu6050.h"

extern I2C_HandleTypeDef hi2c1;

/* 配置寄存器（MPU6050 寄存器手册） */
#define MPU6050_PWR_MGMT_1_REG   0x6B
#define MPU6050_SMPLRT_DIV_REG   0x19
#define MPU6050_CONFIG_REG       0x1A
#define MPU6050_GYRO_CONFIG_REG  0x1B
#define MPU6050_ACCEL_CONFIG_REG 0x1C

/* PWR_MGMT_1：复位位（DEVICE_RESET） */
#define MPU6050_PWR_RESET        0x80
/* PWR_MGMT_1：清睡眠位并选用 X 轴陀螺 PLL 作为时钟源 */
#define MPU6050_CLKSEL_PLL_XGYRO 0x01
/* 复位后寄存器稳定所需延时（ms） */
#define MPU6050_RESET_DELAY_MS   100U
/* CONFIG：DLPF 设为 42Hz（DLPF_CFG=3），抑制高频噪声 */
#define MPU6050_DLPF_42HZ        0x03
/* SMPLRT_DIV：陀螺输出 1kHz（DLPF 开启）/(1+7)=125Hz 采样 */
#define MPU6050_SMPLRT_DIV_125HZ 0x07
/* GYRO_CONFIG：±2000 dps 满量程（FS_SEL=3，位 4:3=11） */
#define MPU6050_GYRO_FS_2000DPS  0x18
/* ACCEL_CONFIG：±2g 满量程（AFS_SEL=0） */
#define MPU6050_ACCEL_FS_2G      0x00

/* 满量程换算：陀螺 ±2000dps → rad/s；加速度模值单位为原始 LSB，无需换算 */
#define MPU6050_GYRO_SENS_2000DPS 16.4f          /* LSB/(dps) */
#define MPU6050_DEG_TO_RAD        0.017453293f    /* π/180 */
#define MPU6050_RAD_TO_DEG        57.29578f       /* 180/π */

/* Mahony 滤波参数：固定积分步长 + PI 反馈增益 */
#define MPU6050_MAHONY_DT     0.01f    /* 与 mpu6050_task 周期 10ms 对齐 */
#define MPU6050_MAHONY_KP     2.0f     /* 比例增益，加速度修正陀螺漂移 */
#define MPU6050_MAHONY_KI     0.005f   /* 积分增益，消除稳态零偏 */

/* 陀螺零偏校准：yaw 无重力参考，零偏不校准会以 1~3 dps 匀速漂移 */
#define MPU6050_GYRO_CAL_SAMPLES     200U   /* 采样数，约 400ms */
#define MPU6050_GYRO_CAL_DISCARD     50U    /* 复位/配置后的瞬态样本，丢弃不计 */
#define MPU6050_GYRO_CAL_INTERVAL_MS 2U
#define MPU6050_GYRO_CAL_MAX_SPREAD  164    /* 单轴峰峰值上限（raw，约 10dps），超出视为非静止 */

/* 运行期静止自校准：静止满 2s 重测零偏，自愈上电校准失败与温漂 */
#define MPU6050_GYRO_STILL_THRESHOLD 50     /* 已校准时允许的零偏变化上限（raw，约 3dps） */
#define MPU6050_GYRO_RECAL_SAMPLES   200U   /* 100Hz × 2s */

#define MPU6050_I2C_TIMEOUT_MS 10U
#define MPU6050_TASK_PERIOD_MS 10U
#define MPU6050_FALL_LOW_AVM_THRESHOLD 6000U
#define MPU6050_FALL_IMPACT_AVM_THRESHOLD 28000U
#define MPU6050_FALL_IMPACT_DELTA_THRESHOLD 12000U
#define MPU6050_FALL_ANGLE_DEG 65.0f
#define MPU6050_FALL_STABLE_GVM_THRESHOLD 2500U
#define MPU6050_FALL_LOW_WINDOW_MS 500U
#define MPU6050_FALL_IMPACT_WINDOW_MS 800U
#define MPU6050_FALL_CONFIRM_MS 1500U
#define MPU6050_FALL_HOLD_MS 10000U
#define MPU6050_FALL_RECOVER_MS 1500U
#define MPU6050_FALL_LOW_WINDOW_COUNT (MPU6050_FALL_LOW_WINDOW_MS / MPU6050_TASK_PERIOD_MS)
#define MPU6050_FALL_IMPACT_WINDOW_COUNT (MPU6050_FALL_IMPACT_WINDOW_MS / MPU6050_TASK_PERIOD_MS)
#define MPU6050_FALL_CONFIRM_COUNT (MPU6050_FALL_CONFIRM_MS / MPU6050_TASK_PERIOD_MS)
#define MPU6050_FALL_HOLD_COUNT (MPU6050_FALL_HOLD_MS / MPU6050_TASK_PERIOD_MS)
#define MPU6050_FALL_RECOVER_COUNT (MPU6050_FALL_RECOVER_MS / MPU6050_TASK_PERIOD_MS)
#define MPU6050_COLLISION_AVM_THRESHOLD 30000U
#define MPU6050_COLLISION_DELTA_THRESHOLD 14000U
#define MPU6050_COLLISION_GVM_THRESHOLD 22000U
#define MPU6050_COLLISION_HOLD_MS 5000U
#define MPU6050_COLLISION_HOLD_COUNT (MPU6050_COLLISION_HOLD_MS / MPU6050_TASK_PERIOD_MS)

/* 姿态数据 */
float pitch, roll, yaw;

/* 传感器原始值 */
short aacx, aacy, aacz;
short gyrox, gyroy, gyroz;

/* 向量模值 */
uint16_t AVM;
uint16_t GVM;

/* 跌倒标志 */
bool fall_flag = 0;
bool collision_flag = 0;

typedef enum {
    MPU6050_FALL_STATE_IDLE = 0,
    MPU6050_FALL_STATE_LOW_G,
    MPU6050_FALL_STATE_IMPACT,
    MPU6050_FALL_STATE_CONFIRMED
} mpu6050_fall_state_t;

static uint8_t mpu6050_configured = 0;
static mpu6050_fall_state_t fall_state = MPU6050_FALL_STATE_IDLE;
static uint16_t fall_confirm_count = 0U;
static uint16_t fall_recover_count = 0U;
static uint16_t fall_window_count = 0U;
static uint16_t fall_hold_count = 0U;
static uint16_t collision_hold_count = 0U;
static uint16_t last_avm = 0U;
static uint16_t avm_delta = 0U;
static uint8_t avm_baseline_valid = 0U;

/* Mahony 滤波器状态：姿态四元数 + 积分误差项 */
static float mahony_q0 = 1.0f, mahony_q1 = 0.0f, mahony_q2 = 0.0f, mahony_q3 = 0.0f;
static float mahony_ex_int = 0.0f, mahony_ey_int = 0.0f, mahony_ez_int = 0.0f;

/* 陀螺静止零偏（raw LSB），解算前从原始值扣除 */
static float gyro_bias_x = 0.0f, gyro_bias_y = 0.0f, gyro_bias_z = 0.0f;
static uint8_t gyro_bias_valid = 0U;

/* 运行期静止检测窗口：连续样本的计数、累加和与峰峰值 */
static uint16_t still_count = 0U;
static int32_t still_sum_x = 0, still_sum_y = 0, still_sum_z = 0;
static short still_min_x = 0, still_min_y = 0, still_min_z = 0;
static short still_max_x = 0, still_max_y = 0, still_max_z = 0;

/**
 * @brief  清空运行期陀螺静止检测窗口
 */
static void mpu6050_reset_gyro_still_window(void)
{
    still_count = 0U;
    still_sum_x = 0;
    still_sum_y = 0;
    still_sum_z = 0;
    still_min_x = 0;
    still_min_y = 0;
    still_min_z = 0;
    still_max_x = 0;
    still_max_y = 0;
    still_max_z = 0;
}

/**
 * @brief  平方根倒数，用于向量归一化
 */
static float mpu6050_inv_sqrt(float x)
{
    return 1.0f / sqrtf(x);
}

/**
 * @brief  复位 Mahony 滤波器为初始姿态
 */
static void mpu6050_mahony_reset(void)
{
    mahony_q0 = 1.0f;
    mahony_q1 = 0.0f;
    mahony_q2 = 0.0f;
    mahony_q3 = 0.0f;
    mahony_ex_int = 0.0f;
    mahony_ey_int = 0.0f;
    mahony_ez_int = 0.0f;
}

static void mpu6050_clear_result(void)
{
    pitch = 0.0f;
    roll = 0.0f;
    yaw = 0.0f;
    aacx = 0;
    aacy = 0;
    aacz = 0;
    gyrox = 0;
    gyroy = 0;
    gyroz = 0;
    AVM = 0;
    GVM = 0;
    fall_flag = 0;
    collision_flag = 0;
    fall_state = MPU6050_FALL_STATE_IDLE;
    fall_confirm_count = 0U;
    fall_recover_count = 0U;
    fall_window_count = 0U;
    fall_hold_count = 0U;
    collision_hold_count = 0U;
    last_avm = 0U;
    avm_delta = 0U;
    avm_baseline_valid = 0U;
    gyro_bias_x = 0.0f;
    gyro_bias_y = 0.0f;
    gyro_bias_z = 0.0f;
    gyro_bias_valid = 0U;
    mpu6050_mahony_reset();
    mpu6050_reset_gyro_still_window();
}

/**
 * @brief  计算三轴原始值向量模，避免使用 pow 带来的浮点开销
 */
static uint16_t mpu6050_vector_magnitude(short x, short y, short z)
{
    int64_t lx = x;
    int64_t ly = y;
    int64_t lz = z;
    uint64_t sum = ((uint64_t)(lx * lx) +
                    (uint64_t)(ly * ly) +
                    (uint64_t)(lz * lz));

    if (sum == 0ULL) {
        return 0U;
    }
    return (uint16_t)sqrt((double)sum);
}

/**
 * @brief  判断当前是否处于倒地后的稳定姿态
 */
static uint8_t mpu6050_is_fall_posture_stable(void)
{
    uint8_t tilted = ((fabs(pitch) >= MPU6050_FALL_ANGLE_DEG) ||
                      (fabs(roll) >= MPU6050_FALL_ANGLE_DEG)) ? 1U : 0U;
    uint8_t stable = (GVM <= MPU6050_FALL_STABLE_GVM_THRESHOLD) ? 1U : 0U;

    return ((tilted != 0U) && (stable != 0U)) ? 1U : 0U;
}

/**
 * @brief  重置跌倒检测临时状态，保留已确认报警
 */
static void mpu6050_reset_fall_candidate(void)
{
    fall_state = fall_flag ? MPU6050_FALL_STATE_CONFIRMED : MPU6050_FALL_STATE_IDLE;
    fall_confirm_count = 0U;
    fall_window_count = 0U;
}

/**
 * @brief  更新倒地报警状态，按低重力/冲击/稳定姿态三阶段确认
 */
static void mpu6050_update_fall_alarm(void)
{
    uint8_t impact = ((AVM >= MPU6050_FALL_IMPACT_AVM_THRESHOLD) ||
                      (avm_delta >= MPU6050_FALL_IMPACT_DELTA_THRESHOLD)) ? 1U : 0U;
    uint8_t stable_fall = mpu6050_is_fall_posture_stable();

    if (fall_flag) {
        if (fall_hold_count < MPU6050_FALL_HOLD_COUNT) {
            fall_hold_count++;
            return;
        }
        if (stable_fall != 0U) {
            fall_recover_count = 0U;
            return;
        }
        if (fall_recover_count < MPU6050_FALL_RECOVER_COUNT) {
            fall_recover_count++;
        }
        if (fall_recover_count >= MPU6050_FALL_RECOVER_COUNT) {
            fall_flag = false;
            fall_recover_count = 0U;
            fall_hold_count = 0U;
            mpu6050_reset_fall_candidate();
        }
        return;
    }

    switch (fall_state) {
        case MPU6050_FALL_STATE_IDLE:
            if (AVM <= MPU6050_FALL_LOW_AVM_THRESHOLD) {
                fall_state = MPU6050_FALL_STATE_LOW_G;
                fall_window_count = MPU6050_FALL_LOW_WINDOW_COUNT;
            } else if (impact != 0U) {
                fall_state = MPU6050_FALL_STATE_IMPACT;
                fall_window_count = MPU6050_FALL_IMPACT_WINDOW_COUNT;
            }
            break;

        case MPU6050_FALL_STATE_LOW_G:
            if (impact != 0U) {
                fall_state = MPU6050_FALL_STATE_IMPACT;
                fall_window_count = MPU6050_FALL_IMPACT_WINDOW_COUNT;
            } else if (fall_window_count > 0U) {
                fall_window_count--;
            } else {
                mpu6050_reset_fall_candidate();
            }
            break;

        case MPU6050_FALL_STATE_IMPACT:
            if (stable_fall != 0U) {
                if (fall_confirm_count < MPU6050_FALL_CONFIRM_COUNT) {
                    fall_confirm_count++;
                }
                if (fall_confirm_count >= MPU6050_FALL_CONFIRM_COUNT) {
                    fall_flag = true;
                    fall_state = MPU6050_FALL_STATE_CONFIRMED;
                    fall_hold_count = 0U;
                    fall_recover_count = 0U;
                }
            } else {
                fall_confirm_count = 0U;
                if (fall_window_count > 0U) {
                    fall_window_count--;
                } else {
                    mpu6050_reset_fall_candidate();
                }
            }
            break;

        case MPU6050_FALL_STATE_CONFIRMED:
        default:
            fall_state = MPU6050_FALL_STATE_CONFIRMED;
            break;
    }
}

/**
 * @brief  更新碰撞报警状态，尖峰触发后保持一段时间便于显示和上传
 */
static void mpu6050_update_collision_alarm(void)
{
    uint8_t collision = (((AVM >= MPU6050_COLLISION_AVM_THRESHOLD) &&
                          (avm_delta >= MPU6050_COLLISION_DELTA_THRESHOLD)) ||
                         (GVM >= MPU6050_COLLISION_GVM_THRESHOLD)) ? 1U : 0U;

    if (collision != 0U) {
        collision_flag = true;
        collision_hold_count = MPU6050_COLLISION_HOLD_COUNT;
        return;
    }

    if (collision_hold_count > 0U) {
        collision_hold_count--;
        collision_flag = true;
        return;
    }

    collision_flag = false;
}

/**
 * @brief  向 MPU6050 单个寄存器写入一个字节
 * @param  reg 寄存器地址
 * @param  val 写入值
 * @retval 0 成功，1 I2C 失败
 */
static uint8_t mpu6050_write_reg(uint8_t reg, uint8_t val)
{
    if (HAL_I2C_Mem_Write(&hi2c1, (MPU6050_ADDR << 1), reg,
                          I2C_MEMADD_SIZE_8BIT, &val, 1, MPU6050_I2C_TIMEOUT_MS) != HAL_OK)
        return 1;
    return 0;
}

/**
 * @brief  上电静止采样陀螺零偏，供姿态解算扣除
 * @note   加速度无法修正 yaw，零偏必须在源头扣除；采样期间检测到运动
 *         （峰峰值超限）或读取失败则放弃本次校准
 * @retval 1 校准成功，0 校准失败
 */
static uint8_t mpu6050_calibrate_gyro_bias(void)
{
    int32_t sum_x = 0, sum_y = 0, sum_z = 0;
    short min_x = 32767, min_y = 32767, min_z = 32767;
    short max_x = -32768, max_y = -32768, max_z = -32768;
    short gx, gy, gz;
    uint16_t i;

    /* 丢弃复位/唤醒后的瞬态样本，否则会污染零偏与静止判定 */
    for (i = 0U; i < MPU6050_GYRO_CAL_DISCARD; i++) {
        if (mpu6050_get_gyroscope(&gx, &gy, &gz))
            return 0U;
        HAL_Delay(MPU6050_GYRO_CAL_INTERVAL_MS);
    }

    for (i = 0U; i < MPU6050_GYRO_CAL_SAMPLES; i++) {
        if (mpu6050_get_gyroscope(&gx, &gy, &gz))
            return 0U;
        sum_x += gx;
        sum_y += gy;
        sum_z += gz;
        if (gx < min_x) min_x = gx;
        if (gx > max_x) max_x = gx;
        if (gy < min_y) min_y = gy;
        if (gy > max_y) max_y = gy;
        if (gz < min_z) min_z = gz;
        if (gz > max_z) max_z = gz;
        HAL_Delay(MPU6050_GYRO_CAL_INTERVAL_MS);
    }

    if (((max_x - min_x) > MPU6050_GYRO_CAL_MAX_SPREAD) ||
        ((max_y - min_y) > MPU6050_GYRO_CAL_MAX_SPREAD) ||
        ((max_z - min_z) > MPU6050_GYRO_CAL_MAX_SPREAD))
        return 0U;

    gyro_bias_x = (float)sum_x / (float)MPU6050_GYRO_CAL_SAMPLES;
    gyro_bias_y = (float)sum_y / (float)MPU6050_GYRO_CAL_SAMPLES;
    gyro_bias_z = (float)sum_z / (float)MPU6050_GYRO_CAL_SAMPLES;
    return 1U;
}

/**
 * @brief  MPU6050 初始化：软复位后唤醒并配置量程、采样率、DLPF
 * @param  无
 * @retval 无
 * @note   任一步 I2C 失败静默返回，不阻塞其他外设启动；上电零偏校准失败时
 *         保持未就绪，由周期任务在连续静止窗口内恢复
 */
void mpu6050_init(void)
{
    mpu6050_configured = 0;
    mpu6050_clear_result();

    /* 软复位到已知状态，复位期间寄存器不可访问需等待 */
    if (mpu6050_write_reg(MPU6050_PWR_MGMT_1_REG, MPU6050_PWR_RESET))
        return;
    HAL_Delay(MPU6050_RESET_DELAY_MS);

    /* 唤醒（复位后默认睡眠）并选 X 轴陀螺 PLL 时钟，比内部 RC 更稳 */
    if (mpu6050_write_reg(MPU6050_PWR_MGMT_1_REG, MPU6050_CLKSEL_PLL_XGYRO))
        return;
    if (mpu6050_write_reg(MPU6050_SMPLRT_DIV_REG, MPU6050_SMPLRT_DIV_125HZ))
        return;
    if (mpu6050_write_reg(MPU6050_CONFIG_REG, MPU6050_DLPF_42HZ))
        return;
    if (mpu6050_write_reg(MPU6050_GYRO_CONFIG_REG, MPU6050_GYRO_FS_2000DPS))
        return;
    if (mpu6050_write_reg(MPU6050_ACCEL_CONFIG_REG, MPU6050_ACCEL_FS_2G))
        return;

    gyro_bias_valid = mpu6050_calibrate_gyro_bias();

    mpu6050_configured = 1U;
}

/**
 * @brief  获取 MPU6050 是否完成初始化并可用
 * @param  无
 * @retval 1 可用，0 不可用
 */
uint8_t mpu6050_is_ready(void)
{
    return ((mpu6050_configured != 0U) && (gyro_bias_valid != 0U)) ? 1U : 0U;
}

/**
 * @brief  获取跌倒报警状态
 * @param  无
 * @retval 1 已报警，0 未报警
 */
uint8_t mpu6050_is_fall_alarm(void)
{
    return fall_flag ? 1U : 0U;
}

/**
 * @brief  获取激烈碰撞报警状态
 * @param  无
 * @retval 1 已报警，0 未报警
 */
uint8_t mpu6050_is_collision_alarm(void)
{
    return collision_flag ? 1U : 0U;
}

/**
 * @brief  获取 MPU6050 报警位图
 * @param  无
 * @retval bit0=跌倒，bit1=碰撞
 */
uint8_t mpu6050_get_alarm_flags(void)
{
    uint8_t flags = 0U;

    if (fall_flag) {
        flags |= 0x01U;
    }
    if (collision_flag) {
        flags |= 0x02U;
    }
    return flags;
}

/**
 * @brief  清除已锁存的 MPU6050 报警状态
 * @param  无
 * @retval 无
 */
void mpu6050_clear_alarm(void)
{
    fall_flag = false;
    collision_flag = false;
    fall_state = MPU6050_FALL_STATE_IDLE;
    fall_confirm_count = 0U;
    fall_recover_count = 0U;
    fall_window_count = 0U;
    fall_hold_count = 0U;
    collision_hold_count = 0U;
}

/**
 * @brief  运行期静止自校准：连续 2s 原始值峰峰值稳定时更新零偏
 * @note   零偏无效时不依赖当前零偏判静止，避免上电校准失败后无法自愈；
 *         已有有效零偏时只接受约 3dps 内的变化，避免吸收明显旋转
 */
static void mpu6050_gyro_bias_autocal(void)
{
    float candidate_x;
    float candidate_y;
    float candidate_z;
    uint8_t bias_was_valid;

    if (still_count == 0U) {
        still_min_x = gyrox;
        still_min_y = gyroy;
        still_min_z = gyroz;
        still_max_x = gyrox;
        still_max_y = gyroy;
        still_max_z = gyroz;
    } else {
        if (gyrox < still_min_x) still_min_x = gyrox;
        if (gyrox > still_max_x) still_max_x = gyrox;
        if (gyroy < still_min_y) still_min_y = gyroy;
        if (gyroy > still_max_y) still_max_y = gyroy;
        if (gyroz < still_min_z) still_min_z = gyroz;
        if (gyroz > still_max_z) still_max_z = gyroz;
    }

    still_sum_x += gyrox;
    still_sum_y += gyroy;
    still_sum_z += gyroz;
    still_count++;

    if (((still_max_x - still_min_x) > MPU6050_GYRO_CAL_MAX_SPREAD) ||
        ((still_max_y - still_min_y) > MPU6050_GYRO_CAL_MAX_SPREAD) ||
        ((still_max_z - still_min_z) > MPU6050_GYRO_CAL_MAX_SPREAD)) {
        mpu6050_reset_gyro_still_window();
        return;
    }

    if (still_count < MPU6050_GYRO_RECAL_SAMPLES)
        return;

    candidate_x = (float)still_sum_x / (float)still_count;
    candidate_y = (float)still_sum_y / (float)still_count;
    candidate_z = (float)still_sum_z / (float)still_count;

    if ((gyro_bias_valid != 0U) &&
        ((fabsf(candidate_x - gyro_bias_x) > (float)MPU6050_GYRO_STILL_THRESHOLD) ||
         (fabsf(candidate_y - gyro_bias_y) > (float)MPU6050_GYRO_STILL_THRESHOLD) ||
         (fabsf(candidate_z - gyro_bias_z) > (float)MPU6050_GYRO_STILL_THRESHOLD))) {
        mpu6050_reset_gyro_still_window();
        return;
    }

    bias_was_valid = gyro_bias_valid;
    gyro_bias_x = candidate_x;
    gyro_bias_y = candidate_y;
    gyro_bias_z = candidate_z;
    gyro_bias_valid = 1U;
    mpu6050_reset_gyro_still_window();

    if (bias_was_valid == 0U)
        mpu6050_mahony_reset();
}

/**
 * @brief  Mahony 姿态解算：以固定 10ms 步长融合原始六轴，更新 pitch/roll/yaw
 * @note   加速度提供重力参考修正陀螺漂移；纯 6 轴 yaw 无绝对参考会缓慢漂移，
 *         仅用于遥测显示，报警逻辑不依赖 yaw
 */
static void mpu6050_mahony_update(void)
{
    float gx = (((float)gyrox - gyro_bias_x) / MPU6050_GYRO_SENS_2000DPS) * MPU6050_DEG_TO_RAD;
    float gy = (((float)gyroy - gyro_bias_y) / MPU6050_GYRO_SENS_2000DPS) * MPU6050_DEG_TO_RAD;
    float gz = (((float)gyroz - gyro_bias_z) / MPU6050_GYRO_SENS_2000DPS) * MPU6050_DEG_TO_RAD;
    float ax = (float)aacx;
    float ay = (float)aacy;
    float az = (float)aacz;
    float recip_norm;
    float qa, qb, qc;

    /* 加速度模值有效时才做重力修正，自由落体/无数据时仅积分陀螺 */
    if ((ax != 0.0f) || (ay != 0.0f) || (az != 0.0f)) {
        float vx, vy, vz;
        float ex, ey, ez;

        recip_norm = mpu6050_inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recip_norm;
        ay *= recip_norm;
        az *= recip_norm;

        /* 四元数推算的重力方向 */
        vx = 2.0f * (mahony_q1 * mahony_q3 - mahony_q0 * mahony_q2);
        vy = 2.0f * (mahony_q0 * mahony_q1 + mahony_q2 * mahony_q3);
        vz = mahony_q0 * mahony_q0 - mahony_q1 * mahony_q1 -
             mahony_q2 * mahony_q2 + mahony_q3 * mahony_q3;

        /* 实测重力与推算重力的叉积作为姿态误差 */
        ex = ay * vz - az * vy;
        ey = az * vx - ax * vz;
        ez = ax * vy - ay * vx;

        mahony_ex_int += MPU6050_MAHONY_KI * ex * MPU6050_MAHONY_DT;
        mahony_ey_int += MPU6050_MAHONY_KI * ey * MPU6050_MAHONY_DT;
        mahony_ez_int += MPU6050_MAHONY_KI * ez * MPU6050_MAHONY_DT;

        gx += MPU6050_MAHONY_KP * ex + mahony_ex_int;
        gy += MPU6050_MAHONY_KP * ey + mahony_ey_int;
        gz += MPU6050_MAHONY_KP * ez + mahony_ez_int;
    }

    /* 一阶积分四元数微分方程 */
    gx *= 0.5f * MPU6050_MAHONY_DT;
    gy *= 0.5f * MPU6050_MAHONY_DT;
    gz *= 0.5f * MPU6050_MAHONY_DT;
    qa = mahony_q0;
    qb = mahony_q1;
    qc = mahony_q2;
    mahony_q0 += (-qb * gx - qc * gy - mahony_q3 * gz);
    mahony_q1 += (qa * gx + qc * gz - mahony_q3 * gy);
    mahony_q2 += (qa * gy - qb * gz + mahony_q3 * gx);
    mahony_q3 += (qa * gz + qb * gy - qc * gx);

    recip_norm = mpu6050_inv_sqrt(mahony_q0 * mahony_q0 + mahony_q1 * mahony_q1 +
                                  mahony_q2 * mahony_q2 + mahony_q3 * mahony_q3);
    mahony_q0 *= recip_norm;
    mahony_q1 *= recip_norm;
    mahony_q2 *= recip_norm;
    mahony_q3 *= recip_norm;

    /* 四元数转欧拉角，公式与原 DMP 输出约定一致 */
    pitch = asinf(-2.0f * mahony_q1 * mahony_q3 + 2.0f * mahony_q0 * mahony_q2) *
            MPU6050_RAD_TO_DEG;
    roll = atan2f(2.0f * mahony_q2 * mahony_q3 + 2.0f * mahony_q0 * mahony_q1,
                  -2.0f * mahony_q1 * mahony_q1 - 2.0f * mahony_q2 * mahony_q2 + 1.0f) *
           MPU6050_RAD_TO_DEG;
    yaw = atan2f(2.0f * (mahony_q1 * mahony_q2 + mahony_q0 * mahony_q3),
                 mahony_q0 * mahony_q0 + mahony_q1 * mahony_q1 -
                 mahony_q2 * mahony_q2 - mahony_q3 * mahony_q3) *
          MPU6050_RAD_TO_DEG;
}

/**
 * @brief  获取陀螺仪原始数据
 */
uint8_t mpu6050_get_gyroscope(short *gx, short *gy, short *gz)
{
    uint8_t buf[6];
    if (HAL_I2C_Mem_Read(&hi2c1, (MPU6050_ADDR << 1), MPU6050_GYRO_XOUTH_REG,
                         I2C_MEMADD_SIZE_8BIT, buf, 6, MPU6050_I2C_TIMEOUT_MS) != HAL_OK)
        return 1;
    *gx = (buf[0] << 8) | buf[1];
    *gy = (buf[2] << 8) | buf[3];
    *gz = (buf[4] << 8) | buf[5];
    return 0;
}

/**
 * @brief  获取加速度计原始数据
 */
uint8_t mpu6050_get_accelerometer(short *ax, short *ay, short *az)
{
    uint8_t buf[6];
    if (HAL_I2C_Mem_Read(&hi2c1, (MPU6050_ADDR << 1), MPU6050_ACCEL_XOUTH_REG,
                         I2C_MEMADD_SIZE_8BIT, buf, 6, MPU6050_I2C_TIMEOUT_MS) != HAL_OK)
        return 1;
    *ax = (buf[0] << 8) | buf[1];
    *ay = (buf[2] << 8) | buf[3];
    *az = (buf[4] << 8) | buf[5];
    return 0;
}

/**
 * @brief  MPU6050 调度器周期任务
 */
void mpu6050_task(void)
{
    if (!mpu6050_configured)
        return;

    if (mpu6050_get_accelerometer(&aacx, &aacy, &aacz)) {
        mpu6050_configured = 0;
        mpu6050_clear_result();
        return;
    }
    if (mpu6050_get_gyroscope(&gyrox, &gyroy, &gyroz)) {
        mpu6050_configured = 0;
        mpu6050_clear_result();
        return;
    }

    mpu6050_gyro_bias_autocal();
    if (gyro_bias_valid == 0U)
        return;

    mpu6050_mahony_update();

    AVM = mpu6050_vector_magnitude(aacx, aacy, aacz);
    GVM = mpu6050_vector_magnitude(gyrox, gyroy, gyroz);
    if (avm_baseline_valid == 0U) {
        last_avm = AVM;
        avm_delta = 0U;
        avm_baseline_valid = 1U;
        return;
    }
    avm_delta = (AVM >= last_avm) ? (uint16_t)(AVM - last_avm) : (uint16_t)(last_avm - AVM);
    last_avm = AVM;

    mpu6050_update_collision_alarm();
    mpu6050_update_fall_alarm();
}
