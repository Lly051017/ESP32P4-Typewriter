# GCode 运动控制模块

本模块为 ESP32-P4 提供轻量级 GCode 解析器和运动控制系统，从 FluidNC 提取并简化，适用于写字机项目。

**版本**：V2.0（2026-05-29）- 新增多电机并行控制支持

---

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层                                    │
│  writer_example.c - 演示和测试函数                           │
│  writer_test.c    - 单元测试                                │
├─────────────────────────────────────────────────────────────┤
│  writer_controller.c/h - 高层写字机控制                      │
│  - 抬笔/落笔控制                                            │
│  - 直线和圆弧绘制                                           │
│  - 状态回调                                                 │
│  - GCode 命令队列                                           │
│  - motor_move_callback() → 并行电机控制                      │
├─────────────────────────────────────────────────────────────┤
│  motion_control.c/h - 运动执行                              │
│  - 直线插补                                                │
│  - 圆弧插补 (G2/G3)                                         │
│  - 延时命令                                                │
├─────────────────────────────────────────────────────────────┤
│  motion_planner.c/h - 轨迹规划                              │
│  - 加减速曲线                                               │
│  - 转角速度计算                                             │
│  - 块缓冲区管理                                            │
├─────────────────────────────────────────────────────────────┤
│  gcode_parser.c/h - GCode 解析                              │
│  - G0/G1/G2/G3 运动命令                                     │
│  - G17/G18/G19 平面选择                                    │
│  - G20/G21 单位设置                                        │
│  - G90/G91 距离模式                                        │
│  - F 进给速度、X/Y/Z 坐标                                   │
│  - I/J/K 圆弧偏移                                          │
├─────────────────────────────────────────────────────────────┤
│  stepper_motor.c/h - 多电机并行控制                         │
│  - motor_tasks_init() - 创建 per-motor FreeRTOS 任务        │
│  - motor_move_submit() - 提交命令到电机队列                  │
│  - motor_wait_done() - 通过 EventGroup 等待完成             │
│  - mt1(X), mt2(Y), mt3(Z), mt4(A) - 独立任务               │
└─────────────────────────────────────────────────────────────┘
```

---

## 多电机并行控制（新增）

### 概述

V2.0 架构实现了真正的并行电机控制：

- **独立任务**：每个电机有自己的 FreeRTOS 任务（mt1-mt4）
- **命令队列**：每个电机有专用的命令队列
- **信号隔离**：电机只响应自己驱动器的状态
- **并行执行**：X/Y/Z 同时启动，不等待其他电机

### 执行流程

```
writer_execute_gcode("G1 X30 Y10 F500")
  │
  ↓
g_command_queue (FreeRTOS 队列)
  │
  ↓
writer_task() [后台任务]
  │
  ↓
gcode_parse_line() → gcode_block_t
  │
  ↓
process_gcode_block() → mc_linear()
  │
  ↓
motor_move_callback()
  │
  ├─ motor_clear_done(X|Y|Z)
  ├─ motor_move_submit(X, ...) → X队列 ──→ mt1任务 ──→ 发送位置命令
  ├─ motor_move_submit(Y, ...) → Y队列 ──→ mt2任务 ──→ 发送位置命令
  ├─ motor_move_submit(Z, ...) → Z队列 ──→ mt3任务 ──→ 发送位置命令
  │                                            │
  │                                            ↓
  │                                   各任务独立轮询 status&0x02
  │                                            │
  │                                            ↓
  │                                   到位 → 设置 EventGroup 位
  │
  ↓
motor_wait_done(X|Y|Z, 15000) ← 等待所有完成位
  │
  ↓
返回 → writer_wait_idle() 返回
```

### 关键特性

| 特性 | 说明 |
|------|------|
| **真正并行** | X 和 Y 同时移动，不是顺序执行 |
| **独立完成** | 每个电机独立发出完成信号 |
| **无阻塞** | 电机任务独立运行，不等待其他电机 |
| **事件驱动** | 使用 FreeRTOS EventGroup 完成信号通知 |

---

## 支持的 GCode 命令

| 命令 | 描述 | 参数 |
|------|------|------|
| G0 | 快速定位 | X, Y, Z |
| G1 | 直线插补 | X, Y, Z, F |
| G2 | 顺时针圆弧 | X, Y, Z, I, J, K, F |
| G3 | 逆时针圆弧 | X, Y, Z, I, J, K, F |
| G4 | 延时 | P（秒） |
| G17 | XY 平面选择 | - |
| G18 | ZX 平面选择 | - |
| G19 | YZ 平面选择 | - |
| G20 | 英寸单位 | - |
| G21 | 毫米单位 | - |
| G90 | 绝对定位 | - |
| G91 | 增量定位 | - |

---

## 快速入门指南

### 步骤 1：包含头文件

```c
#include "writer_controller.h"
#include "stepper_motor.h"
```

### 步骤 2：初始化硬件

```c
// 初始化电机 UART
stepper_motor_init(UART_NUM_2, GPIO_NUM_11, GPIO_NUM_12);

// 初始化多电机并行控制任务（新增！）
motor_tasks_init();  // 创建 mt1(X), mt2(Y), mt3(Z), mt4(A) 任务
```

### 步骤 3：配置写字机

```c
writer_config_t config = {
    .steps_per_mm = {80.0f, 80.0f, 40.0f},        // X, Y, Z 每毫米脉冲数
    .max_rate_mm_min = {3000.0f, 3000.0f, 1000.0f}, // 最大速度
    .acceleration_mm_s2 = {500.0f, 500.0f, 200.0f}, // 加速度
    .max_travel_mm = {200.0f, 200.0f, 50.0f},      // 最大行程
    .default_feed_rate = 500.0f,                    // 默认速度
    .rapid_rate = 3000.0f,                          // 快速移动速度
    .pen_up_pos = 5.0f,                             // 抬笔高度
    .pen_down_pos = 0.0f,                           // 落笔高度
    .pen_lift_delay_ms = 100,                       // 抬笔延时
    .motor_ids = {MOTOR_ID_X, MOTOR_ID_Y, MOTOR_ID_Z}, // 电机ID
    .invert_dir = {false, false, false},            // 方向反转
};

