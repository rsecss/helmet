# MAX30102 心率血氧传感器模块

## Goal

在 SmartHelm 现有框架中新增 MAX30102 心率/血氧传感器驱动模块，通过硬件 I2C1 总线采集 Red/IR 光电数据并解算 HR（心率 bpm）与 SpO2（血氧饱和度 %），以现有传感器模块（MPU6050/DHT11/MQ2）一致的风格集成至调度器。

## What I already know

- I2C1 已在 CubeMX 中配置为 Fast Mode 400 kHz（PB6/PB7），当前由 MPU6050 (0x68) 独占使用。
- MAX30102 7-bit I2C 地址固定为 **0x57**（写 0xAE / 读 0xAF），与 MPU6050 **不冲突**，可共享 I2C1 总线。
- 项目模块模板已成熟：`APP/<module>.{c,h}` + `bsp_system.h` 登记 + `scheduler.c` 注册周期任务 + `main.c` 初始化。
- 现有驱动全部使用 `HAL_I2C_Mem_Read/Write(&hi2c1, addr<<1, reg, ...)` 阻塞 API，非 DMA。
- 输出风格一致为 `printf` 走 USART1（115200）。
- STM32F103C8T6 资源：64 KB Flash / 20 KB SRAM（心率算法需要环形缓冲区，典型 100~500 个样本的 int32，需评估内存占用）。
- MAX30102 芯片 Part ID 寄存器（0xFF）= 0x15，可用于上电自检。
- MAX30102 VLED 需 3.3V、VDD 需 1.8V，通常模块板自带 LDO（硬件责任，软件不关心）。

## Assumptions (temporary)

- 采用 **轮询 FIFO + 调度器周期任务** 的风格（与 MPU6050 一致），不启用 MAX30102 的 INT 引脚，避免改动 CubeMX 引脚分配。
- 采样率 100 Hz（SMP_AVE=1，SPO2_SR=100），FIFO 几乎满时的轮询周期应 ≤ 50 ms 以防溢出（32 深 FIFO × 1 sample = 320 ms 满，但建议 50 ms 读一次留余量）。
- HR/SpO2 算法采用 Maxim 官方参考实现（MAXREFDES117 精简版，约 4~6 KB Flash）。
- 算法更新周期约 1 秒（每累积 100 个样本解算一次）。

## Decision (ADR-lite)

**Context**: 需决定 MAX30102 的 I2C 总线归属与 FIFO 读取触发机制。

**Decision**:
1. **独立 I2C2 总线**（PB10/PB11，Fast Mode 400 kHz，Duty cycle 2），不与 MPU6050 共用 I2C1。CubeMX 已配置并生成代码。
2. **纯轮询方案（A）**：调度器每 50 ms 读一次 FIFO，不接 MAX30102 的 INT 脚。

**Consequences**:
- 利：总线隔离减少相互阻塞风险；无需改动 `.ioc` 再引入额外 GPIO/EXTI；与 MPU6050/DHT11/MQ2 现有轮询风格完全一致。
- 弊：多占用 I2C2 外设与 PB10/PB11 两个引脚；100 Hz 采样下每 50 ms 产生约 30 字节 I2C 读（总线 400 kHz 下可忽略）。
- 未来如需低功耗或高采样率，可再接 INT 脚升级为 B 方案，驱动接口保持兼容。

## Requirements (evolving)

- 新增 `APP/max30102.{c,h}`，通过 I2C1 总线与 MAX30102 通讯。
- 提供 `max30102_init()`：上电自检（读 Part ID=0x15 验证连接）、寄存器配置（SPO2 模式、100Hz 采样、LED 电流、FIFO）。
- 提供 `max30102_task()`：周期读取 FIFO Red/IR 数据 → 更新心率/血氧。
- 提供全局变量（extern）：`heart_rate`（bpm, int32）、`spo2`（%, int32）、`hr_valid` / `spo2_valid`（uint8_t）。
- 集成到 `scheduler_task[]` 与 `main.c` 初始化流程。
- 登记至 `bsp_system.h`。
- 更新 Keil `helmet.uvprojx` 添加源文件。
- 更新 `CLAUDE.md` 与 `README.md` 的外设/模块章节。

## Acceptance Criteria (evolving)

- [ ] 冷启动后 2 秒内能通过 I2C 读到 Part ID = 0x15（自检通过）。
- [ ] 手指放置 10 秒内，串口能持续稳定输出合理的 HR（40~180）和 SpO2（85~100）。
- [ ] MPU6050 姿态解算、DHT11、MQ2 功能不受 I2C 总线共享影响（所有串口输出保持正常）。
- [ ] CI 质量门禁通过（cppcheck、头文件守卫、UTF-8 无 BOM、LF）。
- [ ] 手指离开时 `hr_valid=0`，不输出错误的心率。

## Definition of Done

- [ ] 代码通过 Keil MDK 编译无 warning。
- [ ] 通过 CI 质量门禁（`.github/workflows/quality.yml`）。
- [ ] 实机串口验证 HR/SpO2 在手指贴合时数值可信。
- [ ] `bsp_system.h`、`scheduler.c`、`main.c`、`helmet.uvprojx` 同步更新。
- [ ] `CLAUDE.md`（外设表格、架构章节）与 `README.md`（功能模块）同步更新。
- [ ] Trellis session 通过 `add_session.py` 记录。

## Out of Scope (explicit)

- 不新增 I2C2 或软件 I2C。
- 不引入外部第三方库（例如 Arduino SparkFun 库），算法代码内嵌。
- 不做跌倒+心率的融合逻辑。
- 不做蓝牙/无线上报。
- 不做 OLED 显示。

## Technical Notes

- 可参考 Maxim 官方 **MAXREFDES117#** 参考设计中的 `algorithm.c`（自相关峰值检测 + 比值解算 SpO2），License 为 Maxim 自家宽松许可。
- FIFO 每样本 6 字节（Red 3B + IR 3B，18-bit）。读取需使用 `HAL_I2C_Mem_Read` burst。
- 寄存器地址常用：`FIFO_WR_PTR=0x04`、`FIFO_RD_PTR=0x06`、`FIFO_DATA=0x07`、`MODE=0x09`、`SPO2=0x0A`、`LED1_PA=0x0C`（Red）、`LED2_PA=0x0D`（IR）、`PART_ID=0xFF`。
- 典型配置：MODE=0x03（SpO2 模式），SPO2=0x27（100Hz，18-bit，4.7ms 脉宽），LED 电流 0x24（约 7 mA）。

## Research Notes

### 相似方案对比

- **MAXREFDES117 官方算法**：Maxim 参考实现，自相关峰值检测心率 + 经验公式 SpO2，约 400~500 行 C，成熟可靠，几乎所有 STM32/Arduino 开源工程的基石。
- **DFRobot / SparkFun Arduino 库**：包装官方算法 + C++ OO 封装，结构相对重，不适合直接移植到裸 C 嵌入式。
- **自研滑窗峰检测**：代码量最小（~100 行）但临床精度差、抗运动干扰弱。

### 项目约束

- STM32F103 资源紧张（64KB Flash / 20KB SRAM），需要尽量减小算法内存占用。
- 已有 MPU6050 DMP 固件占用不小 Flash，需注意剩余空间。
- 必须纯 C（Keil MDK-ARM，armcc/armclang）。
