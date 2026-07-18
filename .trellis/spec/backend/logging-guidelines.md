# Logging Guidelines

> How logging is done in this project.

---

## Overview

感知层使用 `printf` 重定向到 **USART1（PA9 TX / PA10 RX，115200-8N1）** 作为唯一日志通道。

- **重定向实现**：`Core/Src/usart.c` 的 `fputc()` 通过 `HAL_UART_Transmit` 同步发送单字节
- **用途**：开发/调试期的文本输出；不用于与网关的结构化通信（后者使用 UART2 + 自定义帧协议）
- **无日志框架**：项目资源受限（20 KB SRAM），不引入 log4c 等库，以约定取代框架

当前文档覆盖范围：**感知层调试输出**。传输层的事件帧协议、应用层的云端日志在对应层代码落地时再扩展。

---

## Log Levels

项目尚未实现分级日志宏，但新模块开发应遵循以下**概念级**分级，必要时包装为宏。

| 级别 | 用途 | 频率 | 示例 |
|------|------|------|------|
| `ERROR` | 不可恢复错误、硬件异常 | 稀有事件 | `printf("[ERR] mpu6050 init failed\r\n")` |
| `WARN`  | 可恢复异常、降级发生 | 偶发 | `printf("[WRN] ringbuffer full, dropping data\r\n")` |
| `INFO`  | 关键状态变化、模块就绪 | 低频 | `printf("[INFO] mpu6050 ready\r\n")` |
| `DEBUG` | 调试数据、原始传感器值 | 高频可能 | `printf("[DBG] pitch=%.1f roll=%.1f\r\n", pitch, roll)` |

**推荐扩展**（未强制落地）：

```c
/* 建议放在 bsp_system.h 或独立 log.h */
#define LOG_LEVEL_DEBUG  3
#define LOG_LEVEL_INFO   2
#define LOG_LEVEL_WARN   1
#define LOG_LEVEL_ERROR  0

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#define LOG_E(fmt, ...) do { if (LOG_LEVEL >= LOG_LEVEL_ERROR) printf("[ERR] " fmt "\r\n", ##__VA_ARGS__); } while (0)
#define LOG_W(fmt, ...) do { if (LOG_LEVEL >= LOG_LEVEL_WARN)  printf("[WRN] " fmt "\r\n", ##__VA_ARGS__); } while (0)
#define LOG_I(fmt, ...) do { if (LOG_LEVEL >= LOG_LEVEL_INFO)  printf("[INFO] " fmt "\r\n", ##__VA_ARGS__); } while (0)
#define LOG_D(fmt, ...) do { if (LOG_LEVEL >= LOG_LEVEL_DEBUG) printf("[DBG] " fmt "\r\n", ##__VA_ARGS__); } while (0)
```

发布构建前将 `LOG_LEVEL` 改为 `LOG_LEVEL_WARN` 或更低，减少串口占用。

---

## Structured Logging

嵌入式场景下的"结构化"指**人可读的固定字段格式**，不是 JSON/protobuf。

**推荐格式**：

```
[LEVEL] <module>: <key>=<value> [key=value ...]
```

示例：

```c
printf("[INFO] mpu6050: pitch=%.1f roll=%.1f yaw=%.1f\r\n", pitch, roll, yaw);
printf("[WRN] ringbuffer: overflow count=%u\r\n", overflow_count);
```

**字段约定**：

- **行尾统一 `\r\n`**：适配串口终端软件（MobaXterm、XCOM 等）
- **一行一条日志**：不在同一条日志中换行
- **浮点保留 1~2 位小数**：`%.1f`、`%.2f`，避免噪声位
- **十六进制带 `0x` 前缀**：`0x%02X`
- **无字符串转义**：不要在日志里塞入含 `\r\n` 的用户数据

---

## What to Log

### 启动阶段（INFO）
- 各模块初始化完成
- 关键参数（时钟频率、Mahony 固定步长、传感器采样率）
- 陀螺零偏校准是否完成

### 运行时（WARN / ERROR）
- 外设返回非 HAL_OK
- I2C 读取失败或传感器从 ready 退化为 unavailable
- 环形缓冲区满 / 空导致的数据丢弃
- I2C / UART 总线错误

### 事件检测（WARN / INFO）
- 跌倒检测触发（`fall_flag = true`）
- 烟雾浓度超阈
- 心率/血氧异常
- GPS 定位首次成功 / 失效

### 调试期（DEBUG，发布前移除或降级）
- 每次采样的原始传感器值
- 周期性状态（pitch/roll/yaw 连续输出）

---

## What NOT to Log

### 性能与稳定性禁区

1. **中断回调中禁止 `printf`**：`HAL_UART_Transmit` 是阻塞调用，会拖长中断时间、丢帧。如需在中断中记录，应设置标志位让任务打印。

2. **实时路径的高频循环**：10ms 周期任务每次都 `printf` 会占满 USART1 带宽（115200 ≈ 11 KB/s）。示例：`mpu6050_task` 现有的 `printf("pitch:... roll:... yaw:...")` 应在稳定后改为 DEBUG 级或去除。

3. **for 循环内逐元素打印**：超过 ~10 行的数据应汇总打印（例如缓冲区 dump 用 hex 格式压缩），不要逐项 `printf`。

### 内容禁区

4. **敏感数据**：GPS 精确坐标（调试时可加扰动）、网络凭据、设备唯一标识。

5. **未初始化的指针/缓冲区内容**：可能输出不可打印字符干扰串口终端，甚至触发终端转义序列。

6. **固件版本之外的编译信息**：避免把完整构建路径、编译时间暴露在发布固件中。

### 代码约定

7. **禁止裸 `printf("调试完记得删")`**：测试性打印必须在合并前清除，或包在 `#ifdef DEBUG` 下。

8. **不要在 `Core/`（CubeMX 生成）中添加 `printf`**：`Core/` 文件由 CubeMX 重新生成会覆盖。所有日志应在 `APP/` 模块或 `USER CODE BEGIN/END` 块内。
