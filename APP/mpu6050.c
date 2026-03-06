#include "mpu6050.h"

uint8_t i = 10;                     // 循环计数器
float pitch, roll, yaw;             // 欧拉角（姿态数据）
short aacx, aacy, aacz;             // 加速度计原始数据
short gyrox, gyroy, gyroz;          // 陀螺仪原始数据
unsigned long walk;                 // 步数
float steplength = 0.3, Distance;   // 步距和路程计算参数
uint8_t svm_set = 1;                // 路程计算标志
uint16_t AVM;                       // 加速度向量模值
uint16_t GVM;                       // 陀螺仪向量模值
bool fall_flag = 0;                 // 跌倒标志位
bool collision_flag = 0;            // 碰撞标志位

/**
 * @brief  I2C 写操作函数，用于向 MPU6050 写入多个字节数据
 */
uint8_t MPU_Write_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    uint8_t data[1 + len]; // 组合数据缓冲区，第一个字节为寄存器地址
    data[0] = reg;
    for (uint8_t i = 0; i < len; i++)
        data[i + 1] = buf[i];

    // 通过 I2C 发送数据
    if (HAL_I2C_Master_Transmit(&hi2c1, (addr << 1), data, len + 1, HAL_MAX_DELAY) != HAL_OK)
        return 1; // 传输失败
    return 0;     // 传输成功
}

/**
 * @brief  I2C 读操作函数，用于从 MPU6050 读取多个字节数据
 */
uint8_t MPU_Read_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    // 发送寄存器地址
    if (HAL_I2C_Master_Transmit(&hi2c1, (addr << 1), &reg, 1, HAL_MAX_DELAY) != HAL_OK)
        return 1; // 发送寄存器地址失败

    // 读取数据
    if (HAL_I2C_Master_Receive(&hi2c1, (addr << 1), buf, len, HAL_MAX_DELAY) != HAL_OK)
        return 1; // 读取数据失败
    return 0;     // 读取成功
}

/**
 * @brief  向 MPU6050 写入单个字节数据
 */
uint8_t MPU_Write_Byte(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    if (HAL_I2C_Master_Transmit(&hi2c1, (MPU_ADDR << 1), buf, 2, HAL_MAX_DELAY) != HAL_OK)
    {
        return 1; // 传输失败
    }
    return 0; // 传输成功
}

/**
 * @brief  从 MPU6050 读取单个字节数据
 */
uint8_t MPU_Read_Byte(uint8_t reg)
{
    uint8_t data;
    // 发送寄存器地址
    if (HAL_I2C_Master_Transmit(&hi2c1, (MPU_ADDR << 1), &reg, 1, HAL_MAX_DELAY) != HAL_OK)
    {
        return 0; // 传输失败
    }

    // 读取数据
    if (HAL_I2C_Master_Receive(&hi2c1, (MPU_ADDR << 1), &data, 1, HAL_MAX_DELAY) != HAL_OK)
    {
        return 0; // 读取失败
    }
    return data; // 返回读取的数据
}

/**
 * @brief  初始化 MPU6050 函数
 */
void MPU_Init(void)
{
    // 解除 MPU6050 休眠状态
    MPU_Write_Byte(MPU_PWR_MGMT1_REG, 0x00);
    // 设置陀螺仪采样率（典型值 125Hz）
    MPU_Write_Byte(MPU_SAMPLE_RATE_REG, 0x07);
    // 设置低通滤波器频率（典型值 5Hz）
    MPU_Write_Byte(MPU_CFG_REG, 0x06);
    // 配置陀螺仪（不自检，量程 2000deg/s）
    MPU_Write_Byte(MPU_GYRO_CFG_REG, 0x18);
    // 配置加速度计（不自检，量程 2G，低通滤波 5Hz）
    MPU_Write_Byte(MPU_ACCEL_CFG_REG, 0x01);
}

/**
 * @brief  获取温度值函数
 */
short MPU_Get_Temperature(void)
{
    uint8_t buf[2];
    short raw;
    float temp;
    // 读取温度寄存器的高低 8 位
    MPU_Read_Len(MPU_ADDR, MPU_TEMP_OUTH_REG, 2, buf);
    // 组合 16 位原始数据
    raw = ((uint16_t)buf[0] << 8) | buf[1];
    // 转换为实际温度值（扩大 100 倍返回）
    temp = 36.53 + ((double)raw) / 340;
    return temp * 100;
}

/**
 * @brief  获取陀螺仪原始数据函数
 */
uint8_t MPU_Get_Gyroscope(short *gx, short *gy, short *gz)
{
    uint8_t buf[6], res;
    res = MPU_Read_Len(MPU_ADDR, MPU_GYRO_XOUTH_REG, 6, buf);
    if (res == 0)
    {
        // 组合各轴的高低 8 位数据
        *gx = ((uint16_t)buf[0] << 8) | buf[1];
        *gy = ((uint16_t)buf[2] << 8) | buf[3];
        *gz = ((uint16_t)buf[4] << 8) | buf[5];
    }
    return res;
}

/**
 * @brief  获取加速度计原始数据函数
 */
uint8_t MPU_Get_Accelerometer(short *ax, short *ay, short *az)
{
    uint8_t buf[6], res;
    res = MPU_Read_Len(MPU_ADDR, MPU_ACCEL_XOUTH_REG, 6, buf);
    if (res == 0)
    {
        // 组合各轴的高低 8 位数据
        *ax = ((uint16_t)buf[0] << 8) | buf[1];
        *ay = ((uint16_t)buf[2] << 8) | buf[3];
        *az = ((uint16_t)buf[4] << 8) | buf[5];
    }
    return res;
}

/**
 * @brief  MPU6050 任务处理函数
 * 
 * @param  无
 * @return 无
 */
void mpu6050_task(void)
{
    // 获取姿态数据（欧拉角）
    mpu_dmp_get_data(&pitch, &roll, &yaw);
    // 获取加速度计数据
    MPU_Get_Accelerometer(&aacx, &aacy, &aacz);
    // 获取陀螺仪数据
    MPU_Get_Gyroscope(&gyrox, &gyroy, &gyroz);
    
    // 计算加速度向量模值
    AVM = sqrt(pow(aacx, 2) + pow(aacy, 2) + pow(aacz, 2));
    // 计算陀螺仪向量模值
    GVM = sqrt(pow(gyrox, 2) + pow(gyroy, 2) + pow(gyroz, 2));
    
    // 判断是否跌倒（ pitch 或 roll 超过 60 度）
    fall_flag = ((fabs(pitch) > 60) | (fabs(roll) > 60));
    
    // 打印姿态数据
    printf("pitch:%0.1f   roll:%0.1f   yaw:%0.1f\r\n", pitch, roll, yaw);
}    
