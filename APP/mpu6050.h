#ifndef MPU6050_H
#define MPU6050_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MPU6050 I2C 地址（AD0 接地） */
#define MPU6050_ADDR            0x68

/* 数据读取寄存器（InvenSense 驱动内部处理其余寄存器） */
#define MPU6050_ACCEL_XOUTH_REG 0x3B
#define MPU6050_GYRO_XOUTH_REG  0x43

/* 初始化（含 DMP 固件加载、自检、校准） */
void mpu6050_init(void);

/* 调度器周期任务（10ms） */
void mpu6050_task(void);

/* DMP 姿态解算 */
uint8_t mpu6050_dmp_get_data(float *pitch, float *roll, float *yaw);

/* 原始数据读取 */
uint8_t mpu6050_get_gyroscope(short *gx, short *gy, short *gz);
uint8_t mpu6050_get_accelerometer(short *ax, short *ay, short *az);

/* 姿态数据 */
extern float pitch, roll, yaw;

/* 传感器原始值 */
extern short aacx, aacy, aacz;
extern short gyrox, gyroy, gyroz;

/* 向量模值 */
extern uint16_t AVM;
extern uint16_t GVM;

/* 报警标志 */
extern bool fall_flag;
extern bool collision_flag;

uint8_t mpu6050_is_ready(void);             // 获取传感器可用状态
uint8_t mpu6050_is_fall_alarm(void);        // 获取跌倒报警状态
uint8_t mpu6050_is_collision_alarm(void);   // 获取激烈碰撞报警状态
uint8_t mpu6050_get_alarm_flags(void);      // bit0=跌倒，bit1=碰撞
void mpu6050_clear_alarm(void);             // 清除已锁存报警

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_H */
