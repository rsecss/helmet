# brainstorm: 4G 联网模块上云与串口转发

## Goal

完成 4G 联网模块开发测试：将 STM32F103 已测得的传感器数据封包后通过 USART2 发送到自制云平台，并解析云端/上位机下发的数据以控制设备状态，例如 LED 开关；同时支持 USART2 与 USART1 之间的调试转发。

## What I already know

- 用户提供了语雀需求文档：https://yinerda.yuque.com/yt1fh6/4gdtu/li2wcpryzg8znvai
- 目标 MCU 是 STM32F103，已有 MQ2、DHT11、MPU6050、MAX30102 等传感器模块。
- 需要封装上传函数，通过串口 2 发送数据。
- 需要封装解析函数：先可转发到上位机串口，再按协议校验并解析有效数据。
- USART2 发送/接收数据需要转发到 USART1，方便调试。
- 用户已通过 CubeMX 手动打开 USART2。
- USART1 本阶段只做调试输出，同时也承载其他模块调试；后期需要接入语音模块，因此 4G 调试转发必须接口清晰，方便关闭或替换。
- 可参考当前仓库内 `GD32_HAL` 项目的 `APP/usart_app.c`、`Components/websocket/ws_protocol_parser.c`。
- `资料.md` 是官方硬件资料，但没有规定上云业务协议格式；本任务采用 `GD32_HAL` 的字符串命令格式。
- 串口收发方式已确定：USART2 使用 `DMA + 空闲中断 + RingBuffer`，实现思路参考 `GD32_HAL/APP/usart_app.c` 与 `GD32_HAL/Components/ringbuffer/`。
- 2026-04-29 恢复任务时确认：Phase 1 代码已基本落地，`APP/m100pg.c/.h` 已实现 USART2 ReceiveToIdle DMA 启动、接收缓存、调度器上下文转发到 USART1。
- 当前 `m100pg_task()` 只消费接收缓存并调试转发；尚未实现上传封包、下发命令解析、LED 控制。
- `APP/scheduler.c` 当前启用 `max30102_task` 与 `m100pg_task`，`mq2_task`、`dht11_task`、`mpu6050_task` 仍被注释，若 Phase 2 上传真实传感器值，需要恢复这些任务或定义取值兜底。
- 数据来源现状：MPU6050 和 MAX30102 已在头文件导出姿态/心率血氧变量；MQ2 的 `ppm` 是 `.c` 全局变量但未在 `mq2.h` 声明；DHT11 的 `temp/humi` 是 `.c` 内 `static` 变量，当前不能被 `m100pg.c` 直接读取。
- PA8 当前被 DHT11 用作单总线数据脚；PRD 早期把 PA8 当作 LED 控制脚，这会与 DHT11 冲突，Phase 2 前必须重新确认 LED 控制目标。
- 2026-04-29 用户确认：4G 基础调试已完成；正式上云封包、下发解析、LED 控制前，需要先完成三色 LED 模块开发和调试。
- 已创建前置子任务：`.trellis/tasks/04-29-tri-color-led`。

## Assumptions

- 4G 模块连接 USART2，自制云平台协议可用普通字符串拼接完成。
- USART1 是调试串口，当前工程已有 `printf` 走 USART1 的习惯。
- MVP 优先做数据上传、下发解析、LED 控制与调试转发，不先做复杂重连/离线缓存。
- 4G 模块内部不应直接散落 `printf`/`HAL_UART_Transmit(&huart1, ...)`，应通过独立调试接口转发，避免后期 USART1 改接语音模块时大面积改代码。
- 上传周期建议先按 1000ms 设计，与 DHT11 慢速采样周期对齐；后续可通过宏调整。

## Open Questions

- LED 控制目标需要确认：不能继续默认使用 PA8，因为 PA8 已被 DHT11 占用。
- 三色 LED 的 R/G/B 引脚和共阳/共阴类型尚未确认；确认前不能进入 LED 模块实现。
- 用户已确认上传帧使用默认字段集合：温湿度、烟雾、姿态、心率、血氧、LED 状态。

## Requirements

