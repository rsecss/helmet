#ifndef MAX30102_H
#define MAX30102_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MAX30102 I2C 7-bit 地址 */
#define MAX30102_ADDR           0x57

/* 核心寄存器 */
#define MAX30102_REG_INT_STATUS_1   0x00
#define MAX30102_REG_INT_STATUS_2   0x01
#define MAX30102_REG_INT_ENABLE_1   0x02
#define MAX30102_REG_INT_ENABLE_2   0x03
#define MAX30102_REG_FIFO_WR_PTR    0x04
#define MAX30102_REG_OVF_COUNTER    0x05
#define MAX30102_REG_FIFO_RD_PTR    0x06
#define MAX30102_REG_FIFO_DATA      0x07
#define MAX30102_REG_FIFO_CONFIG    0x08
#define MAX30102_REG_MODE_CONFIG    0x09
#define MAX30102_REG_SPO2_CONFIG    0x0A
#define MAX30102_REG_LED1_PA        0x0C   // Red
#define MAX30102_REG_LED2_PA        0x0D   // IR
#define MAX30102_REG_PART_ID        0xFF

/* 出厂 Part ID */
#define MAX30102_PART_ID_VALUE      0x15

/* 采样环形缓冲长度（4 秒 @ 100 Hz，兼顾频率分辨率与内存占用）*/
#define MAX30102_BUF_LEN            400

/* 滑窗步长：累计该数量新样本触发一次解算（约 0.5 s 更新频率）*/
#define MAX30102_CALC_STRIDE        50

/* 手指接触阈值（IR 直流，经验值）*/
#define MAX30102_FINGER_THRESHOLD   50000

/* 初始化：软复位 → 自检（Part ID） → 配置 FIFO/LED/SpO2 模式 */
uint8_t max30102_init(void);

/* 调度器周期任务（50ms）：读 FIFO → 样本入环形缓冲 → 滑窗解算 HR/SpO2 */
void max30102_task(void);

/* 心率（bpm）与血氧饱和度（%），无效时保持为 0 */
extern int32_t heart_rate;
extern int32_t spo2;

/* 有效性标志：1 表示数值可信（手指接触且解算成功）*/
extern uint8_t hr_valid;
extern uint8_t spo2_valid;

#ifdef __cplusplus
}
#endif

#endif /* MAX30102_H */
