# SmartHelm — 智能头盔

[![Quality Gate](https://github.com/rsecss/helmet/actions/workflows/quality.yml/badge.svg)](https://github.com/rsecss/helmet/actions/workflows/quality.yml)

基于 STM32F103C8T6 的开源智能头盔嵌入式项目。

## 硬件平台

| 参数 | 规格 |
|------|------|
| MCU | STM32F103C8T6 (Cortex-M3) |
| 主频 | 72 MHz (HSE 8MHz + PLL ×9) |
| Flash / SRAM | 64 KB / 20 KB |
| 封装 | LQFP48 |

## 项目结构

```
Core/           CubeMX 生成的外设初始化、中断、时钟配置
APP/            应用层业务逻辑（调度器、BSP 等）
Drivers/        STM32 HAL 库 + CMSIS
MDK-ARM/        Keil 工程文件 + 启动文件
helmet.ioc      STM32CubeMX 工程配置
```

## 开发环境

- **IDE**: Keil MDK-ARM V5.32
- **代码生成**: STM32CubeMX 6.17.0
- **固件包**: STM32Cube FW_F1 V1.8.7
- **烧录工具**: J-Link / ST-Link（SWD 接口）

## 快速开始

1. 使用 Keil 打开 `MDK-ARM/helmet.uvprojx`
2. Build（F7）编译工程
3. 通过 SWD 下载至目标板

## 外设使用

| 外设 | 引脚 | 用途 |
|------|------|------|
| USART1 | PA9 (TX) / PA10 (RX) | ASRPro 天问离线语音模块，115200-8N1，默认纯语音串口 |
| ADC1 | PA0 | MQ2 烟雾传感器模拟量采集（DMA 循环采样） |
| TIM1 | 内部时钟 | 微秒级延时（预分频 72-1，1MHz 计数） |
| TIM3_CH1 | PB4 | TB6612FNG A 通道 PWMA，小风扇 PWM 调速（部分重映射） |
| GPIO | PA8 | DHT11 温湿度传感器数据线（推挽输出） |
| GPIO | PA11 / PA12 / PB15 | TB6612FNG AIN2 / AIN1 / STBY |
| GPIO | PB12 (R) / PB13 (G) / PB14 (B) | 共阴三色 LED，低电平默认熄灭 |
| I2C1 | PB6 (SCL) / PB7 (SDA) | MPU6050 六轴传感器通信（Fast Mode 400kHz） |
| I2C2 | PB10 (SCL) / PB11 (SDA) | MAX30102 心率血氧传感器通信（Fast Mode 400kHz） |
| USART2 | PA2 (TX) / PA3 (RX) | M100PG 4G DTU 透传通信，115200-8N1，RX 使用 DMA 空闲中断 |
| SWD | PA13 / PA14 | 程序下载与调试 |

## 功能模块

| 模块 | 源文件 | 说明 |
|------|--------|------|
| 任务调度器 | `APP/scheduler.c` | 基于 SysTick 的毫秒级轮询协作调度 |
| MQ2 烟雾传感器 | `APP/mq2.c` | ADC+DMA 采集，计算气体浓度（ppm），100ms 周期 |
| DHT11 温湿度传感器 | `APP/dht11.c` | 单总线时序通信，读取温度和湿度，1000ms 周期 |
| MPU6050 六轴传感器 | `APP/mpu6050.c` | I2C 通信 + DMP 姿态解算（pitch/roll/yaw），跌倒确认与激烈碰撞报警，10ms 周期 |
| MAX30102 心率血氧传感器 | `APP/max30102.c` | I2C2 通信，FIFO 轮询读取 Red/IR，PBA 心跳检测 + AC/DC 比值算 SpO2，50ms 周期 |
| M100PG 4G DTU | `APP/m100pg.c` / `APP/m100pg_protocol.c` | USART2 DMA 空闲接收，1000ms 上传传感器帧（含 fall/collision 报警状态），解析 `LED_ON/OFF/WHITE/RED/GREEN` 并可转发到 USART1 调试口 |
| ASRPro 离线语音模块 | `APP/asrpro.c` | USART1 单字节中断接收 `led_on`、`led_off`、`motor_speed_0..3`，默认关闭 USART1 调试输出避免干扰语音模块 |
| 三色 LED | `APP/rgb_led.c` | PB12/PB13/PB14 控制共阴 RGB LED，默认关闭，供云端 LED 颜色控制 |
| 本地安全报警 | `APP/helmet_alarm.c` | 读取 MPU6050 报警状态，报警后 RGB 红灯至少快闪 15s，解除后恢复云端下发颜色 |
| PWM 电机驱动 | `APP/pwm_motor.c` | TIM3_CH1/PB4 控制 TB6612FNG A 通道 PWMA，公开接口提供调速、转向和停止 |
| ST7735 彩色显示屏 | `APP/st7735.c` | GPIO 软件 SPI，PB0=SCL、PA7=SDA、PB1=DC，提供低耦合清屏、矩形填充、ASCII 字符串绘制接口 |
| LCD 数据显示应用 | `APP/lcd_app.c` | 200ms 脏刷新 6 行传感器数据：温度、湿度、烟雾浓度、心率、血氧、姿态/报警状态 |
| 位带操作 | `APP/sys.h` | GPIO 位带操作宏，支持单 IO 口读写 |

## 软件架构

采用裸机轮询调度模型，`APP/scheduler.c` 实现基于 SysTick 的毫秒级协作调度器，主循环中周期性执行各任务函数。

ASRPro 语音模块默认占用 USART1：ASR_TX 接 MCU PA10，ASR_RX 接 MCU PA9。语音固件输出固定文本命令，STM32 端仅匹配小写命令词并兼容 `\r\n` 与首尾空白。LED 命令通过 `helmet_alarm_set_base_led()` 设置基础灯态，跌倒/碰撞报警红灯保持优先级；电机命令 `motor_speed_0..3` 映射到 0%、33%、66%、100%。如需临时恢复 USART1 调试，可在 `APP/asrpro.h` 中打开 `ASRPRO_ENABLE_USART1_DEBUG` 或关闭 `ASRPRO_ENABLE_COMMAND_EXECUTION` 后重新编译。
