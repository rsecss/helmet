# MPU6050 姿态解算：移除 InvenSense DMP，改用自实现 Mahony 滤波

## 背景与动机

仓库拟以 public + 保留权利 + 免责的方式公开。当前 `APP/mpu6050_inv_mpu*.{c,h}`
（含 3062 字节 DMP 固件 `dmp_memory[]`）为 InvenSense 专有代码（`All Rights
Reserved`），其许可禁止公开再分发，是仓库唯一的法律风险源。移除该依赖，改用
在 MCU 上自实现的 Mahony 姿态解算滤波，使仓库 100% 无 InvenSense 代码。

## 方案

- 姿态解算：**Mahony 互补滤波**（PI 反馈 + 四元数），纯 6 轴（加速度+陀螺），
  与 MPU6050 无磁力计配置匹配。
- 积分步长：**固定 dt = 0.01f（10ms）**，对齐 `mpu6050_task` 调度周期。
- 寄存器初始化：照 MPU6050 公开寄存器手册自写 `PWR_MGMT_1` / `SMPLRT_DIV` /
  `CONFIG`（DLPF）/ `GYRO_CONFIG` / `ACCEL_CONFIG`，不复制 InvenSense 代码结构。

## 删除文件（移除 InvenSense 专有代码）

- `APP/mpu6050_inv_mpu.c` / `.h`
- `APP/mpu6050_inv_mpu_dmp_motion_driver.c` / `.h`

## 修改文件

- `APP/mpu6050.c`：重写 `mpu6050_init()`（自写寄存器配置）、姿态解算
  （`mpu6050_dmp_get_data` → Mahony），删除 DMP 相关辅助函数与自检。
- `APP/mpu6050.h`：调整姿态解算函数声明，补充寄存器宏；对外接口
  （`pitch/roll/yaw`、`aacx..`、`AVM/GVM`、`fall_flag/collision_flag`、
  报警查询接口）保持不变。
- `APP/bsp_system.h`：删除第 40–41 行两个 InvenSense 头 include。
- `MDK-ARM/helmet.uvprojx`：移除 4 个 InvenSense 文件的工程登记。

## 不变的契约（下游零改动，验收依据）

- 全局符号 `pitch/roll/yaw`（float）、`aacx/aacy/aacz`、`gyrox/gyroy/gyroz`
  （short）、`AVM/GVM`（uint16_t）、`fall_flag/collision_flag`（bool）名称与
  类型不变。
- 报警查询接口 `mpu6050_is_ready/is_fall_alarm/is_collision_alarm/
  get_alarm_flags/clear_alarm` 签名不变。
- 消费方 `m100pg_bsp.c`、`m100pg_protocol.c`、`lcd_app.c`、`helmet_alarm.c`、
  `scheduler.c` 不改动。
- fall/collision 状态机（吃 AVM/GVM/pitch/roll）逻辑不变。

## 已知取舍

- 纯 6 轴 yaw 无磁力计绝对参考，会缓慢漂移。yaw 仅用于遥测/LCD 显示，无任何
  报警逻辑依赖，影响可接受。pitch/roll 有重力参考不漂移，fall 检测精度不受影响。

## 验收标准

- 仓库内不再有任何 `Copyright ... InvenSense` 代码。
- `grep -ri invensense APP/` 无结果，`grep -r dmp_ APP/` 无结果。
- Keil F7 编译零警告（需人工在 Keil 中验证，本环境无法编译 STM32 工程）。
- 上电后 pitch/roll 随姿态变化正确、静止稳定；fall/collision 报警可触发可恢复
  （需人工实机验证）。

---

## 当前状态（2026-07-16，未提交，工作区改动）

### 已完成
- 4 个 InvenSense 文件已删除，`grep` 验证 APP/ 零残留。
- `mpu6050.c/.h`、`bsp_system.h`、`helmet.uvprojx`、README/CLAUDE/CHANGELOG 均已改。
- Mahony 姿态解算 + 陀螺零偏上电校准 + 运行期静止自校准均已落地。
- 已实机烧录验证：**pitch/roll 正常稳定（0~3°）**。

### 本次续接修复（2026-07-18）
- 根因：上电校准返回 `void`，失败后仍置 ready；运行期静止判定又依赖当前零偏，
  当零偏无效且任一轴原始偏置超过阈值时会形成无法进入重校准的循环。
- 将寄存器配置状态与零偏有效状态拆开；`mpu6050_is_ready()` 仅在两者均有效时返回 1。
- 上电校准显式返回成功/失败；失败后周期任务继续采样，但零偏有效前不输出姿态和报警。
- 运行期恢复改为 2 秒窗口峰峰值判稳，不再依赖无效零偏；首次恢复后重置 Mahony 状态。
- 已有有效零偏时，候选变化超过约 3dps 会被拒绝，避免把明显旋转吸收为零偏。
- 禁止固化单块芯片的实测偏置，保持更换 MPU6050 后仍可自校准。
- GCC 主机桩以 `-std=c99 -Wall -Wextra -Werror` 编译并通过以下回归：
  - 无效零偏 + 常量 raw 偏置超过旧阈值时，200 个稳定样本后可恢复；
  - 扣偏后静止更新 1000 次，yaw 保持接近 0；
  - 明显旋转不覆盖已有零偏；运动窗口不会误置零偏有效。

### 待实机验证
- Keil F7 Build 零警告。
- 静止放置 10 秒，确认 yaw 不再保持约 -1.5°/s 的固定速率漂移。
- 移动状态上电导致初始校准失败后再静置，确认约 2 秒后 ready 恢复并开始输出姿态。
- 回归 pitch/roll 方向、fall/collision 触发与恢复。

### 提交注意
- 提交前跑 `Keilkill.bat` 或用 `git add` 精确选文件，排除 `MDK-ARM/helmet/`、
  `DebugConfig/`、`*.lst` 等编译产物。
- 建议提交信息：`refactor(mpu6050): 移除 InvenSense DMP，改用自实现 Mahony 姿态解算`
