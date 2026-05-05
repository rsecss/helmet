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

---

## Scenario: Cooperative Scheduler Sensor Task Nonblocking

### 1. Scope / Trigger

适用于所有由 `APP/scheduler.c` 周期调用的传感器任务，尤其是 I2C 轮询设备（如 MPU6050、MAX30102）。当 4G 下发无 `[4G RX]` 且只保留 `m100pg_task` 后恢复正常时，必须优先检查传感器任务是否阻塞主循环。

### 2. Signatures

```c
uint8_t sensor_init(void);  /* 返回 0 表示传感器可用，非 0 表示降级 */
void sensor_task(void);     /* 调度器周期任务，不返回状态给调度器 */
```

HAL 阻塞外设调用必须使用模块私有有限超时宏：

```c
#define SENSOR_I2C_TIMEOUT_MS 10U
HAL_I2C_Mem_Read(&hi2cX, addr, reg, I2C_MEMADD_SIZE_8BIT,
                 buf, len, SENSOR_I2C_TIMEOUT_MS);
```

### 3. Contracts

- 调度器任务不得使用 `HAL_MAX_DELAY`、`0xFFFFFF` 或无退出条件的轮询等待。
- 初始化失败必须保持模块 `ready=0`，任务入口先判断 `ready`，不可继续访问外设。
- 任务内 I2C 读写失败必须直接返回；会导致连续阻塞的硬件错误应清零结果并关闭 `ready`。
- 高频任务不得直接 `printf` 原始数据；USART1 调试口是阻塞单字节发送，传感器日志会影响 4G 调试观察。
- 上传模块只读取传感器缓存快照；不得为了上传等待传感器刷新。

### 4. Validation & Error Matrix

| 现象 | 先查 | 必须验证 |
|------|------|----------|
| 全量任务下无 `[4G RX]`，只保留 4G 正常 | 传感器任务阻塞 | 逐个恢复传感器任务，观察 `[4G] task alive` 和云端下发 |
| 恢复某 I2C 传感器后 4G 下发消失 | I2C 超时参数 | 搜索该模块是否有 `HAL_MAX_DELAY`、`0xFFFFFF`、长轮询 |
| 传感器未接或 ACK 失败后主循环卡死 | 初始化降级 | `*_init()` 失败后 `*_task()` 是否 `ready` 门控返回 |
| USART1 被传感器日志刷屏 | 高频打印 | `*_task()` 中是否有周期性 `printf` |

### 5. Good/Base/Bad Cases

**Good**：I2C 读写使用 10ms 级有限超时；初始化成功才置 `ready=1`；任务失败后直接返回或降级。

**Base**：任务本周期没有新 FIFO 数据时快速返回，不清除 4G 接收状态，不打印。

**Bad**：传感器任务中使用 `HAL_MAX_DELAY` 或每 10ms 打印姿态/心率，导致 `m100pg_task()` 无法及时消费 USART2 ring buffer。

### 5.1 Case Study: MPU6050 / MAX30102 阻塞 4G 下发

**Symptom**:
- 上电日志可到 `[BOOT] scheduler ready`，云端能看到周期上行。
- 云端下发 `LED_OFF` / `LED_GREEN` 时，USART1 无 `[4G RX]`，LED 无动作。
- 只保留 `m100pg_task` 后，下发立即恢复；`mq2_task`、`dht11_task` 与 4G 同跑正常。
- 单独恢复 `mpu6050_task` 或 `max30102_task`，4G 下发再次无响应。

**Root Cause**:
- `APP/mpu6050_inv_mpu.c` 的 `i2c_read/i2c_write` 使用 `0xFFFFFF` 超长超时。
- `APP/mpu6050.c`、`APP/max30102.c` 在调度器任务路径使用 `HAL_MAX_DELAY`。
- `APP/max30102.c` 周期任务内打印 `no finger` / HR / SpO2，USART1 为阻塞单字节发送，会干扰 4G 调试观察。
- 协作调度器没有抢占能力；任一传感器任务卡住时，`m100pg_task()` 不再消费 USART2 ring buffer。