- 当前阶段只实现 USART2 → USART1 调试转发，封包上传、云端下发解析、LED 控制留到下一阶段。
- 4G Phase 2 进入前必须先完成三色 LED 模块；`m100pg` 不直接操作 LED GPIO，只调用 LED 模块接口。
- 封装传感器数据上传函数。
- 通过 USART2 向 4G 模块/云平台发送上传数据。
- 接收 USART2 数据并转发到 USART1 调试输出。
- USART1 调试转发必须封装为独立接口/开关，不与 4G 协议解析强耦合。
- USART2 接收必须采用 `HAL_UARTEx_ReceiveToIdle_DMA()`，中断回调只搬运数据并重启 DMA，不做字符串解析、不做阻塞打印。
- USART2 接收数据先进入 4G 专用 RingBuffer，由 `m100pg_task()` 在调度器上下文消费。
- 对下发数据做协议校验，校验通过后再解析控制命令。
- 支持至少一个 LED 开关控制状态更新。
- 下发控制协议采用 `GD32_HAL` 风格：`LED<编号>_ON` / `LED<编号>_OFF`。
- 上传协议采用简单字符串拼接，字段命名保持清晰，便于云平台和串口助手直接观察。
- 上传帧默认字段：`temp`、`hum`、`mq2`、`pitch`、`roll`、`yaw`、`hr`、`spo2`、`led`。
- 上传帧示例：`UP,temp=25,hum=60,mq2=123,pitch=1.2,roll=0.5,yaw=88.0,hr=78,spo2=98,led=1\r\n`。
- 默认上传周期：1000ms，可通过 `M100PG_UPLOAD_PERIOD_MS` 宏调整。

## Acceptance Criteria

- [ ] 单片机能通过 USART2 发送封包后的传感器数据。
- [ ] 上传帧包含 `temp/hum/mq2/pitch/roll/yaw/hr/spo2/led` 字段，缺失数据用清晰默认值或无效值占位。
- [ ] USART2 收到的数据能转发到 USART1 调试观察。
- [ ] 关闭 4G 调试转发后，上传和解析功能仍能正常工作。
- [ ] 合法下发命令能被解析并控制 LED 状态。
- [ ] `LED1_ON`、`LED1_OFF` 能分别打开/关闭 PA8 LED。
- [ ] 非法/不完整数据不会误触发控制动作。
- [ ] 上传/解析接口可被调度器或主循环稳定调用。
- [ ] USART2 空闲中断 DMA 接收能连续多次触发，不能只收到第一帧。
- [ ] RingBuffer 溢出时不阻塞中断，不解析半截脏数据。

## Definition of Done (team quality bar)

- 代码通过 Keil 编译，无新增 warning。
- APP 代码通过质量门禁：cppcheck、头文件守卫、UTF-8 无 BOM、LF。
- 串口实机测试覆盖上传、下发控制、非法数据、调试转发。
- README/CLAUDE/必要 spec 同步更新。

## Out of Scope (explicit)

- 本任务不先实现云平台服务端。
- 本任务不先实现复杂 TLS/MQTT 协议栈，除非语雀协议要求。
- 本任务不做传感器算法重构。
- 本任务不实现语音模块，仅预留 USART1 调试转发可分离的接口边界。

## Technical Notes

