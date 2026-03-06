#include "dht11.h"

/**
 * @brief   配置 DHT11 数据引脚为输入模式（上拉）
 */
static void DHT11_IO_IN(void)
{
    GPIO_InitTypeDef s = {0};
    s.Pin  = DHT11_PIN;
    s.Mode = GPIO_MODE_INPUT;
    s.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT11_PORT, &s);
}

/**
 * @brief   配置 DHT11 数据引脚为推挽输出模式
 */
static void DHT11_IO_OUT(void)
{
    GPIO_InitTypeDef s = {0};
    s.Pin   = DHT11_PIN;
    s.Mode  = GPIO_MODE_OUTPUT_PP;
    s.Pull  = GPIO_NOPULL;
    s.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &s);
}

/**
 * @brief   微秒级延时（基于 TIM1, 1MHz）
 * @param   us  延时微秒数
 */
void Delay_us(uint16_t us)
{
    uint16_t differ = 0xFFFF - us - 5;
    __HAL_TIM_SET_COUNTER(&htim1, differ);
    HAL_TIM_Base_Start(&htim1);
    while (differ < 0xFFFF - 5)
    {
        differ = __HAL_TIM_GET_COUNTER(&htim1);
    }
    HAL_TIM_Base_Stop(&htim1);
}

/**
 * @brief   读取温湿度数据
 * @param   temp  温度值输出指针
 * @param   humi  湿度值输出指针
 * @retval  0 成功，1 失败
 */
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi)
{
    uint8_t buf[5] = {0};
    uint8_t retry;

    // 复位：拉低 ≥18ms → 拉高 20~40us
    DHT11_IO_OUT();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    Delay_us(30);

    // 等待 DHT11 响应：低 40~80us → 高 40~80us
    DHT11_IO_IN();

    retry = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) && retry < 100)
    {
        retry++;
        Delay_us(1);
    }
    if (retry >= 100)
    {
        return 1;
    }

    retry = 0;
    while (!HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) && retry < 100)
    {
        retry++;
        Delay_us(1);
    }
    if (retry >= 100)
    {
        return 1;
    }

    // 读取 40 位数据（5 字节）
    for (uint8_t i = 0; i < 40; i++)
    {
        retry = 0;
        while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) && retry < 100)
        {
            retry++;
            Delay_us(1);
        }

        retry = 0;
        while (!HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) && retry < 100)
        {
            retry++;
            Delay_us(1);
        }

        Delay_us(40); // 40us 后采样判断 0/1
        buf[i / 8] <<= 1;
        if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN))
        {
            buf[i / 8] |= 1;
        }
    }

    // 校验和验证
    if ((buf[0] + buf[1] + buf[2] + buf[3]) != buf[4])
    {
        return 1;
    }

    *humi = buf[0];
    *temp = buf[2];
    return 0;
}

/**
 * @brief   初始化 DHT11，配置 GPIO 并探测传感器
 * @retval  0 成功，1 失败
 */
uint8_t DHT11_Init(void)
{
    uint8_t retry;

    DHT11_GPIO_CLK_EN();
    DHT11_IO_OUT();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);

    // 复位
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    Delay_us(30);

    // 检测响应
    DHT11_IO_IN();

    retry = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) && retry < 100)
    {
        retry++;
        Delay_us(1);
    }
    if (retry >= 100)
    {
        return 1;
    }

    retry = 0;
    while (!HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) && retry < 100)
    {
        retry++;
        Delay_us(1);
    }
    if (retry >= 100)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief   DHT11 调度器任务，周期读取并打印温湿度
 */
static uint8_t humi;
static uint8_t temp;

void dht11_task(void)
{
    DHT11_Read_Data(&temp, &humi);
    printf("temp:%d,humi:%d\r\n", temp, humi);
}