writer_init(&config);
```

### 步骤 4：执行 GCode 命令

```c
// 命令之间必须等待空闲！
writer_execute_gcode("G21");           // 设置单位为毫米
writer_wait_idle();

writer_execute_gcode("G90");           // 绝对定位
writer_wait_idle();

writer_execute_gcode("G0 Z5");         // 抬笔
writer_wait_idle();

writer_execute_gcode("G0 X10 Y10");    // 移动到起始位置
writer_wait_idle();

writer_execute_gcode("G1 Z0 F500");    // 落笔
writer_wait_idle();

writer_execute_gcode("G1 X90 Y10 F300"); // 画线
writer_wait_idle();

writer_execute_gcode("G0 Z5");         // 抬笔
writer_wait_idle();
```

---

## 重要使用注意事项

### 1. 必须等待空闲

**关键**：每个 GCode 命令后必须调用 `writer_wait_idle()`：

```c
// 正确 - 等待完成
writer_execute_gcode("G1 X100 Y100");
writer_wait_idle();  // 等待电机到达位置

// 错误 - 会导致命令队列溢出
writer_execute_gcode("G1 X100 Y100");
writer_execute_gcode("G1 X0 Y0");  // 可能被忽略或导致错误
```

### 2. 电机 ID 配置

电机 ID 必须与硬件配置匹配：

| 轴 | 默认 ID | 描述 |
|------|---------|------|
| X | 1 | X轴电机（左右移动） |
| Y | 2 | Y轴电机（前后移动） |
| Z | 3 | Z轴电机（笔尖升降） |

```c
// 在 writer_config_t 中配置电机 ID
.motor_ids = {1, 2, 3},  // X=1, Y=2, Z=3
```

### 3. 每毫米脉冲数

根据电机和滑轮系统计算：

```c
// 示例：1.8° 步进电机，16细分，20齿滑轮，2mm节距皮带
// 每转步数 = 200 * 16 = 3200
// 每转毫米数 = 20 * 2 = 40mm
// steps_per_mm = 3200 / 40 = 80
.steps_per_mm = {80.0f, 80.0f, 40.0f},
```

### 4. 进给速度注意事项

- 最大进给速度受 `max_rate_mm_min` 限制
- 进给速度自动转换为电机 RPM
- Z轴速度自动降低为 X/Y 速度的 50%

---

## API 参考

### writer_controller.h

| 函数 | 描述 |
|------|------|
| `writer_init()` | 使用配置初始化写字机 |
| `writer_deinit()` | 反初始化写字机 |
| `writer_execute_gcode()` | 执行 GCode 字符串 |
| `writer_wait_idle()` | 阻塞直到所有命令完成 |
| `writer_draw_line()` | 绘制两点之间的直线 |
| `writer_draw_circle()` | 绘制圆形 |
| `writer_pen_up()` | 抬笔 |
| `writer_pen_down()` | 落笔 |
| `writer_get_status()` | 获取当前状态 |

### writer_example.h

| 函数 | 描述 |
|------|------|
| `writer_example_run_demo()` | 运行完整演示序列 |
| `writer_example_draw_square()` | 绘制正方形图案 |
| `writer_example_draw_circle()` | 绘制圆形图案 |

---

## 完整示例

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"
#include "stepper_motor.h"
#include "writer_controller.h"

void writing_task(void *arg)
{
    // 等待硬件就绪
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 初始化电机 UART
    stepper_motor_init(UART_NUM_2, GPIO_NUM_11, GPIO_NUM_12);
    
    // 初始化并行电机控制（新增！）
    motor_tasks_init();
    
    // 配置写字机
    writer_config_t config = {
        .steps_per_mm = {80.0f, 80.0f, 40.0f},
        .max_rate_mm_min = {3000.0f, 3000.0f, 1000.0f},
        .acceleration_mm_s2 = {500.0f, 500.0f, 200.0f},
        .max_travel_mm = {200.0f, 200.0f, 50.0f},
        .default_feed_rate = 500.0f,
        .rapid_rate = 3000.0f,
        .pen_up_pos = 5.0f,
        .pen_down_pos = 0.0f,
        .pen_lift_delay_ms = 100,
        .motor_ids = {1, 2, 3},
        .invert_dir = {false, false, false},
    };
    
    writer_init(&config);
    
    // 绘制正方形 (80x80mm) - X 和 Y 同时移动！
    writer_execute_gcode("G21");               writer_wait_idle();
    writer_execute_gcode("G90");               writer_wait_idle();
    writer_execute_gcode("G0 Z5");             writer_wait_idle();
    writer_execute_gcode("G0 X10 Y10");        writer_wait_idle();  // X+Y 并行
    writer_execute_gcode("G1 Z0 F500");        writer_wait_idle();
    writer_execute_gcode("G1 X90 Y10 F300");   writer_wait_idle();  // X+Y 并行
    writer_execute_gcode("G1 X90 Y90");        writer_wait_idle();  // X+Y 并行
    writer_execute_gcode("G1 X10 Y90");        writer_wait_idle();  // X+Y 并行
    writer_execute_gcode("G1 X10 Y10");        writer_wait_idle();  // X+Y 并行
    writer_execute_gcode("G0 Z5");             writer_wait_idle();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```