- 语雀链接通过 Grok fetch/search 未能获取公开内容，暂不能从外部确认协议帧格式。
- `资料.md` 确认 M100MG-B1/M100PG 类 DTU 为单 TTL 串口 DTU，适合设备控制、状态检测、传感器数据采集等通过 4G 与服务器通信的场景。
- `资料.md` 确认串口兼容 3.3V/5V 电平，波特率范围 1200-460800，支持 TCP/UDP/MQTT/HTTP/WebSocket 等，但未定义业务协议。
- `APP/m100pg.c` / `APP/m100pg.h` 已作为 4G 模块实现入口，当前覆盖 Phase 1 调试转发链路。
- CubeMX 已生成 USART2：`huart2`、`MX_USART2_UART_Init()`、PA2/PA3、USART2 IRQ、DMA1 Channel6 RX、`MX_USART2_UART_Init()` 调用已落盘。
- 当前已实现 `HAL_UARTEx_RxEventCallback()` 转调 `m100pg_rx_event_callback()`，并在 `m100pg_init()` 中启动 `HAL_UARTEx_ReceiveToIdle_DMA(&huart2, ...)`。
- `Core/Src/usart.c` 确认 USART2 参数为 115200, 8N1, TX/RX, no flow control；USART2 RX DMA 为 `DMA1_Channel6`，Normal 模式。
- `Core/Src/dma.c` 已使能 `DMA1_Channel6_IRQn`；`Core/Src/stm32f1xx_it.c` 已生成 `DMA1_Channel6_IRQHandler()` 和 `USART2_IRQHandler()`。
- PA8 不应再作为 MVP LED 控制对象，因为 DHT11 驱动会动态切换 PA8 输入/输出用于单总线通信。
- `APP/scheduler.c` 当前只启用 `max30102_task`，其他传感器任务被注释；4G 上传时需要确认是否恢复 MQ2/DHT11/MPU6050 任务。
- 设计约束：建议新增 `debug_uart_write()` / `m100pg_set_debug_forward()` 一类边界，4G 模块只调用调试抽象，不直接绑定 USART1。

## Reference: GD32_HAL uart_app

### 可迁移的实现思路

- `HAL_UARTEx_RxEventCallback()` 只做短操作：停止 DMA、把本次数据写入 ringbuffer、清空 DMA buffer、重新启动 `HAL_UARTEx_ReceiveToIdle_DMA()`。
- 联网通道独立于调试串口：GD32 中 `USART6` 是 websocket/联网数据通道，`USART1` 是 shell/调试通道；本项目映射为 `USART2` 联网、`USART1` 调试。
- 调度器用短周期任务消费 ringbuffer：GD32 中 `websocket_task` 周期 5ms，读取完整缓冲后再调协议解析。
- 下发命令通过回调解耦硬件控制：`ws_protocol_parser_init(my_led_controller)` 注册 LED 控制回调，解析器不直接操作 GPIO。
- LED 命令格式参考：`LED<编号>_ON` / `LED<编号>_OFF`，解析失败返回错误码，不触发回调。
- `rt_ringbuffer_put()` 在空间不足时截断写入，不阻塞；本项目应采用同类策略：中断侧不能等待。

### 不直接照搬的点

- GD32 依赖 `rt_ringbuffer`，本项目应优先复用已有 `APP/ringbuffer.c`；若接口不匹配再做薄适配。
- GD32 的 `websocket_task` 内直接 `my_printf(&huart1, ...)`，本项目必须改为可关闭的调试转发接口，避免 USART1 后期接语音模块时耦合。
- GD32 使用 `USART6` / F4 HAL；本项目目标是 STM32F103 的 `USART2`，中断名、DMA 通道、Keil 工程配置以 CubeMX 生成结果为准。
- GD32 命令解析以 LED 控制为主；本项目还需要增加传感器数据上传帧封装。
- GD32 `ws_protocol_parse_buffer()` 使用 `strstr()` 查找后缀，可能接受带额外字符的宽松输入；本项目 MVP 应只接受完整命令，避免云端噪声误触发。

## Final Technical Design

### Module Boundaries

| 模块/文件 | 职责 |
|-----------|------|
| `APP/m100pg.c/.h` | 4G 模块入口：初始化、USART2 发送、DMA 接收缓存消费、上传封包、下发解析、LED 控制接口 |
| `Core/Src/usart.c` | CubeMX 生成的 USART1/USART2 初始化；只在 `USER CODE` 区保留必要用户代码 |
| `Core/Src/stm32f1xx_it.c` | CubeMX 生成 IRQ 入口，保持 `HAL_UART_IRQHandler()` / `HAL_DMA_IRQHandler()` 调用 |
| `APP/scheduler.c` | 注册 `m100pg_task()` 周期任务 |
| `APP/bsp_system.h` | 引入 `m100pg.h`，暴露统一模块入口 |

### Data Flow

```text
云平台/4G DTU
  -> USART2 RX DMA
  -> HAL_UARTEx_RxEventCallback(huart2, Size)
  -> m100pg_rx_push(dma_buffer, Size)
  -> restart HAL_UARTEx_ReceiveToIdle_DMA()
  -> m100pg_task()
  -> debug forward (optional)
  -> m100pg_parse_downlink()
  -> LED control callback / PA8 state update
```

