#ifndef DHT11_H
#define DHT11_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// DHT11 数据引脚配置为输入模式
#define DHT11_IO_IN()  {GPIOA->CRH&=0XFFFFFFF0;GPIOA->CRH|=8;}
// DHT11 数据引脚配置为输出模式
#define DHT11_IO_OUT() {GPIOA->CRH&=0XFFFFFFF0;GPIOA->CRH|=3;}
// DHT11 数据引脚输出宏定义
#define DHT11_DQ_OUT PAout(8)
// DHT11 数据引脚输入宏定义
#define DHT11_DQ_IN  PAin(8) 

// 微秒级延时函数，用于精确控制信号时序
void Delay_us(uint16_t us);

// 读取 DHT11 数据函数，获取温度和湿度值
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi);

// 初始化 DHT11 函数，配置 GPIO 并检测传感器是否存在
uint8_t DHT11_Init(void);

// DHT11 任务函数，用于定期读取传感器数据并打印
void dht11_task(void);

#ifdef __cplusplus
}
#endif

#endif
