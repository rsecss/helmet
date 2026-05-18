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

**特点**：中断侧写入、任务侧读取，避免阻塞中断。当前每个 UART 驱动模块在自己的 `.c` 内部维护私有静态环形缓冲（参见 `APP/m100pg.c::m100pg_rx_ring` + `m100pg_ring_*`、`APP/asrpro.c::asrpro_rx_ring` + `asrpro_ring_*`），不再共享公共 `ringbuffer` 组件。

```c
/* APP/m100pg.c 中断回调（生产者，简化示意） */
void m100pg_rx_event_callback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART2) return;
    m100pg_ring_write(m100pg_rx_dma_buffer, size);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, m100pg_rx_dma_buffer,
                                 sizeof(m100pg_rx_dma_buffer));
}

/* APP/m100pg.c 调度器任务（消费者） */
void m100pg_task(void)
{
    uint16_t len = m100pg_ring_read(m100pg_forward_buffer,
                                    sizeof(m100pg_forward_buffer));
    if (len) m100pg_proto_feed(&m100pg_proto, m100pg_forward_buffer, len);
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
| DMA 缓冲区 | `<模块>_<方向>_dma_buffer` 或模块私有 static 数组 | `m100pg_rx_dma_buffer`, `dma_buff`（ADC 历史名） |
| 环形缓冲区实例 | 模块私有 static 数组 + `<模块>_ring_*` 接口，不暴露给 `.h` | `m100pg_rx_ring`, `asrpro_rx_ring` |
| 传感器原始值 | 简短三字母 + 轴 | `aacx/aacy/aacz`（加速度）, `gyrox/gyroy/gyroz`（角速度） |
| 解算结果 | 物理量全称 | `pitch`, `roll`, `yaw`, `AVM`, `GVM`（向量模） |
| 事件标志 | `<事件>_flag` 或模块 `is_*` 接口 | `mpu6050_is_fall_alarm()`, `mpu6050_is_collision_alarm()` |
| 缓冲区尺寸宏 | `<模块>_<用途>_SIZE`，模块前缀强制 | `M100PG_RX_RING_SIZE`, `ASRPRO_LINE_BUFFER_SIZE` |

**跨模块访问 extern 变量** 必须在 `.h` 用 `extern` 声明，在 `.c` 定义一次，禁止在多个 `.c` 重复定义。

---

## Common Mistakes

1. **DMA 缓冲被栈变量覆盖**：DMA 缓冲区必须是**全局或静态**，不能是函数局部变量，否则栈回收导致数据损坏。

2. **中断中调用 `printf`**：中断回调应只做"搬运"（如把 DMA 缓冲拷进环形缓冲），不要在中断里解析或打印，否则延长中断驻留、丢失后续数据。参考 `HAL_UARTEx_RxEventCallback`。

3. **环形缓冲区满时丢写未告警**：模块私有 ring write 满时通常静默丢弃；生产者必须检查返回值或维护溢出标志（参考 `APP/m100pg.c::m100pg_rx_overflow`），并在调度器任务中以 `printf("[4G] rx ring overflow\r\n")` 等方式可观测，避免静悄悄丢数据。

4. **DMA 缓冲清零时机错误**：UART DMA 回调中 `memset(rx_dma_buffer, 0, ...)` **必须**在写入私有 ring buffer 之后执行，否则会擦掉未入队的数据。

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

## Scenario: MPU6050 Safety Alarm Fanout

### 1. Scope / Trigger

适用于 `APP/mpu6050.c` 基于姿态/加速度/角速度产生本地安全报警，并把报警状态扇出到 LCD、4G telemetry、RGB 本地输出的跨模块变更。目标是保守少误报：侧放头盔不能单独触发倒地报警，倒地和激烈碰撞必须可独立触发并可同时上报。

### 2. Signatures / Payload Fields

```c
uint8_t mpu6050_is_fall_alarm(void);
uint8_t mpu6050_is_collision_alarm(void);
uint8_t mpu6050_get_alarm_flags(void);  /* bit0=fall, bit1=collision */
void mpu6050_clear_alarm(void);

