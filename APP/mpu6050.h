#ifndef MPU6050_H
#define MPU6050_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// MPU6050 各种寄存器地址定义
#define MPU_SELF_TESTX_REG 0X0D   // X 轴自检寄存器
#define MPU_SELF_TESTY_REG 0X0E   // Y 轴自检寄存器
#define MPU_SELF_TESTZ_REG 0X0F   // Z 轴自检寄存器
#define MPU_SELF_TESTA_REG 0X10   // 加速度计自检寄存器
#define MPU_SAMPLE_RATE_REG 0X19  // 采样频率分频器寄存器
#define MPU_CFG_REG 0X1A          // 配置寄存器（低通滤波器）
#define MPU_GYRO_CFG_REG 0X1B     // 陀螺仪配置寄存器（量程等）
#define MPU_ACCEL_CFG_REG 0X1C    // 加速度计配置寄存器（量程等）
#define MPU_MOTION_DET_REG 0X1F   // 运动检测阀值设置寄存器
#define MPU_FIFO_EN_REG 0X23      // FIFO 使能寄存器
#define MPU_I2CMST_CTRL_REG 0X24  // I2C 主机控制寄存器
#define MPU_I2CSLV0_ADDR_REG 0X25 // I2C 从机 0 器件地址寄存器
#define MPU_I2CSLV0_REG 0X26      // I2C 从机 0 数据地址寄存器
#define MPU_I2CSLV0_CTRL_REG 0X27 // I2C 从机 0 控制寄存器
#define MPU_I2CSLV1_ADDR_REG 0X28 // I2C 从机 1 器件地址寄存器
#define MPU_I2CSLV1_REG 0X29      // I2C 从机 1 数据地址寄存器
#define MPU_I2CSLV1_CTRL_REG 0X2A // I2C 从机 1 控制寄存器
#define MPU_I2CSLV2_ADDR_REG 0X2B // I2C 从机 2 器件地址寄存器
#define MPU_I2CSLV2_REG 0X2C      // I2C 从机 2 数据地址寄存器
#define MPU_I2CSLV2_CTRL_REG 0X2D // I2C 从机 2 控制寄存器
#define MPU_I2CSLV3_ADDR_REG 0X2E // I2C 从机 3 器件地址寄存器
#define MPU_I2CSLV3_REG 0X2F      // I2C 从机 3 数据地址寄存器
#define MPU_I2CSLV3_CTRL_REG 0X30 // I2C 从机 3 控制寄存器
#define MPU_I2CSLV4_ADDR_REG 0X31 // I2C 从机 4 器件地址寄存器
#define MPU_I2CSLV4_REG 0X32      // I2C 从机 4 数据地址寄存器
#define MPU_I2CSLV4_DO_REG 0X33   // I2C 从机 4 写数据寄存器
#define MPU_I2CSLV4_CTRL_REG 0X34 // I2C 从机 4 控制寄存器
#define MPU_I2CSLV4_DI_REG 0X35   // I2C 从机 4 读数据寄存器

#define MPU_I2CMST_STA_REG 0X36 // I2C 主机状态寄存器
#define MPU_INTBP_CFG_REG 0X37  // 中断/旁路设置寄存器
#define MPU_INT_EN_REG 0X38     // 中断使能寄存器
#define MPU_INT_STA_REG 0X3A    // 中断状态寄存器

#define MPU_ACCEL_XOUTH_REG 0X3B // 加速度计 X 轴高 8 位寄存器
#define MPU_ACCEL_XOUTL_REG 0X3C // 加速度计 X 轴低 8 位寄存器
#define MPU_ACCEL_YOUTH_REG 0X3D // 加速度计 Y 轴高 8 位寄存器
#define MPU_ACCEL_YOUTL_REG 0X3E // 加速度计 Y 轴低 8 位寄存器
#define MPU_ACCEL_ZOUTH_REG 0X3F // 加速度计 Z 轴高 8 位寄存器
#define MPU_ACCEL_ZOUTL_REG 0X40 // 加速度计 Z 轴低 8 位寄存器

#define MPU_TEMP_OUTH_REG 0X41 // 温度传感器高 8 位寄存器
#define MPU_TEMP_OUTL_REG 0X42 // 温度传感器低 8 位寄存器

#define MPU_GYRO_XOUTH_REG 0X43 // 陀螺仪 X 轴高 8 位寄存器
#define MPU_GYRO_XOUTL_REG 0X44 // 陀螺仪 X 轴低 8 位寄存器
#define MPU_GYRO_YOUTH_REG 0X45 // 陀螺仪 Y 轴高 8 位寄存器
#define MPU_GYRO_YOUTL_REG 0X46 // 陀螺仪 Y 轴低 8 位寄存器
#define MPU_GYRO_ZOUTH_REG 0X47 // 陀螺仪 Z 轴高 8 位寄存器
#define MPU_GYRO_ZOUTL_REG 0X48 // 陀螺仪 Z 轴低 8 位寄存器

#define MPU_I2CSLV0_DO_REG 0X63 // I2C 从机 0 数据寄存器
#define MPU_I2CSLV1_DO_REG 0X64 // I2C 从机 1 数据寄存器
#define MPU_I2CSLV2_DO_REG 0X65 // I2C 从机 2 数据寄存器
#define MPU_I2CSLV3_DO_REG 0X66 // I2C 从机 3 数据寄存器

#define MPU_I2CMST_DELAY_REG 0X67 // I2C 主机延时管理寄存器
#define MPU_SIGPATH_RST_REG 0X68  // 信号通道复位寄存器
#define MPU_MDETECT_CTRL_REG 0X69 // 运动检测控制寄存器
#define MPU_USER_CTRL_REG 0X6A    // 用户控制寄存器
#define MPU_PWR_MGMT1_REG 0X6B    // 电源管理寄存器 1
#define MPU_PWR_MGMT2_REG 0X6C    // 电源管理寄存器 2
#define MPU_FIFO_CNTH_REG 0X72    // FIFO 计数寄存器高 8 位
#define MPU_FIFO_CNTL_REG 0X73    // FIFO 计数寄存器低 8 位
#define MPU_FIFO_RW_REG 0X74      // FIFO 读写寄存器
#define MPU_DEVICE_ID_REG 0X75    // 器件 ID 寄存器

// MPU6050 地址定义
#define MPU_ADDR 0X68 // 当 AD0 脚接地时的 I2C 地址


// 初始化 MPU6050 函数
void MPU_Init(void);

// I2C 连续写操作函数
uint8_t MPU_Write_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf);

// I2C 连续读操作函数
uint8_t MPU_Read_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf);

// 获取温度值函数
short MPU_Get_Temperature(void);

// 获取陀螺仪原始数据函数
uint8_t MPU_Get_Gyroscope(short *gx, short *gy, short *gz);

// 获取加速度计原始数据函数
uint8_t MPU_Get_Accelerometer(short *ax, short *ay, short *az);

// 步数获取函数
void dmp_getwalk(void);

// 振动计算函数
void dmp_svm(void);

// MPU6050 任务处理函数
void mpu6050_task(void);

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_H */
