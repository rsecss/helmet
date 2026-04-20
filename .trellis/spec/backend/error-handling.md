# Error Handling

> How errors are handled in this project.

---

## Overview

感知层无操作系统，错误处理采用**返回值分级 + 本地降级**的策略，核心原则：

- **致命错误不阻塞主循环**：单个外设故障不应导致整机宕机。
- **事件优先**：健康/安全相关错误（跌倒、烟雾超阈）必须在本地可靠上报，网络不可用时降级为本地蜂鸣器/LED 告警。
- **错误码离散化**：模块内部错误使用 `uint8_t` 返回码（0=成功，非 0=具体错误），便于上层判断。

当前文档覆盖范围：**感知层错误处理**。传输层协议错误码、应用层 HTTP 错误响应在对应层代码落地时再扩展。

---

## Error Types

感知层错误分三层：

### 1. HAL 层错误：`HAL_StatusTypeDef`（ST 官方定义）

```c
typedef enum {
    HAL_OK      = 0x00U,
    HAL_ERROR   = 0x01U,
    HAL_BUSY    = 0x02U,
    HAL_TIMEOUT = 0x03U,
} HAL_StatusTypeDef;
```

**调用约定**：所有 HAL 函数必须检查返回值，失败时走降级分支，不要强行重试。

### 2. 模块层错误：`uint8_t` 返回码（项目约定）

```c
/* 示例：APP/mpu6050.c */
uint8_t mpu6050_dmp_get_data(float *p, float *r, float *y)
{
    if (dmp_read_fifo(...)) return 1;           // FIFO 读取失败
    if (!(sensors & INV_WXYZ_QUAT)) return 2;   // 姿态数据无效
    /* ... */
    return 0;                                   // 成功
}
```

**错误码语义由模块自定义**，建议：
- `0` — 成功
- `1~9` — 可恢复错误（如总线繁忙、数据暂无效）
- `10+` — 硬件级异常

模块应在 `.h` 注释中说明错误码含义。

### 3. 断言失败：`assert_param`（调试构建）

`USE_FULL_ASSERT` 宏启用时，HAL 参数断言会调用 `assert_failed()`（`Core/Src/main.c`）。默认实现为空，发布构建建议关闭此宏。

---

## Error Handling Patterns

### 模式 1：HAL 调用立即检查

```c
if (HAL_I2C_Mem_Read(&hi2c1, (MPU6050_ADDR << 1), MPU6050_ACCEL_XOUTH_REG,
                     I2C_MEMADD_SIZE_8BIT, buf, 6, HAL_MAX_DELAY) != HAL_OK)
    return 1;   // 传播错误，不阻塞调度器
```

### 模式 2：初始化阶段失败静默返回（降级可用）

MPU6050 初始化中任一步失败直接 `return`，不阻塞 `main()` 后续其他外设启动：

```c
void mpu6050_init(void)
{
    if (mpu_init(&int_param) != 0) return;
    if (mpu_set_sensors(INV_XYZ_GYRO | INV_XYZ_ACCEL)) return;
    /* ... */
    run_self_test();   // 自检失败不阻塞，仅影响校准精度
    mpu_set_dmp_state(1);
}
```

**原则**：单个传感器失败不应使整机无法运行，任务调度仍可处理其他传感器。

### 模式 3：Error_Handler — 系统级不可恢复错误（CubeMX 生成）

```c
void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}
```

**触发场景**：HSE 振荡器启动失败、Flash 参数配置失败等时钟/硬件初始化级错误。仅由 CubeMX 生成代码调用，**应用层禁止直接调用 `Error_Handler()`**。

### 模式 4：事件优先降级策略（预期扩展）

当前未实现，但规划如下：

```
异常检测（跌倒/烟雾超阈/心率异常）
    ↓
本地蜂鸣器 + LED（立即）
    ↓
UART 上报网关（本地可靠）
    ↓
网关负责上云（网络不可用时本地告警仍有效）
```

**规则**：事件的本地告警必须独立于上云链路，即使网络/DTU 故障也能工作。

---

## API Error Responses

感知层不直接对外提供 API，错误表现为两类"对外响应"：

### 1. 对上层模块：返回码

模块函数用 `uint8_t` 返回错误码，上层（调度器 / `main`）根据返回码决定是否重试或降级。

### 2. 对网关 / 上位机：UART 事件帧（待协议定稿）

事件型错误（跌倒、烟雾超阈）应通过 UART 发送结构化事件帧到网关。**帧协议在传输层规范中定义**（尚未编写），当前通过 `printf` 调试输出占位。

### 3. 对用户：本地声光告警（待硬件接入）

蜂鸣器 + LED 驱动尚未实现，规划由事件标志位（如 `fall_flag`）驱动。

---

## Common Mistakes

1. **忽略 HAL 返回值**：`HAL_I2C_Mem_Read(...)` 不检查返回值 → I2C 总线繁忙或设备拔出时数据为脏值，误判姿态。**所有 HAL 调用必须检查返回值**。

2. **在中断中进入死循环或长时间等待**：中断回调必须快速返回（微秒级），`HAL_MAX_DELAY` 仅可在主循环/任务中使用，中断中应用非阻塞轮询或直接丢帧。

3. **初始化失败不处理继续往下用**：例如 MPU6050 init 失败仍调用 `mpu6050_task`，会读取脏数据。扩展时应在模块内部维护 `mpu6050_ready` 标志，任务开始前判断。

4. **应用层直接调用 `Error_Handler()`**：`Error_Handler` 会关中断死循环，导致整机瘫痪。应用层错误应使用模块返回码，让调度器继续运行其他任务。

5. **assert_failed 未实现导致调试困难**：启用 `USE_FULL_ASSERT` 时，应在 `assert_failed` 内加入 `printf("assert: %s:%d\r\n", file, line)` 方便定位，发布构建前再关闭断言。

6. **错误码混用**：HAL 层用 `HAL_StatusTypeDef`、模块层用 `uint8_t`，不要把 HAL 的 `HAL_ERROR`（0x01）直接传给期望模块错误码的上层，必须转换或分离处理。