void helmet_alarm_set_base_led(rgb_led_color_t color);
void helmet_alarm_task(void);
```

`helmet_telemetry_t` 必须包含并上传：

```c
uint8_t fall;       /* 0/1，来自 mpu6050_is_fall_alarm() */
uint8_t collision;  /* 0/1，来自 mpu6050_is_collision_alarm() */
```

上行 payload 字段顺序包含：

```text
pitch=%.1f,roll=%.1f,yaw=%.1f,fall=%u,collision=%u,hr=%ld,spo2=%ld
```

### 3. Contracts

- `mpu6050_task()` 是报警状态唯一写者；LCD、4G、RGB 只能通过 `mpu6050_is_*()` 或 `mpu6050_get_alarm_flags()` 读取。
- 倒地报警不得由姿态角单独触发；必须先出现低重力或冲击证据，再持续确认倾斜且低角速度的稳定姿态。
- `collision_alarm` 与 `fall_alarm` 独立；碰撞短保持自动清除，倒地长保持后需要恢复姿态/运动稳定才自动清除。
- 上电或 MPU6050 重新可用后的首帧 AVM 只建立基线，不参与冲击判断，避免 `0 -> 1g` 启动跳变误报。
- `helmet_alarm_task()` 只负责本地 RGB 输出仲裁；云端 LED 下发必须调用 `helmet_alarm_set_base_led()`，不得绕过报警模块直接写 RGB。
- 报警期间 RGB 必须红灯全亮/全灭快闪；报警解除后恢复最近一次云端基础 LED 颜色。
- `m100pg_bsp_collect_sample()` 只读取缓存快照，不等待 MPU6050 刷新；任一报警存在时 telemetry 的 `led` 镜像可覆盖为 `red`。

### 4. Validation & Error Matrix

| 现象 | 先查 | 必须验证 |
|------|------|----------|
| 上电静止侧放后触发 `FALL` | 首帧 AVM 基线和倒地状态机 | 首帧不参与 `avm_delta`，姿态角不能绕过运动事件直接报警 |
| 轻微拿放或桌面侧放误报倒地 | 阈值和确认窗口 | `MPU6050_FALL_IMPACT_*`、`MPU6050_FALL_CONFIRM_MS` 保守，侧放无冲击时 LCD 仍为 `OK` |
| 碰撞后 LCD/4G 很快看不到报警 | 碰撞保持和本地展示保持 | `collision_flag` 有短保持；RGB 至少快闪本地展示窗口 |
| 云端 LED 下发覆盖报警闪烁 | RGB 调用路径 | `rg "rgb_led_set_|rgb_led_off\\(" APP --glob "!APP/rgb_led.c" --glob "!APP/helmet_alarm.c"` 只能命中头文件声明 |
| 4G 上传无 `fall/collision` 字段 | telemetry 协议 | `m100pg_protocol.c` 格式串和 `helmet_telemetry_t` 字段同步，`M100PG_PROTO_TX_BUF` 足够 |
| LCD 显示乱码或编译字符串报错 | LCD 中文标签编码 | `APP/lcd_app.c` 中文运行时字符串使用 UTF-8 hex escape |

### 5. Good/Base/Bad Cases

**Good：事故级倒地**

```text
直立 -> 软垫落下/冲击 -> 明显倾斜静止 >= 1.5s
期望：fall=1, collision 可同时为 1，LCD 显示 FALL 或 FALL+HIT，RGB 红灯快闪。
```

**Base：普通侧放**

```text
手动轻放到侧面，无明显冲击
期望：fall=0, collision=0，LCD 显示 OK，云端 fall=0,collision=0。
```

**Bad：姿态角直接报警**

```c
fall_flag = ((fabs(pitch) > 60) || (fabs(roll) > 60));
```

该写法会把侧放头盔误判为倒地，禁止恢复。

### 6. Tests Required

- Keil Build 通过，`APP/helmet_alarm.c` 已加入 `MDK-ARM/helmet.uvprojx`。
- `git diff --check` 通过；本轮触及 `APP/*.h` 有 include guard；本轮触及源码 UTF-8 无 BOM 且 LF。
- `rg "HAL_MAX_DELAY|0xFFFFFF|printf" APP/mpu6050.c APP/helmet_alarm.c APP/helmet_alarm.h APP/lcd_app.c` 不得命中新增高频阻塞或打印；`snprintf` 命中可接受。
- 台架测试普通侧放、轻拿轻放、软垫碰撞、软垫倒地、倒地恢复、`FALL+HIT` 同时报警。
- 4G telemetry 至少观察一帧包含 `fall=0/1,collision=0/1`；报警时本地 RGB 快闪不影响云端下发基础颜色恢复。

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

## Scenario: MQ2 Smoke Trend Alarm Fanout

### 1. Scope / Trigger

适用于 `APP/mq2.c/.h` 基于 ADC1 DMA 产生烟雾趋势值，并把趋势异常扇出到 LCD、4G telemetry、RGB 本地报警仲裁的跨模块变更。目标是把 MQ2 当作工程趋势提示，不宣称标准气体定量浓度。

### 2. Signatures / Payload Fields

`APP/mq2.h` public API:

```c
void mq2_task(void);
float mq2_get_ppm(void);          /* LPG 曲线折算值，仅作趋势参考 */
float mq2_get_trend_index(void);  /* 清洁空气约为 100 */
float mq2_get_rs_r0_ratio(void);
float mq2_get_voltage(void);
float mq2_get_r0(void);
uint8_t mq2_is_calibrated(void);
uint8_t mq2_is_trend_alarm(void);
```

`helmet_telemetry_t` 必须包含并上传：

```c
uint32_t mq2;       /* rounded mq2_get_trend_index() */
uint8_t  mq2_alarm; /* mq2_is_trend_alarm() */
```

上行 payload 字段顺序包含：

```text
temp=%u,hum=%u,mq2=%lu,mq2_alarm=%u,pitch=%.1f,roll=%.1f,yaw=%.1f
```

### 3. Contracts

- `mq2_task()` 是 MQ2 趋势值、`R0`、`Rs/R0` 和报警状态的唯一写者；LCD、4G、RGB 只能通过 `mq2_*` public getters 读取。
- ADC 采集只能使用 `dma_buff[30]` 和 `HAL_ADC_Start_DMA()` 已启动的循环 DMA；不得恢复阻塞式 `HAL_ADC_PollForConversion()` 常规读取任务。
- 上电清洁空气窗口内完成 `R0` 校准，报警计数忽略前 5 次任务样本。实机测试时上电前 5 次采样必须处于清洁空气，否则 `R0` 会被污染。
- `mq2` 遥测字段和 LCD 显示使用归一化趋势指数，清洁空气约为 `100`；`mq2_get_ppm()` 只作为 LPG 曲线折算参考，不作为论文中的精确定量浓度。
- 报警迟滞阈值集中在 `APP/mq2.c` 顶部：`MQ2_TREND_ALARM_ON_INDEX=180.0f`、`MQ2_TREND_ALARM_OFF_INDEX=130.0f`、连续触发 `3` 次、连续恢复 `10` 次。
- `helmet_alarm_task()` 中 MPU6050 跌倒/碰撞红灯优先于 MQ2 黄灯；MQ2 报警只输出黄灯快闪，不覆盖更高优先级红灯。
- `m100pg_bsp.c` 可在 MQ2 报警且无 MPU6050 报警时把 telemetry 的 `led` 镜像为 `yellow`；下行命令不新增 `led_color_yellow`。
- 若硬件在 MQ2 AO 到 PA0 之间存在分压，必须调整 `MQ2_SENSOR_OUTPUT_SCALE`，否则 `Rs` 计算基于错误电压域。

### 4. Validation & Error Matrix

| 现象 | 先查 | 必须验证 |
|------|------|----------|
| 空气中趋势值不是约 100 | 上电清洁空气校准窗口 | 上电后前 5 次采样没有气体干扰，`mq2_is_calibrated()` 变为 1，`mq2_get_r0()` 为正值 |
| 打火机气体靠近趋势值方向不对 | `Rs/R0` 与电压回路 | 气体靠近时 `mq2_get_rs_r0_ratio()` 下降，`mq2_get_trend_index()` 上升 |
| 打火机气体明显变化但不报警 | 阈值、EMA、连续计数 | 趋势指数连续 3 次达到 `MQ2_TREND_ALARM_ON_INDEX` 后 `mq2_is_trend_alarm()` 为 1 |
| 空气恢复后黄灯长时间不灭 | 恢复迟滞和计数 | 趋势指数连续 10 次低于 `MQ2_TREND_ALARM_OFF_INDEX` 后 `mq2_is_trend_alarm()` 为 0 |
| 云端 `mq2` 看似 ppm | 字段语义文档 | README 和协议注释必须写“归一化趋势指数”，不要写 ppm 估算 |
| 云端 LED 下发覆盖黄灯报警 | RGB 调用路径 | LED 命令走 `helmet_alarm_set_base_led()`；报警任务仍可输出黄灯快闪 |
| STM32 ADC 输入过压 | MQ2 模块供电和 AO 连接 | 确认 PA0 最大电压不超过 3.3V；若有分压，更新 `MQ2_SENSOR_OUTPUT_SCALE` |

### 5. Good/Base/Bad Cases

**Good：烟雾趋势异常**

```text
清洁空气上电 -> mq2≈100 -> 打火机气体靠近 -> mq2 上升并连续越过 180
期望：mq2_alarm=1，LCD 烟雾行显示 ALM，本地 RGB 黄灯快闪。
```

**Base：清洁空气稳定**

```text
清洁空气上电并保持通风
期望：mq2≈100，mq2_alarm=0，RGB 不进入 MQ2 黄灯报警。
```

**Bad：把 MQ2 写成精确定量 ppm**

```text
论文或 README 写“检测 LPG=150ppm”
```

当前工程只能支撑“MQ2 归一化趋势指数约 150”，不能支撑标准气体浓度认证。

### 6. Tests Required

- `git diff --check` 通过；本轮触及 `APP/*.h` 有 include guard；本轮触及源码 UTF-8 无 BOM 且 LF。
- 搜索 `APP/mq2.c APP/mq2.h` 不得命中 `HAL_ADC_PollForConversion`、`HAL_MAX_DELAY`、`mq2_task1`、周期 `printf`。
- Keil Build 通过，确认 `pow()` 链接不报错，`APP/mq2.c` 仍在 `MDK-ARM/helmet.uvprojx`。
- 实机清洁空气上电，观察 `mq2` telemetry 或 LCD 稳定在约 `100`，`mq2_alarm=0`。
- 实机用打火机气体靠近，观察 `mq2` 上升；连续越过阈值后 LCD 出现 `ALM`，RGB 黄灯快闪，telemetry 包含 `mq2_alarm=1,led=yellow`。
- 移开气体并通风，观察 `mq2` 下降；连续恢复后 `mq2_alarm=0`，RGB 恢复基础灯色或关闭。
- 同时触发 MPU6050 报警和 MQ2 报警时，RGB 必须红灯优先；telemetry 可保留 `mq2_alarm=1` 以表达并发状态。

### 7. Wrong vs Correct

#### Wrong

```c
void mq2_task1(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    adc_value = HAL_ADC_GetValue(&hadc1);
}
```

#### Correct

```c
void mq2_task(void)
{
    if (mq2_measure() == 0U) {
        mq2_update_trend_alarm(mq2_state.trend_index);
        return;
    }
    if (mq2_update_calibration() == 0U) {
        mq2_update_trend_alarm(mq2_state.trend_index);
        return;
    }
    mq2_update_concentration();
    mq2_update_trend_alarm(mq2_state.trend_index);
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

---

## Scenario: ASRPro USART1 Offline Voice Commands

### 1. Scope / Trigger

适用于 ASRPro / 天问离线语音模块接入 STM32 USART1，并把语音固件输出的固定文本命令映射到本地执行器。

触发条件：
- 新增或修改 `APP/asrpro.c/.h`。
- 修改 USART1 所有权、`printf` 重定向、USART1 IRQ、语音命令字典。
- 新增语音控制 LED、电机或其他本地执行器。

### 2. Signatures

ASRPro 模块公开 API：

```c
uint8_t asrpro_init(void);
void asrpro_task(void);
void asrpro_uart_rx_cplt_callback(UART_HandleTypeDef *huart);
```

HAL / CubeMX 用户入口：

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);
void USART1_IRQHandler(void);
```

编译期开关：

```c
#define ASRPRO_ENABLE_COMMAND_EXECUTION    1U
#define ASRPRO_ENABLE_USART1_DEBUG         0U
```

硬件合同：

```text
ASRPro ASR_TX -> STM32 PA10 (USART1_RX)
ASRPro ASR_RX -> STM32 PA9  (USART1_TX)
USART1        -> 115200-8N1
```

命令字典：

```text
led_on          -> helmet_alarm_set_base_led(RGB_LED_COLOR_WHITE)
led_off         -> helmet_alarm_set_base_led(RGB_LED_COLOR_OFF)
motor_speed_0   -> pwm_motor_set_speed(0)
motor_speed_1   -> pwm_motor_set_speed(33)
motor_speed_2   -> pwm_motor_set_speed(66)
motor_speed_3   -> pwm_motor_set_speed(100)
```

### 3. Contracts

- 正常固件中 USART1 是 ASRPro 专用串口，不再作为默认日志通道。
- `ASRPRO_ENABLE_USART1_DEBUG` 默认必须为 `0U`；打开后才允许 `printf` / M100PG debug forward 向 PA9 输出。
- `ASRPRO_ENABLE_COMMAND_EXECUTION` 默认必须为 `1U`；临时串口调试需要防误触发时可设为 `0U`。
- USART1 采用单字节中断接收；`HAL_UART_RxCpltCallback()` 只允许把字节放入静态缓冲并重启 `HAL_UART_Receive_IT()`。
- `HAL_UART_ErrorCallback()` 必须在 USART1 错误后重启单字节接收，避免噪声/溢出后接收链路永久停止。
- 命令解析和执行器控制必须在 `asrpro_task()` 调度器任务中执行，不得在中断回调中调用 LED、电机、`printf` 或长阻塞 HAL。
- 解析只接受固定小写命令；允许首尾 ASCII 空白、`\r`、`\n`，不做中文、大小写、模糊或部分匹配。
- LED 控制必须走 `helmet_alarm_set_base_led()`，不得直接调用 `rgb_led_off()` 或写 RGB GPIO；安全报警红灯保持优先级。
- 电机控制必须复用 `pwm_motor_set_speed()`，档位映射保持 `{0, 33, 66, 100}`，与 4G 命令模型一致。
- 新增 `.c` 必须登记到 `APP/bsp_system.h`、`APP/scheduler.c`、`Core/Src/main.c` 和 `MDK-ARM/helmet.uvprojx`。

### 4. Validation & Error Matrix

| 现象 | 先查 | 必须验证 |
|------|------|----------|
| ASRPro 发命令无动作 | USART1 IRQ 和接收启动 | `asrpro_init()` 已在 `MX_USART1_UART_Init()` 后调用，`USART1_IRQHandler()` 调用 `HAL_UART_IRQHandler(&huart1)` |
| 只能收到第一条命令 | 接收重启 | `asrpro_uart_rx_cplt_callback()` 每次完成后重新调用 `HAL_UART_Receive_IT()` |
| 串口噪声后再也收不到命令 | 错误恢复 | `HAL_UART_ErrorCallback()` 识别 USART1 并重启单字节接收 |
| 语音模块收到大量无关文本 | USART1 调试开关 | `ASRPRO_ENABLE_USART1_DEBUG=0U`，`fputc()` 默认不向 `huart1` 发送，M100PG debug forward 默认关闭 |
| `LED_ON` 或中文文本触发动作 | 解析过宽 | 搜索解析逻辑，必须固定小写命令匹配，无大小写转换和模糊匹配 |
| 报警红灯被 `led_off` 关闭 | LED 绕过仲裁 | ASR 只能调用 `helmet_alarm_set_base_led()`，不得直接调用 `rgb_led_off()` / `rgb_led_set_color()` |
| `motor_speed_4` 启动电机 | 档位边界 | 只接受单字符 `'0'..'3'`，其他命令不触发执行器 |
| 4G 下发失效 | UART 回调混淆 | USART1 使用 `HAL_UART_RxCpltCallback()`，USART2/M100PG 继续使用 `HAL_UARTEx_RxEventCallback()` |

### 5. Good/Base/Bad Cases

**Good：ASR 命令行控制**

```text
USART1 RX 输入: " led_on\r\n"
期望: 调用 helmet_alarm_set_base_led(RGB_LED_COLOR_WHITE)，报警中仍显示红灯优先。
```

**Base：电机三档**

```text
USART1 RX 输入: "motor_speed_3\n"
期望: 调用 pwm_motor_set_speed(100)。
```

**Base：临时调试**

```text
ASRPRO_ENABLE_COMMAND_EXECUTION=0U
ASRPRO_ENABLE_USART1_DEBUG=1U
期望: USART1 可恢复 printf 调试，输入 motor_speed_3 不启动电机。
```

**Bad：中断内执行命令**

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        pwm_motor_set_speed(100U);
        printf("voice ok\r\n");
    }
}
```

**Bad：绕过安全报警**

```c
if (memcmp(cmd, "led_off", 7U) == 0) {
    rgb_led_off();
}
```

### 6. Tests Required

- `git diff --check` 通过；新增 `APP/asrpro.h` 含 `ASRPRO_H` include guard。
- 搜索 `APP/asrpro.c` 不得命中 `printf`、`HAL_Delay`、`HAL_MAX_DELAY`、`malloc`、`free`。
- 搜索 UART 回调：`HAL_UART_RxCpltCallback` 只负责 USART1 ASRPro，`HAL_UARTEx_RxEventCallback` 仍只分发 USART2 M100PG。
- Keil Build 通过，`APP/asrpro.c` 已加入 `MDK-ARM/helmet.uvprojx`。
- 实机或串口助手向 PA10 注入 `led_on\n`、`led_off\r\n`、` motor_speed_3 \n`，确认 LED 基础状态和电机档位变化。
- 注入 `LED_ON\n`、`motor_speed_4\n`、`motor_speed_33\n`、中文文本，确认无执行器动作。
- 报警状态下发送 `led_off\n`，确认红灯报警输出不被关闭。
- 默认 `ASRPRO_ENABLE_USART1_DEBUG=0U` 时，确认 PA9 不输出启动日志；临时打开后确认 USART1 调试输出恢复。

### 7. Wrong vs Correct

#### Wrong

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        parse_and_execute(asr_byte);   /* 中断中解析和控制执行器 */
    }
}
```

#### Correct

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    asrpro_uart_rx_cplt_callback(huart);
}

void asrpro_task(void)
{
    /* 从静态缓冲取完整行，裁剪 CR/LF/首尾空白，再执行固定命令 */
}
```
