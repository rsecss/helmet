#include "mpu6050.h"
#include "mpu6050_inv_mpu.h"
#include "mpu6050_inv_mpu_dmp_motion_driver.h"

extern I2C_HandleTypeDef hi2c1;

#define MPU6050_I2C_TIMEOUT_MS 10U

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

static uint8_t mpu6050_ready = 0;

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

    AVM = sqrt(pow(aacx, 2) + pow(aacy, 2) + pow(aacz, 2));
    GVM = sqrt(pow(gyrox, 2) + pow(gyroy, 2) + pow(gyroz, 2));

    fall_flag = ((fabs(pitch) > 60) | (fabs(roll) > 60));

}