**Fix Pattern**:
- 每个 I2C 传感器定义模块私有超时，例如 `MPU6050_I2C_TIMEOUT_MS`、`MAX30102_I2C_TIMEOUT_MS`。
- 初始化开始先 `ready=0` 并清零公开结果；所有初始化步骤成功后才 `ready=1`。
- 任务入口先判断 `ready`；任务内 I2C 失败时清零结果并关闭 `ready`。
- 高频传感器任务不得打印周期数据；调试日志放到低频状态变化或专用调试开关后面。
- `APP/scheduler.c` 中 `m100pg_task` 排在可能访问慢外设的传感器任务之前。

**Prevention Checklist**:
- 恢复全量任务前，逐个组合验证：`m100pg_task` + 单个传感器任务。
- 每恢复一个任务，至少观察一次云端下发是否出现 `[4G RX]`。
- 修改 I2C 传感器任务后，必须搜索该模块和配套 vendor driver：`HAL_MAX_DELAY|0xFFFFFF`。
- 提交前搜索高频任务日志：`rg "printf" APP/<sensor>.c`。

### 6. Tests Required

- `rg "HAL_MAX_DELAY|0xFFFFFF" APP/<sensor>.c APP/<vendor_driver>.c` 不应命中该传感器链路。
- `rg "printf" APP/<sensor>.c` 检查高频任务没有周期性日志。
- 实机逐个恢复任务：`m100pg_task` + 单个传感器运行至少 10 秒，USART1 仍能看到 `[4G] task alive`，云端下发能出现 `[4G RX]`。
- 断开或拔掉目标 I2C 传感器后，系统允许该传感器数据为 0/invalid，但 4G 上传和下发不能停止。

### 7. Wrong vs Correct

#### Wrong

```c
void sensor_task(void)
{
    HAL_I2C_Mem_Read(&hi2c1, addr, reg, I2C_MEMADD_SIZE_8BIT,
                     buf, len, HAL_MAX_DELAY);
    printf("raw=%u\r\n", raw);
}
```

#### Correct

```c
void sensor_task(void)
{
    if (!sensor_ready)
        return;
    if (HAL_I2C_Mem_Read(&hi2c1, addr, reg, I2C_MEMADD_SIZE_8BIT,
                         buf, len, SENSOR_I2C_TIMEOUT_MS) != HAL_OK) {
        sensor_ready = 0;
        sensor_clear_result();
        return;
    }
}
```

---

## Scenario: M100PG USART2 DMA 透传调试

### 1. Scope / Trigger

适用于 `APP/m100pg.c/.h` 这类 **DTU 透传模块**：云平台、4G DTU、STM32 USART2、USART1 调试口跨硬件链路联调。

触发条件：
- 新增或修改 USART2 / DMA / DTU 透传代码。
- 云端能收到上行但 USART1 看不到下发，或 USART1 有发送日志但云端收不到。
- 只有一个 USB 串口可观察，需要隔离 STM32 与 DTU 哪一侧异常。

### 2. Signatures

```c
uint8_t m100pg_init(void);
void m100pg_task(void);
uint8_t m100pg_send_bytes(const uint8_t *data, uint16_t len);
void m100pg_set_debug_forward(uint8_t enabled);
void m100pg_rx_event_callback(UART_HandleTypeDef *huart, uint16_t size);
```

CubeMX / HAL 入口：

```c
MX_USART2_UART_Init();
HAL_UARTEx_ReceiveToIdle_DMA(&huart2, m100pg_rx_dma_buffer,
                             sizeof(m100pg_rx_dma_buffer));
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size);
```

硬件合同：

```text
STM32 PA2 (USART2_TX) -> DTU RXD
STM32 PA3 (USART2_RX) <- DTU TXD
STM32 GND             -> DTU GND
USART1 PA9/PA10       -> USB-TTL 调试口
```

> **Warning**: DTU 与 STM32 必须共地。即使分在两块面包板，只要 UART 跨板连接，也必须把两侧 GND 接到同一参考地。

### 3. Contracts

