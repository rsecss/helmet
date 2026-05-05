#include "asrpro.h"
#include "helmet_alarm.h"
#include "pwm_motor.h"
#include "rgb_led.h"
#include "usart.h"

#define ASRPRO_RX_RING_SIZE                128U
#define ASRPRO_LINE_BUFFER_SIZE            48U
#define ASRPRO_MOTOR_GEAR_COUNT            4U

static const uint8_t asrpro_motor_gear_to_percent[ASRPRO_MOTOR_GEAR_COUNT] = {
    0U, 33U, 66U, 100U
};

static uint8_t asrpro_rx_byte = 0U;
static uint8_t asrpro_rx_ring[ASRPRO_RX_RING_SIZE];
static char asrpro_line_buffer[ASRPRO_LINE_BUFFER_SIZE];
static volatile uint16_t asrpro_rx_head = 0U;
static volatile uint16_t asrpro_rx_tail = 0U;
static volatile uint16_t asrpro_rx_count = 0U;
static uint16_t asrpro_line_len = 0U;
static uint8_t asrpro_line_overflow = 0U;

/**
 * @brief       清空 ASRPro 接收环形缓冲区
 * @param       无
 * @retval      无
 */
static void asrpro_ring_clear(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    asrpro_rx_head = 0U;
    asrpro_rx_tail = 0U;
    asrpro_rx_count = 0U;
    if (primask == 0U) {
        __enable_irq();
    }
}

/**
 * @brief       将 USART1 中断收到的字节写入环形缓冲区
 * @param       byte 接收字节
 * @retval      无
 */
static void asrpro_ring_write(uint8_t byte)
{
    if (asrpro_rx_count >= ASRPRO_RX_RING_SIZE) {
        return;
    }

    asrpro_rx_ring[asrpro_rx_head] = byte;
    asrpro_rx_head = (uint16_t)((asrpro_rx_head + 1U) % ASRPRO_RX_RING_SIZE);
    asrpro_rx_count++;
}

/**
 * @brief       从环形缓冲区读取一个字节
 * @param       out 输出字节指针
 * @retval      1 读到字节，0 缓冲区为空
 */
static uint8_t asrpro_ring_read_byte(uint8_t *out)
{
    uint32_t primask = __get_PRIMASK();

    if (out == NULL)
        return 0U;

    __disable_irq();
    if (asrpro_rx_count == 0U) {
        if (primask == 0U) {
            __enable_irq();
        }
        return 0U;
    }

    *out = asrpro_rx_ring[asrpro_rx_tail];
    asrpro_rx_tail = (uint16_t)((asrpro_rx_tail + 1U) % ASRPRO_RX_RING_SIZE);
    asrpro_rx_count--;
    if (primask == 0U) {
        __enable_irq();
    }

    return 1U;
}

/**
 * @brief       跳过命令行首尾 ASCII 空白
 * @param       text 原始命令行
 * @param       len 原始长度
 * @param       out_len 裁剪后长度
 * @retval      裁剪后命令起始地址
 */
static const char *asrpro_trim_line(char *text, uint16_t len, uint16_t *out_len)
{
    uint16_t start = 0U;
    uint16_t end = len;

    while ((start < end) &&
           ((text[start] == ' ') || (text[start] == '\t') ||
            (text[start] == '\r') || (text[start] == '\n'))) {
        start++;
    }

    while ((end > start) &&
           ((text[end - 1U] == ' ') || (text[end - 1U] == '\t') ||
            (text[end - 1U] == '\r') || (text[end - 1U] == '\n'))) {
        end--;
    }

    *out_len = (uint16_t)(end - start);
    return &text[start];
}

/**
 * @brief       执行一条已完整接收的 ASRPro 命令
 * @param       line 命令文本
 * @param       len 命令长度
 * @retval      无
 */
static void asrpro_dispatch_line(char *line, uint16_t len)
{
    const char *cmd;
    uint16_t cmd_len;

    cmd = asrpro_trim_line(line, len, &cmd_len);
    if (cmd_len == 0U)
        return;

#if ASRPRO_ENABLE_COMMAND_EXECUTION
    if ((cmd_len == 6U) && (memcmp(cmd, "led_on", 6U) == 0)) {
        helmet_alarm_set_base_led(RGB_LED_COLOR_WHITE);
        return;
    }

    if ((cmd_len == 7U) && (memcmp(cmd, "led_off", 7U) == 0)) {
        helmet_alarm_set_base_led(RGB_LED_COLOR_OFF);
        return;
    }

    if ((cmd_len == 13U) && (memcmp(cmd, "motor_speed_", 12U) == 0)) {
        char gear_ch = cmd[12];
        if ((gear_ch >= '0') && (gear_ch <= '3')) {
            uint8_t gear = (uint8_t)(gear_ch - '0');
            pwm_motor_set_speed(asrpro_motor_gear_to_percent[gear]);
        }
    }
#else
    (void)cmd;
    (void)cmd_len;
#endif
}

/**
 * @brief       处理调度器取出的一个 ASRPro 字节
 * @param       byte 接收字节
 * @retval      无
 */
static void asrpro_consume_byte(uint8_t byte)
{
    if (byte == (uint8_t)'\n') {
        if (asrpro_line_overflow == 0U) {
            asrpro_line_buffer[asrpro_line_len] = '\0';
            asrpro_dispatch_line(asrpro_line_buffer, asrpro_line_len);
        }
        asrpro_line_len = 0U;
        asrpro_line_overflow = 0U;
        return;
    }

    if (asrpro_line_overflow != 0U)
        return;

    if ((uint16_t)(asrpro_line_len + 1U) >= ASRPRO_LINE_BUFFER_SIZE) {
        asrpro_line_overflow = 1U;
        asrpro_line_len = 0U;
        return;
    }

    asrpro_line_buffer[asrpro_line_len++] = (char)byte;
}

/**
 * @brief       初始化 USART1 单字节接收，作为 ASRPro 命令输入
 * @param       无
 * @retval      0 成功，非 0 失败
 */
uint8_t asrpro_init(void)
{
    asrpro_ring_clear();
    asrpro_line_len = 0U;
    asrpro_line_overflow = 0U;

    if (HAL_UART_Receive_IT(&huart1, &asrpro_rx_byte, 1U) != HAL_OK)
        return 1U;

    return 0U;
}

/**
 * @brief       USART1 单字节接收完成回调入口
 * @param       huart HAL UART 句柄
 * @retval      无
 */
void asrpro_uart_rx_cplt_callback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != USART1))
        return;

    asrpro_ring_write(asrpro_rx_byte);
    (void)HAL_UART_Receive_IT(&huart1, &asrpro_rx_byte, 1U);
}

/**
 * @brief       调度器任务：消费 ASRPro 串口命令并控制执行器
 * @param       无
 * @retval      无
 */
void asrpro_task(void)
{
    uint8_t byte;
    uint16_t guard = ASRPRO_RX_RING_SIZE;

    while ((guard > 0U) && (asrpro_ring_read_byte(&byte) != 0U)) {
        asrpro_consume_byte(byte);
        guard--;
    }
}

/**
 * @brief       HAL USART 接收完成弱回调覆盖，分发 USART1 ASRPro 字节
 * @param       huart HAL UART 句柄
 * @retval      无
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    asrpro_uart_rx_cplt_callback(huart);
}

/**
 * @brief       HAL USART 错误弱回调覆盖，USART1 出错后恢复 ASRPro 接收
 * @param       huart HAL UART 句柄
 * @retval      无
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != USART1))
        return;

    (void)HAL_UART_Receive_IT(&huart1, &asrpro_rx_byte, 1U);
}
