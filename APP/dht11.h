#ifndef DHT11_H
#define DHT11_H

#include "bsp_system.h"

// 移植配置：修改以下三个宏即可适配不同引脚
#define DHT11_PORT            GPIOA
#define DHT11_PIN             GPIO_PIN_8
#define DHT11_GPIO_CLK_EN()   __HAL_RCC_GPIOA_CLK_ENABLE()

// 初始化 DHT11 函数，配置 GPIO 并检测传感器是否存在
uint8_t DHT11_Init(void);

// 读取 DHT11 数据函数，获取温度和湿度值
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi);

// 微秒级延时函数，用于精确控制信号时序
void Delay_us(uint16_t us);

// DHT11 任务函数，用于定期读取传感器数据并打印
void dht11_task(void);

#endif