```text
传感器全局数据
  -> m100pg_upload_sensor_data()
  -> snprintf() 拼接 UP 帧
  -> HAL_UART_Transmit(&huart2, ...)
  -> debug forward TX (optional)
  -> 4G DTU / 云平台
```

### Public Interfaces (planned)

```c
uint8_t m100pg_init(void);
void m100pg_task(void);
uint8_t m100pg_upload_sensor_data(void);
uint8_t m100pg_send_bytes(const uint8_t *data, uint16_t len);
void m100pg_set_debug_forward(uint8_t enabled);
void m100pg_rx_event_callback(UART_HandleTypeDef *huart, uint16_t size);
```

约束：
- `m100pg_rx_event_callback()` 由 `HAL_UARTEx_RxEventCallback()` 转调，只处理 `USART2`。
- 中断路径只允许：停止/重启 DMA、写 RingBuffer、清 DMA 临时缓冲；不解析、不 `printf`。
- `m100pg_task()` 才能做调试转发、命令解析和 LED 控制。
- 调试转发必须可关闭，后期 USART1 改接语音模块时不影响 4G 上传/解析。

### Buffer Plan

| 缓冲区 | 建议大小 | 用途 |
|--------|----------|------|
| `m100pg_rx_dma_buffer` | 128 bytes | USART2 单次 DMA 空闲接收临时缓冲 |
| `m100pg_rx_ring_pool` | 512 bytes | USART2 异步接收 RingBuffer |
| `m100pg_rx_parse_buffer` | 256 bytes | `m100pg_task()` 消费后解析/转发 |
| `m100pg_tx_buffer` | 256 bytes | 上传帧 `snprintf()` 拼接 |

溢出策略：
- 中断侧 RingBuffer 空间不足时丢弃超出部分，并设置 `rx_overflow` 标志。
- 任务侧发现 `rx_overflow` 时可输出一次调试提示并清标志。
- 不在中断里等待空间，也不阻塞重启 DMA。

### Debug Forwarding Boundary

调试口抽象为内部接口，不把 USART1 写死到协议逻辑：

```text
m100pg_debug_write(prefix, data, len)
```

行为：
- `debug_forward=1`：USART2 RX/TX 原始数据按前缀转发到 USART1，例如 `[4G RX] ...`、`[4G TX] ...`。
- `debug_forward=0`：不输出，但上传、接收、解析照常运行。
- 后期接语音模块时，只需要关闭或替换 `m100pg_debug_write()`。

### Upload Trigger

- `m100pg_task()` 维护 `last_upload_tick`。
- 默认每 `M100PG_UPLOAD_PERIOD_MS = 1000` 上传一次。
- 上传前读取各传感器全局变量；无效值使用 `0`，不阻塞等待传感器刷新。

## Proposed Technical Approach

1. 新增/完善 `APP/m100pg.c/.h` 作为 4G 模块唯一入口。
2. 在 `m100pg_init()` 中初始化 4G ringbuffer 并启动 `HAL_UARTEx_ReceiveToIdle_DMA(&huart2, ...)`。
3. 在 `HAL_UARTEx_RxEventCallback()` 中识别 `USART2`，只写入 4G ringbuffer、重启 DMA，不做解析和打印。
4. 在 `m100pg_task()` 中读取 ringbuffer；先按开关转发调试输出，再调用 `m100pg_parse_downlink()`。
5. 上传函数 `m100pg_upload_sensor_data()` 负责字符串拼接并通过 USART2 发送；发送内容可选转发到调试接口。
6. 控制命令采用回调/专用函数边界，例如 `m100pg_led_control(uint8_t on)` 操作 PA8，解析层不直接散落 GPIO 代码。

### Failure & Edge Cases

