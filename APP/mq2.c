#include "mq2.h"


float adc_value = 0;    // ADC 原始值
float voltage = 0;      // 电压值
float RS = 0;           // 传感器电阻值
float RL = 4.7;         // 负载电阻值
float R0 = 35.904;      // 传感器在清洁空气中的电阻值（单位：千欧）
float ppm = 0;          // 丙烷浓度（ppm）

uint32_t dma_buff[30];  // DMA 缓冲区，用于存储 ADC 采样数据

/**
 * @brief MQ2 传感器任务函数(推荐，基于 DMA 的方法)
 *  
 * @param None
 * @retval None
 */
void mq2_task(void)
{
    // 清零 ADC 原始值
    adc_value = 0;

    // 累加 30 个采样点的 ADC 值
    for (uint8_t i = 0; i < 30; i++)
    {
        adc_value += (float)dma_buff[i];
    }

    // 计算平均电压值
    voltage = (float)(adc_value / 30.0f) / 4095 * 3.3f;

    // 计算传感器电阻值
    RS = ((5.0f - voltage) / voltage) * RL;
    
    // 计算丙烷浓度（ppm）
    ppm = pow((RS / (R0 * 11.5428)), -1.5278);

    // 打印调试信息
    printf("V:%.2f RS:%.2f ppm:%.2f\r\n", voltage, RS, ppm);
}


/**
 * @brief MQ2 传感器任务函数(常规读取方法，可以注释掉)
 * 
 * @param None
 * @retval None
 */
void mq2_task1(void)
{
    // 启动 ADC 转换
    HAL_ADC_Start(&hadc1);
    
    // 等待转换完成
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    
    // 检查转换是否完成
    if (HAL_IS_BIT_SET(HAL_ADC_GetState(&hadc1), HAL_ADC_STATE_REG_EOC))
    {
        // 获取 ADC 值
        adc_value = HAL_ADC_GetValue(&hadc1);
        
        // 计算电压值
        voltage = (float)adc_value / 4095 * 5.0f;
        
        // 打印电压值（调试用）
        printf("voltage:%f\r\n",voltage);
    }
}
