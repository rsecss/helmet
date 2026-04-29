# 三色LED模块开发

## Goal

先完成三色 LED 模块的固件封装和调试接口，作为 4G 云端下发控制的前置能力。模块应提供清晰的颜色/开关状态接口，供后续 `m100pg` 下发解析调用，并避免与 DHT11、USART、I2C、ADC、SWD 等既有外设引脚冲突。

## What I already know

- 项目 MCU 是 STM32F103C8T6，使用 STM32 HAL、CubeMX、Keil MDK-ARM。
- CubeMX 已生成 PB12/PB13/PB14 GPIO 输出配置，且默认输出低电平。
- PA8 在 README 和 `APP/dht11.c/.h` 中已经用于 DHT11 单总线数据脚，不能作为三色 LED 控制脚。
- 当前已用引脚：PA0=MQ2 ADC，PA2/PA3=USART2 4G，PA8=DHT11，PA9/PA10=USART1 调试，PA13/PA14=SWD，PB6/PB7=I2C1 MPU6050，PB10/PB11=I2C2 MAX30102。
- 4G Phase 2 需要 `LED1_ON` / `LED1_OFF` 或等价命令控制 LED 状态，并把 LED 状态打包进上传帧。
- 三色 LED 公共端接地，因此为共阴极；R=PB12，G=PB13，B=PB14。
- `helmet.ioc` 已配置 PB12/PB13/PB14，未发现与现有 CubeMX 外设配置冲突。

## Assumptions

- 三色 LED 是普通 RGB LED 模块，不是 WS2812/NeoPixel 这类单线时序灯。
- MVP 先做数字开关控制：红/绿/蓝三路 GPIO 置位组合出基础颜色；亮度/PWM 呼吸效果不进入第一阶段。
- 共阴极三色 LED 逻辑为高电平点亮、低电平熄灭。

## Open Questions

- 当前无阻塞问题；下一步需要实机验证红、绿、蓝和关闭状态。

## Requirements

- 新增独立 LED 模块，后续由 4G 控制逻辑调用，不把 GPIO 操作散落在 `m100pg.c` 中。
- LED 模块至少支持关闭、红、绿、蓝和一个云端控制用的开关状态。
- LED 模块必须暴露状态读取接口，供 4G 上传帧填充 `led` 字段。
- 不使用 PA8，除非明确决定牺牲 DHT11。
- GPIO 配置必须以 `helmet.ioc` 为源头；若新增引脚，应由 CubeMX 生成后再在 APP 层调用。
- PB12/PB13/PB14 配置为 GPIO Output，默认输出低电平，避免上电默认点亮。
- 共阴极逻辑：输出 `GPIO_PIN_SET` 点亮对应颜色，输出 `GPIO_PIN_RESET` 熄灭对应颜色。
- 调试阶段新增 `rgb_led_task()`，由调度器每 1000ms 调用一次，在白色和关闭之间切换。

## Acceptance Criteria

- [x] 三色 LED 模块有独立 `.c/.h` 文件，并纳入 `APP/bsp_system.h` 与 Keil 工程。
- [x] 可通过函数调用设置 LED 关闭、红、绿、蓝。
- [x] 可读取当前 LED 状态用于 4G 上传。
- [x] 已注册 `rgb_led_task()` 到调度器，支持 1 秒间隔白色闪烁测试。
- [ ] LED 控制不影响 DHT11 的 PA8 单总线。
- [ ] 后续 `m100pg` 只通过 LED 模块接口控制 LED，不直接操作 LED GPIO。

## Definition of Done

- Keil 编译通过，无新增 warning。
- APP 代码满足头文件守卫、UTF-8 无 BOM、LF 行尾。
- 串口/实机验证三路颜色和关闭状态。
- README/CLAUDE/硬件资源记录同步更新新增 LED 引脚。

## Out of Scope

- 不实现 PWM 调光、呼吸灯、渐变动画。
- 不实现 WS2812 单线时序驱动，除非硬件确认不是普通 RGB LED。
- 不在本子任务内实现 4G 上传封包和下发解析，只提供可调用 LED 接口。

## Technical Notes

- 当前 GPIO 证据：`Core/Src/gpio.c` 初始化 PB12/PB13/PB14 为 `GPIO_MODE_OUTPUT_PP`，默认 `GPIO_PIN_RESET`。
- 当前 CubeMX 证据：`helmet.ioc` 中 PB12/PB13/PB14 已配置为 GPIO 输出。
- PA8 冲突证据：`APP/dht11.h` 定义 `DHT11_PORT GPIOA`、`DHT11_PIN GPIO_PIN_8`。
- 2026-04-29 接线决策：R=PB12，G=PB13，B=PB14，公共端接地，共阴极。
- 2026-04-29 实现：新增 `APP/rgb_led.c/.h`，接口包含 `rgb_led_init()`、`rgb_led_task()`、`rgb_led_set_color()`、`rgb_led_set_enabled()`、`rgb_led_get_color()`、`rgb_led_get_enabled()`。
- 2026-04-29 内聚调整：`rgb_led.c` 顶部集中定义 `RGB_LED_*` 端口/引脚映射，直接声明 PB12/PB13/PB14，业务函数不使用 CubeMX 生成的 `led_*` 标签名。