| 场景 | 处理策略 |
|------|----------|
| USART2 只收到一次 | 每次 RxEvent 后必须重启 `HAL_UARTEx_ReceiveToIdle_DMA()` |
| 接收帧过长 | RingBuffer 截断/丢弃超出部分，不阻塞中断 |
| 下发 `LED1_ONxxx` | 拒绝，不改变 PA8 |
| 下发 `LED2_ON` | MVP 拒绝，只支持 LED1 |
| 下发无换行命令 | 若完整等于 `LED1_ON` / `LED1_OFF` 仍可接受 |
| 多条命令粘包 | MVP 可先按一次接收整体解析；后续再扩展按 `\r\n` 分帧 |
| 传感器值无效 | 上传 `0`，保持字段完整 |
| USART1 调试关闭 | 不影响 USART2 收发和解析 |

### Phase 1 Debug Ladder

方法二（电脑只接 USART1，USART2 接真实 4G 模块）不能单独证明 USART2 链路错误；它同时依赖 4G 模块已联网、云端确实下发、模块处于透传/输出状态。

调试顺序：
1. 上电后 USART1 必须看到 `[4G] usart2 rx dma ready`；否则先查 `m100pg_init()`、DMA、USART2 初始化。
2. 真实 4G 模块有下发时，USART1 必须看到 `[4G RX] len=<n> ...`；若没有，说明 STM32 没收到 USART2 RxEvent。
3. 若看到 `[4G RX]` 但没有原始内容，再查 RingBuffer 消费和 USART1 发送。
4. 若方法二无输出，优先用双 USB-TTL 或 PA2-PA3 自环隔离：先证明 STM32 USART2 RX/DMA 正常，再查 4G 模块/云端链路。

## Protocol Contract

### Upload

```text
UP,temp=<int>,hum=<int>,mq2=<int>,pitch=<float>,roll=<float>,yaw=<float>,hr=<int>,spo2=<int>,led=<0|1>\r\n
```

- `temp` / `hum`: DHT11 温湿度。
- `mq2`: MQ2 烟雾传感器输出值。
- `pitch` / `roll` / `yaw`: MPU6050 姿态角。
- `hr` / `spo2`: MAX30102 心率和血氧；无效时使用 `0`。
- `led`: PA8 当前状态，`1` 表示打开，`0` 表示关闭。

### Downlink

```text
LED1_ON
LED1_OFF
```

- 只接受完整命令。
- 非 `LED1_ON` / `LED1_OFF` 的内容不得改变 GPIO。
- 可先把原始下发数据转发到调试接口，再做协议解析。

### Debug Forwarding

- USART1 调试转发必须由独立接口控制，不能散落在协议解析中。
- 关闭调试转发后，USART2 上传、接收和解析仍必须正常工作。

## Decision (ADR-lite)

**Context**: 官方 `资料.md` 只说明 DTU 串口透传和支持的网络协议，没有业务帧格式；项目需要一个可快速测试、便于云平台和串口助手观察的上行/下行协议。

**Decision**: 下发控制命令采用 `GD32_HAL` 的字符串命令格式：`LED<编号>_ON` / `LED<编号>_OFF`。本项目 MVP 只实现 `LED1_ON` / `LED1_OFF` 控制 PA8。上传帧采用可读字符串拼接，保持简单可调试。

**Consequences**:
- 优点：无需额外协议栈，串口助手和云平台都能直接发送/观察。
- 优点：LED 控制解析可复用 GD32 的回调解耦思路。
- 代价：没有二进制校验和复杂分包能力；MVP 通过完整字符串匹配和长度边界降低误触发风险。

## Implementation Plan

1. Phase 1：完成 `APP/m100pg.c/.h` 的 USART2 DMA 空闲接收、RingBuffer 缓存、USART1 原始转发、调试转发开关。
2. Phase 2：补充上传封包、下发解析、LED 控制。
3. 在 `Core/Src/main.c` 的 `USER CODE` 区调用 `m100pg_init()`，启动 USART2 空闲 DMA 接收。
4. 在 `HAL_UARTEx_RxEventCallback()` 中处理 USART2 接收，只写入 ringbuffer 并重启 DMA。
5. 在 `APP/scheduler.c` 增加 `m100pg_task`，周期消费接收缓存。
6. 实机先验证 USART2 输入能连续转发到 USART1；通过后再进入 Phase 2。
