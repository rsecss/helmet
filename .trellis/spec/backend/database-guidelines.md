# Database Guidelines

> Data storage & access patterns for this project.

---

## Overview

SmartHelm 感知层运行于 STM32F103C8T6（64 KB Flash / 20 KB SRAM），**不使用传统数据库或 ORM**。本文档将"数据层"解释为**嵌入式数据存储与访问策略**：

- **运行时内存数据**：全局变量 / 静态变量 / DMA 缓冲区 / 环形缓冲区
- **跨模块数据共享**：通过 `.h` 中的 `extern` 声明 + `.c` 中的定义
- **持久化存储**：**当前未使用**；未来如需保存校准参数、配置等，应通过内部 Flash 的 Last Sector 方式实现（待扩展）

当前文档覆盖范围：**感知层内存数据**。传输层数据帧协议、应用层云端存储在对应层代码落地时再扩展。

---

## Query Patterns

感知层数据流的三种访问模式，已在现有代码中定型：

### 1. DMA 外设 → 固定缓冲区（硬件直接写入）

**特点**：外设数据量稳定、采集速率高，CPU 不直接读外设寄存器。

```c
/* APP/bsp_system.h 声明 */
extern uint32_t dma_buff[30];               // ADC1 DMA 循环采样
extern uint8_t  usart_rx_dma_buffer[1000];  // USART2 DMA 空闲中断接收

/* main.c 启动 */
HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&dma_buff[0], 30);
HAL_UARTEx_ReceiveToIdle_DMA(&huart2, usart_rx_dma_buffer, sizeof(usart_rx_dma_buffer));
```

**适用**：ADC 连续采样（MQ2）、UART 不定长接收（4G/GPS）。

### 2. 环形缓冲区（生产 - 消费模式）

**特点**：中断侧写入、任务侧读取，避免阻塞中断。参见 `APP/ringbuffer.c`。

```c
/* 中断回调（生产者） */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (!ringbuffer_is_full(&usart_rb))
        ringbuffer_write(&usart_rb, usart_rx_dma_buffer, Size);
    memset(usart_rx_dma_buffer, 0, sizeof(usart_rx_dma_buffer));
}

/* 调度器任务（消费者） */
void m100pg_task(void)
{
    if (ringbuffer_is_empty(&usart_rb)) return;
    ringbuffer_read(&usart_rb, usart_read_buffer, usart_rb.itemCount);
    /* 解析... */
}
```

**适用**：异步接收的数据流（UART、SPI），存在读写速率不匹配的场景。

### 3. 全局变量（任务间状态共享）

**特点**：单一生产者（任务内写）、多个消费者（其他模块读）。

```c
/* APP/mpu6050.h */
extern float pitch, roll, yaw;
extern bool  fall_flag;

/* APP/mpu6050.c（生产者） */
void mpu6050_task(void)
{
    mpu6050_dmp_get_data(&pitch, &roll, &yaw);
    fall_flag = ((fabs(pitch) > 60) | (fabs(roll) > 60));
}
```

**适用**：传感器解算结果、事件标志。**禁止多任务写同一变量**——协作式调度下虽无抢占，但仍需保持单一写者约定以便未来切换 RTOS。

---

## Migrations

**当前版本：无持久化存储，无需迁移。**

未来如引入 Flash/EEPROM 存储（例如 MPU6050 校准参数、用户配置），应遵循：

- **版本字段**：存储结构体第一个字段为 `uint16_t version`，允许启动时判定布局
- **CRC 校验**：结构体末尾附 CRC16，避免断电写入半途损坏
- **地址规划**：STM32F103C8T6 Flash 按 1 KB 页擦除；建议使用最后 1~2 页（地址 `0x0800FC00` 起）
- **向后兼容**：新版本读旧布局时退化到默认值，不要强制擦除

文档此策略在首个持久化特性落地时补充示例。

---

## Naming Conventions

| 类别 | 规则 | 示例 |
|------|------|------|
| DMA 缓冲区 | `<外设>_<方向>_dma_buffer` | `usart_rx_dma_buffer`, `dma_buff`（历史遗留） |
| 环形缓冲区实例 | `<用途>_rb` | `usart_rb` |
| 传感器原始值 | 简短三字母 + 轴 | `aacx/aacy/aacz`（加速度）, `gyrox/gyroy/gyroz`（角速度） |
| 解算结果 | 物理量全称 | `pitch`, `roll`, `yaw`, `AVM`, `GVM`（向量模） |
| 事件标志 | `<事件>_flag` | `fall_flag`（跌倒） |
| 缓冲区尺寸宏 | `<模块>_BUFFER_SIZE` 或 `RINGBUFFER_SIZE` | `RINGBUFFER_SIZE`, `BUFFER_SIZE` |

**跨模块访问 extern 变量** 必须在 `.h` 用 `extern` 声明，在 `.c` 定义一次，禁止在多个 `.c` 重复定义。

---

## Common Mistakes

1. **DMA 缓冲被栈变量覆盖**：DMA 缓冲区必须是**全局或静态**，不能是函数局部变量，否则栈回收导致数据损坏。

2. **中断中调用 `printf`**：中断回调应只做"搬运"（如把 DMA 缓冲拷进环形缓冲），不要在中断里解析或打印，否则延长中断驻留、丢失后续数据。参考 `HAL_UARTEx_RxEventCallback`。

3. **环形缓冲区满时丢写未告警**：当前 `ringbuffer_write` 满则返回 `-1` 静默丢弃，生产者未检查返回值会静悄悄丢数据。修改代码时若对数据完整性要求高，应检查返回值或增加满告警标志。

4. **DMA 缓冲清零时机错误**：UART DMA 回调中 `memset(usart_rx_dma_buffer, 0, ...)` **必须**在 `ringbuffer_write` 之后执行，否则会擦掉未入队的数据。

5. **`extern` 声明缺失 / 重复定义**：新模块的全局变量应在 `.h` 用 `extern`、在 `.c` 定义一次。忘记 `extern` 会链接错误；多处定义会"multiple definition"链接失败。

6. **全局变量大小写与模块前缀不一致**：例如 `AVM`/`GVM`（历史遗留，全大写容易误认为宏）。新代码应使用 `mpu6050_avm` 这类带模块前缀的 snake_case 命名。