- `USART2` 是联网数据通道，`USART1` 只做调试观察；协议逻辑不得依赖 USART1 永久在线。
- `m100pg_init()` 必须先清 ring buffer，再启动 `HAL_UARTEx_ReceiveToIdle_DMA()`，并关闭 DMA half-transfer 中断。
- `m100pg_rx_event_callback()` 只处理 `USART2`，中断路径只允许：停止 DMA、裁剪 `size`、写 ring buffer、记录计数、清 DMA 临时缓冲、重启 DMA。
- `m100pg_task()` 才能打印、转发、解析；关闭 `debug_forward` 后，上行发送和下行接收仍应工作。
- `m100pg_send_bytes()` 只能证明 STM32 已把字节送到 USART2 TX，不等价于云端已收到。
- 上电默认执行器状态必须与协议镜像默认值和 Web 默认状态一致。当前合同是 `APP/rgb_led.c::rgb_led_init()` 默认关闭，`APP/m100pg_protocol.c::m100pg_proto_init()` 默认 `mirror_led = HELMET_LED_OFF`，Web 默认/首帧上传显示 `led=off`。

### 4. Validation & Error Matrix

| 现象 | 先查 | 必须验证 |
|------|------|----------|
| USART1 无 `[4G] usart2 rx dma ready` | 初始化顺序 | `MX_DMA_Init()`、`MX_USART2_UART_Init()` 在 `m100pg_init()` 之前 |
| USART1 有 `[4G] usart2 rx dma ready` 和 `[BOOT] scheduler ready`，但无周期 `[4G TX]` | 调度器任务是否真正执行 | `m100pg_task()` 是否注册且排在可能阻塞的传感器任务之前；任务内周期心跳是否能输出 |
| 只保留 `m100pg_task` 后云端下发正常，全量任务下无 `[4G RX]` | 其他调度器任务阻塞主循环 | 按 `m100pg_task` + 单个传感器任务逐个恢复，观察 `[4G] task alive` 和 `[4G RX]`；重点查 `HAL_MAX_DELAY`、长轮询、微秒延时死等 |
| USART1 有 `[4G TX]` 或发送成功，但云端收不到 | 上行物理链路 | PA2->DTU RXD、共地、DTU WebSocket 已上线、波特率一致 |
| 云端下发，USART1 无 `[4G RX]` | 下行物理链路，不要先改协议解析或 LED 逻辑 | 先用 PA2->PA3 自环证明 STM32 USART2 RX/DMA 正常，再验证 DTU TXD->PA3、共地、云端确实下发、DTU 处于透传输出状态 |
| 自环有 `[4G RX]`，接 DTU 无 `[4G RX]` | DTU 下行链路 | DTU TXD 电平、TX/RX 是否交叉、云平台是否把命令发到设备下行、DTU 是否在线且未只开上行 |
| 有 `[4G RX]` 但 LED 命令无动作 | 协议内容 | USART1 原文是否完整等于 `LED_OFF` / `LED_ON` 等命令；若输出 `downlink ignored`，先修云端 payload 格式 |
| 上传帧一直 `led=off`，但实机上电 LED 亮 | 默认态不同步或未复位烧录 | 断电重启或按复位键后确认 `[RGB] init off`；检查 `rgb_led_init()`、`m100pg_proto_init()`、Web 默认状态三者一致 |
| 只收到第一帧 | DMA 重启 | RxEvent 后必须重新调用 `HAL_UARTEx_ReceiveToIdle_DMA()` |
| 数据偶发截断 | ring buffer 容量/消费周期 | 中断侧不阻塞，任务侧短周期消费，溢出需有可观测标志 |
| DTU 单独接 USB-TTL 正常，接 STM32 不正常 | STM32 侧 RX/TX/GND | 用 PA2->PA3 自环验证 STM32 USART2 DMA，再接 DTU |

### 4.1 Debug Gate Order

M100PG 问题必须按证据门逐层推进，不能从现象直接跳到协议或 GPIO：

