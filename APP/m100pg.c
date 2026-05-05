#include "m100pg.h"
#include "m100pg_bsp.h"
#include "usart.h"

#define M100PG_RX_DMA_BUFFER_SIZE   128U
#define M100PG_RX_RING_SIZE         512U
#define M100PG_FORWARD_CHUNK_SIZE   128U
#define M100PG_UART_TIMEOUT_MS      100U
#define M100PG_UPLOAD_PERIOD_MS     1000U
#define M100PG_TASK_HEARTBEAT_MS    3000U

extern DMA_HandleTypeDef hdma_usart2_rx;

static uint8_t m100pg_rx_dma_buffer[M100PG_RX_DMA_BUFFER_SIZE];
static uint8_t m100pg_rx_ring[M100PG_RX_RING_SIZE];
static uint8_t m100pg_forward_buffer[M100PG_FORWARD_CHUNK_SIZE];
static volatile uint16_t m100pg_rx_head = 0;
static volatile uint16_t m100pg_rx_tail = 0;
static volatile uint16_t m100pg_rx_count = 0;
static volatile uint16_t m100pg_last_rx_len = 0;
static volatile uint32_t m100pg_rx_events = 0;
static volatile uint8_t m100pg_rx_overflow = 0;
static uint8_t m100pg_debug_forward = 1;
static uint32_t m100pg_last_upload_tick = 0;
static uint32_t m100pg_last_heartbeat_tick = 0;

static void m100pg_ring_clear(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    m100pg_rx_head = 0;
    m100pg_rx_tail = 0;
    m100pg_rx_count = 0;
    m100pg_rx_overflow = 0U;
    if (primask == 0U) {
        __enable_irq();
    }
}

static void m100pg_ring_write(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (m100pg_rx_count >= M100PG_RX_RING_SIZE) {
            m100pg_rx_overflow = 1U;
            break;
        }

        m100pg_rx_ring[m100pg_rx_head] = data[i];
        m100pg_rx_head = (uint16_t)((m100pg_rx_head + 1U) % M100PG_RX_RING_SIZE);
        m100pg_rx_count++;
    }
}

static uint16_t m100pg_ring_read(uint8_t *data, uint16_t max_len)
{
    uint16_t len = 0;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    while ((len < max_len) && (m100pg_rx_count > 0U)) {
        data[len] = m100pg_rx_ring[m100pg_rx_tail];
        m100pg_rx_tail = (uint16_t)((m100pg_rx_tail + 1U) % M100PG_RX_RING_SIZE);
        m100pg_rx_count--;
        len++;
    }
    if (primask == 0U) {
        __enable_irq();
    }

    return len;
}

/**
 * @brief       取出并清除接收溢出标志
 * @param       无
 * @retval      1 发生过溢出，0 未发生
 */
static uint8_t m100pg_take_rx_overflow(void)
{
    uint8_t overflow;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    overflow = m100pg_rx_overflow;
    m100pg_rx_overflow = 0U;
    if (primask == 0U) {
        __enable_irq();
    }

    return overflow;
}

/**
 * @brief       通过 USART1 输出 4G 原始收发数据
 * @param       prefix 日志前缀
 * @param       data 原始数据
 * @param       len 数据长度
 * @retval      无
 */
static void m100pg_debug_write(const char *prefix, const uint8_t *data, uint16_t len)
{
    if ((m100pg_debug_forward == 0U) || (data == NULL) || (len == 0U))
        return;

    printf("%s len=%u\r\n", prefix, len);
    HAL_UART_Transmit(&huart1, (uint8_t *)data, len, M100PG_UART_TIMEOUT_MS);
    printf("\r\n");
}

uint8_t m100pg_init(void)
{
    m100pg_ring_clear();
    memset(m100pg_rx_dma_buffer, 0, sizeof(m100pg_rx_dma_buffer));

    m100pg_bsp_init();

    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, m100pg_rx_dma_buffer,
                                     sizeof(m100pg_rx_dma_buffer)) != HAL_OK) {
        printf("[4G] usart2 rx dma start failed\r\n");
        return 1;
    }

    __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
    printf("[4G] usart2 rx dma ready\r\n");
    return 0;
}

void m100pg_set_debug_forward(uint8_t enabled)
{
    m100pg_debug_forward = enabled ? 1U : 0U;
}

uint8_t m100pg_send_bytes(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
        return 1;

    if (HAL_UART_Transmit(&huart2, (uint8_t *)data, len,
                          M100PG_UART_TIMEOUT_MS) != HAL_OK)
        return 1;

    m100pg_debug_write("[4G TX]", data, len);
    return 0;
}

void m100pg_rx_event_callback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART2)
        return;

    HAL_UART_DMAStop(huart);

    if (size > M100PG_RX_DMA_BUFFER_SIZE)
        size = M100PG_RX_DMA_BUFFER_SIZE;

    if (size > 0U)
        m100pg_ring_write(m100pg_rx_dma_buffer, size);

    m100pg_last_rx_len = size;
    m100pg_rx_events++;

    memset(m100pg_rx_dma_buffer, 0, sizeof(m100pg_rx_dma_buffer));
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, m100pg_rx_dma_buffer,
                                 sizeof(m100pg_rx_dma_buffer));
    __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
}

void m100pg_task(void)
{
    uint32_t now = HAL_GetTick();
    uint16_t len = m100pg_ring_read(m100pg_forward_buffer,
                                    sizeof(m100pg_forward_buffer));
    uint8_t overflow = m100pg_take_rx_overflow();

    if ((m100pg_debug_forward != 0U) &&
        ((uint32_t)(now - m100pg_last_heartbeat_tick) >= M100PG_TASK_HEARTBEAT_MS)) {
        m100pg_last_heartbeat_tick = now;
        printf("[4G] task alive rx_events=%lu\r\n", (unsigned long)m100pg_rx_events);
    }

    if ((uint32_t)(now - m100pg_last_upload_tick) >= M100PG_UPLOAD_PERIOD_MS) {
        m100pg_last_upload_tick = now;
        m100pg_bsp_publish_task();
    }

    if ((m100pg_debug_forward != 0U) && (overflow != 0U))
        printf("[4G] rx ring overflow\r\n");

    if (len == 0U)
        return;

    if (m100pg_debug_forward != 0U) {
        printf("[4G RX] len=%u last=%u events=%lu\r\n",
               len, m100pg_last_rx_len, (unsigned long)m100pg_rx_events);
        HAL_UART_Transmit(&huart1, m100pg_forward_buffer, len, M100PG_UART_TIMEOUT_MS);
        printf("\r\n");
    }

    m100pg_proto_feed(&g_m100pg_proto, m100pg_forward_buffer, len);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    m100pg_rx_event_callback(huart, size);
}
