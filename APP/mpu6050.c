#include "mpu6050.h"
#include "mpu6050_inv_mpu.h"
#include "mpu6050_inv_mpu_dmp_motion_driver.h"

extern I2C_HandleTypeDef hi2c1;

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

static uint8_t mpu6050_ready = 0;
static mpu6050_fall_state_t fall_state = MPU6050_FALL_STATE_IDLE;
static uint16_t fall_confirm_count = 0U;
static uint16_t fall_recover_count = 0U;
static uint16_t fall_window_count = 0U;
static uint16_t fall_hold_count = 0U;
static uint16_t collision_hold_count = 0U;
static uint16_t last_avm = 0U;
static uint16_t avm_delta = 0U;
static uint8_t avm_baseline_valid = 0U;

/* q30 格式转 float 的除数 */
#define Q30 1073741824.0f

/* 陀螺仪安装方向矩阵 */
static signed char gyro_orientation[9] = {
    1, 0, 0,
    0, 1, 0,
    0, 0, 1
};

/**
 * @brief  行向量转 DMP 方向标量
 */
static unsigned short inv_row_2_scale(const signed char *row)
{
    unsigned short b;

    if (row[0] > 0)
        b = 0;
    else if (row[0] < 0)
        b = 4;
    else if (row[1] > 0)
        b = 1;
    else if (row[1] < 0)
        b = 5;
    else if (row[2] > 0)
        b = 2;
    else if (row[2] < 0)
        b = 6;
    else
        b = 7;
    return b;
}

/**
 * @brief  方向矩阵转 DMP 标量
 */
static unsigned short inv_orientation_matrix_to_scalar(const signed char *mtx)
{
    unsigned short scalar;

    scalar  = inv_row_2_scale(mtx);
    scalar |= inv_row_2_scale(mtx + 3) << 3;
    scalar |= inv_row_2_scale(mtx + 6) << 6;

    return scalar;
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
 * @brief  执行自检并将偏置写入 DMP
 */
static uint8_t run_self_test(void)
{
    int result;
    long gyro[3], accel[3];
    float sens;
    unsigned short accel_sens;

    result = mpu_run_self_test(gyro, accel);

    mpu_get_gyro_sens(&sens);
    gyro[0] = (long)(gyro[0] * sens);
    gyro[1] = (long)(gyro[1] * sens);
    gyro[2] = (long)(gyro[2] * sens);
    dmp_set_gyro_bias(gyro);

    mpu_get_accel_sens(&accel_sens);
    accel[0] *= accel_sens;
    accel[1] *= accel_sens;
    accel[2] *= accel_sens;
    dmp_set_accel_bias(accel);

    return (result == 0x3) ? 0 : 1;
}

/**
 * @brief  MPU6050 完整初始化（含 DMP 固件加载、自检、校准）
 */
void mpu6050_init(void)
{
    struct int_param_s int_param;

    mpu6050_ready = 0;
    mpu6050_clear_result();

    if (mpu_init(&int_param) != 0)
        return;
    if (mpu_set_sensors(INV_XYZ_GYRO | INV_XYZ_ACCEL))
        return;
    if (mpu_configure_fifo(INV_XYZ_GYRO | INV_XYZ_ACCEL))
        return;
    if (mpu_set_sample_rate(DEFAULT_MPU_HZ))
        return;
    if (dmp_load_motion_driver_firmware())
        return;
    if (dmp_set_orientation(inv_orientation_matrix_to_scalar(gyro_orientation)))
        return;
    if (dmp_enable_feature(DMP_FEATURE_6X_LP_QUAT | DMP_FEATURE_TAP |
                           DMP_FEATURE_ANDROID_ORIENT | DMP_FEATURE_SEND_RAW_ACCEL |
                           DMP_FEATURE_SEND_CAL_GYRO | DMP_FEATURE_GYRO_CAL))
        return;
    if (dmp_set_fifo_rate(DEFAULT_MPU_HZ))
        return;

    run_self_test(); // 自检（失败不阻塞，仅影响校准精度）

    if (mpu_set_dmp_state(1))
        return;

    mpu6050_ready = 1;
}

/**
 * @brief  获取 MPU6050 是否完成初始化并可用
 * @param  无
 * @retval 1 可用，0 不可用
 */
uint8_t mpu6050_is_ready(void)
{
    return mpu6050_ready;
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
 * @brief  DMP 姿态解算，从 FIFO 读取四元数并转换为欧拉角
 */
uint8_t mpu6050_dmp_get_data(float *p, float *r, float *y)
{
    float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
    unsigned long sensor_timestamp;
    short gyro[3], accel[3], sensors;
    unsigned char more;
    long quat[4];

    if (dmp_read_fifo(gyro, accel, quat, &sensor_timestamp, &sensors, &more))
        return 1;

    if (sensors & INV_WXYZ_QUAT) {
        q0 = quat[0] / Q30;
        q1 = quat[1] / Q30;
        q2 = quat[2] / Q30;
        q3 = quat[3] / Q30;

        *p = asin(-2 * q1 * q3 + 2 * q0 * q2) * 57.3f;
        *r = atan2(2 * q2 * q3 + 2 * q0 * q1, -2 * q1 * q1 - 2 * q2 * q2 + 1) * 57.3f;
        *y = atan2(2 * (q1 * q2 + q0 * q3), q0 * q0 + q1 * q1 - q2 * q2 - q3 * q3) * 57.3f;
    } else
        return 2;

    return 0;
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
    if (!mpu6050_ready)
        return;

    if (mpu6050_dmp_get_data(&pitch, &roll, &yaw))
        return;
    if (mpu6050_get_accelerometer(&aacx, &aacy, &aacz)) {
        mpu6050_ready = 0;
        mpu6050_clear_result();
        return;
    }
    if (mpu6050_get_gyroscope(&gyrox, &gyroy, &gyroz)) {
        mpu6050_ready = 0;
        mpu6050_clear_result();
        return;
    }

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