1. **Boot gate**: USART1 看到 `[BOOT] ...` 和 `[4G] usart2 rx dma ready`，只证明初始化调用完成。
2. **Task gate**: USART1 周期看到 `[4G TX]`，才证明 `m100pg_task()` 在主循环中运行。若没有，先查调度器任务顺序、前置任务阻塞、`HAL_GetTick()`。
3. **STM32 RX gate**: 用 PA2->PA3 自环，发送固定字符串，必须看到 `[4G RX]` 和原文。通过后才说明 USART2 IRQ、DMA、RxEvent、ring buffer、任务消费链路正常。
4. **DTU downlink gate**: 自环通过但接 DTU 无 `[4G RX]` 时，问题在 DTU TXD->PA3、共地、云端下发或 DTU 透传配置，不在 `m100pg_protocol` 或 LED GPIO。
5. **Protocol gate**: 只有看到 `[4G RX]` 原文后，才检查 payload 是否完整匹配 `LED_ON` / `LED_OFF` / `LED_WHITE` / `LED_RED` / `LED_GREEN`。
6. **Scheduler isolation gate**: `m100pg_task` 单独运行正常后，逐个恢复传感器任务。每恢复一个任务必须观察至少 10 秒 `[4G] task alive` 和一次云端下发，定位阻塞源后再改该任务。

反例：没有 `[4G RX]` 时修改 `LED_OFF` 解析、LED 默认颜色、初始化顺序，都是表层修复；它们不能证明 USART2 下行链路已经把字节送进 MCU。

### 5. Good/Base/Bad Cases

**Good：完整 DTU 链路**

```text
串口助手(USART1) -> 观察调试输出
STM32 USART2     -> DTU 串口
云平台           -> 收 UP,test=hello；下发 DOWN,hello
期望 USART1      -> [4G RX] len=... 后跟 DOWN,hello
```

**Base：STM32 自环隔离**

```text
PA2(USART2_TX) -> PA3(USART2_RX)
m100pg_send_bytes("4G_SELFTEST\r\n", 13)
期望 USART1 -> [4G RX] len=13 ... 4G_SELFTEST
```

**Bad：只看单侧日志**

```text
[4G TX] len=15
UP,test=hello
```

这只能说明 STM32 调用了发送接口，不能证明 DTU 已收到，更不能证明云端已收到。

**Good：默认态一致**

```text
上电/复位 -> [RGB] init off -> 首帧 telemetry led=off
Web 下发 led_on  -> LED 白色点亮，后续 telemetry led=white
Web 下发 led_off -> LED 熄灭，后续 telemetry led=off
```

**Bad：默认态分裂**

```text
rgb_led_init() 点亮白色，但 m100pg_proto_init() 的 mirror_led=HELMET_LED_OFF
```

这会导致实物 LED 亮而 Web/串口上传持续显示 `led=off`，演示和调试都会被误导。

### 6. Tests Required

- 上电后 USART1 看到 `[4G] usart2 rx dma ready`。
- 上电后 USART1 看到 `[BOOT] scheduler ready` 后，必须继续看到周期 `[4G TX]`；否则先修调度器或阻塞任务。
- 若全量任务下无下发响应，先临时只保留 `m100pg_task`；确认正常后按 MQ2、DHT11、MPU6050、MAX30102 的顺序逐个恢复并记录结果。
- 自环测试：PA2->PA3，发送固定字符串，USART1 必须收到同一字符串。
- DTU 上行测试：串口助手触发发送 `UP,test=hello`，云端必须收到。
- DTU 下行测试：云端下发文本，USART1 必须出现 `[4G RX]` 和原文。
- LED 下发测试只能在 `[4G RX]` 已出现后执行；否则不得判断 `LED_OFF` 或 RGB GPIO 有问题。
- 上电默认态测试：烧录后必须断电重启或按复位键，USART1 看到 `[RGB] init off`，实机 LED 默认关闭，首帧上传 `led=off`。
- 连续下发 5 次，`events` 递增，不能只收到第一帧。
- 关闭 `debug_forward` 后，USART1 不输出，但上行发送和下行 ring buffer 消费不能被破坏。

### 7. Wrong vs Correct

#### Wrong

```c
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    printf("[4G RX] %u\r\n", size);  /* 中断里阻塞打印 */
    parse_downlink(rx_dma_buffer);   /* 中断里解析 */
}
```

#### Correct

```c
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    m100pg_rx_event_callback(huart, size);
}

void m100pg_task(void)
{
    /* 从 ring buffer 取数据，再调试转发或解析 */
}
```
