# 步进电机控制模块 - 使用文档

## 1. 概述

本模块提供基于串口UART协议的42步进电机控制功能，支持4轴电机控制，可实现写字机的写字和绘图功能。

### 1.1 模块组成

| 文件 | 说明 |
|------|------|
| `stepper_motor.h` | 步进电机驱动头文件，定义电机控制API |
| `stepper_motor.c` | 步进电机驱动实现，封装串口通信协议 |
| `plotter.h` | 写字机控制头文件，定义绘图API |
| `plotter.c` | 写字机控制实现，提供图形绘制功能 |

### 1.2 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                     应用层 (Application)                    │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              plotter.c / plotter.h                    │  │
│  │  - 抬笔/落笔控制   - 坐标移动   - 图形绘制             │  │
│  └───────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                    驱动层 (Driver)                          │
│  ┌───────────────────────────────────────────────────────┐  │
│  │           stepper_motor.c / stepper_motor.h           │  │
│  │  - 电机使能   - 速度模式   - 位置模式   - 状态读取     │  │
│  └───────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                    硬件层 (Hardware)                        │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  ESP32-P4 UART1  →  42步进电机驱动器 x 4              │  │
│  │  TX: GPIO12    RX: GPIO13                              │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. 硬件连接

### 2.1 串口接线

根据串口UART控制步进电机使用流程指南，接线方式如下：

| ESP32-P4 | 步进电机驱动器 | 说明 |
|----------|---------------|------|
| GPIO12 (UART1_TX) | R/A/H | 串口发送 |
| GPIO13 (UART1_RX) | T/B/L | 串口接收 |
| GND | GND | 共地 |
| VCC (7-32V) | V+ | 电源正极 |

### 2.2 电机地址分配

| 电机ID | 功能 | 轴标识 |
|--------|------|--------|
| 1 | X轴 - 横向移动 | MOTOR_ID_X |
| 2 | Y轴 - 纵向移动 | MOTOR_ID_Y |
| 3 | Z轴 - 笔尖升降 | MOTOR_ID_Z |
| 4 | A轴 - 预留扩展 | MOTOR_ID_A |

---

## 3. 步进电机驱动 API

### 3.1 初始化函数

```c
void stepper_motor_init(uart_port_t uart_num, int tx_pin, int rx_pin);
```

**功能**：初始化步进电机控制的UART串口

**参数解析**：
- `uart_num`：UART端口号（如 `UART_NUM_1`）
- `tx_pin`：TX引脚编号（如 `GPIO_NUM_12`）
- `rx_pin`：RX引脚编号（如 `GPIO_NUM_13`）

**调用示例**：
```c
stepper_motor_init(UART_NUM_1, GPIO_NUM_12, GPIO_NUM_13);
```

---

### 3.2 电机使能控制

```c
esp_err_t motor_enable(uint8_t motor_id, bool enable);
```

**功能**：使能或失能指定电机

**参数解析**：
- `motor_id`：电机地址（1-4）
- `enable`：`true` 使能，`false` 失能

**调用示例**：
```c
motor_enable(MOTOR_ID_X, true);   // 使能X轴电机
motor_enable(MOTOR_ID_Y, true);   // 使能Y轴电机
motor_enable(MOTOR_ID_Z, true);   // 使能Z轴电机
motor_enable(MOTOR_ID_A, true);   // 使能A轴电机
```

---

### 3.3 电机停止

```c
esp_err_t motor_stop(uint8_t motor_id);
```

**功能**：立即停止指定电机

**参数解析**：
- `motor_id`：电机地址（1-4）

**调用示例**：
```c
motor_stop(MOTOR_ID_X);  // 停止X轴电机
```

---

### 3.4 速度模式控制

```c
esp_err_t motor_velocity_mode(uint8_t motor_id, motor_direction_t dir, uint16_t speed_rpm, uint8_t accel);
```

**功能**：设置电机以恒定速度运行

**参数解析**：
| 参数 | 类型 | 说明 |
|------|------|------|
| `motor_id` | uint8_t | 电机地址（1-4） |
| `dir` | motor_direction_t | 方向：`DIRECTION_CW`(顺时针) / `DIRECTION_CCW`(逆时针) |
| `speed_rpm` | uint16_t | 转速（RPM），范围0-65535 |
| `accel` | uint8_t | 加速度档位（0-255），0表示无加减速 |

**调用示例**：
```c
motor_velocity_mode(MOTOR_ID_X, DIRECTION_CW, 1000, 10);  // X轴顺时针1000RPM，加速度10档
```

---

### 3.5 位置模式控制

```c
esp_err_t motor_position_mode(uint8_t motor_id, motor_direction_t dir, uint16_t speed_rpm, uint8_t accel, int32_t pulses, position_mode_t mode);
```

**功能**：控制电机移动指定脉冲数

**参数解析**：
| 参数 | 类型 | 说明 |
|------|------|------|
| `motor_id` | uint8_t | 电机地址（1-4） |
| `dir` | motor_direction_t | 方向 |
| `speed_rpm` | uint16_t | 转速（RPM） |
| `accel` | uint8_t | 加速度档位（0-255） |
| `pulses` | int32_t | 脉冲数，32位有符号整数 |
| `mode` | position_mode_t | `POS_MODE_RELATIVE`(相对位置) / `POS_MODE_ABSOLUTE`(绝对位置) |

**调用示例**：
```c
// X轴逆时针移动1500RPM，10档加速，32000脉冲，相对位置模式
motor_position_mode(MOTOR_ID_X, DIRECTION_CCW, 1500, 10, 32000, POS_MODE_RELATIVE);
```

**脉冲数计算**：
- 16细分下，3200脉冲 = 1圈
- 移动距离(mm) = 脉冲数 / PULSES_PER_MM

---

### 3.6 状态读取

```c
esp_err_t motor_read_status(uint8_t motor_id, uint8_t *status);
esp_err_t motor_read_position(uint8_t motor_id, int32_t *pos);
esp_err_t motor_read_speed(uint8_t motor_id, int16_t *speed);
```

**功能**：读取电机状态、位置、速度

**参数解析**：
- `motor_id`：电机地址
- `status/pos/speed`：输出参数指针

**状态标志位解析**：
```c
Bit0: 电机使能状态 (1=使能)
Bit1: 电机到位标志 (1=已到位)
Bit2: 电机堵转标志 (1=堵转)
Bit3: 堵转保护标志 (1=已触发保护)
```

**调用示例**：
```c
uint8_t status;
int32_t pos;
int16_t speed;

motor_read_status(MOTOR_ID_X, &status);
motor_read_position(MOTOR_ID_X, &pos);
motor_read_speed(MOTOR_ID_X, &speed);

if (status & 0x02) {
    // 电机已到位
}
```

---

### 3.7 原点与回零

```c
esp_err_t motor_set_zero(uint8_t motor_id, bool save);
esp_err_t motor_homing(uint8_t motor_id, homing_mode_t mode);
esp_err_t motor_clear_position(uint8_t motor_id);
```

**功能**：设置零点、触发回零、清除位置

**参数解析**：
- `save`：`true` 保存零点到EEPROM，`false` 临时设置
- `mode`：回零模式

**回零模式说明**：
| 模式 | 值 | 说明 |
|------|-----|------|
| HOMING_MODE_NEAREST | 0x00 | 单圈就近回零 |
| HOMING_MODE_DIRECTION | 0x01 | 单圈方向回零 |
| HOMING_MODE_COLLISION | 0x02 | 多圈无限位碰撞回零 |
| HOMING_MODE_LIMIT_SWITCH | 0x03 | 多圈有限位开关回零 |

**调用示例**：
```c
motor_clear_position(MOTOR_ID_X);        // 清除X轴位置
motor_set_zero(MOTOR_ID_X, true);        // 设置零点并保存
motor_homing(MOTOR_ID_X, HOMING_MODE_NEAREST);  // 触发就近回零
```

---

### 3.8 多机同步控制

```c
esp_err_t motor_sync_trigger(void);
```

**功能**：触发所有电机同时开始运动（广播命令）

**使用流程**：
1. 向各电机发送带同步标志的控制命令
2. 电机收到命令后缓存，不立即执行
3. 发送同步触发命令，所有电机同时开始运动

**调用示例**：
```c
// 发送带同步标志的命令（实际命令中需设置同步标志位）
// ...

motor_sync_trigger();  // 触发同步运动
```

---

## 4. 多电机并行控制 API（新增）

### 4.1 概述

多电机并行控制系统为每个电机创建独立的 FreeRTOS 任务，实现真正意义上的并行运动控制：

- **独立运动**：每个电机有独立的命令队列和状态轮询任务
- **信号隔离**：电机仅响应自身驱动器返回的到位信号
- **并行执行**：X/Y/Z 轴同时启动，不等待其他电机完成
- **完成通知**：使用 EventGroup 实现多电机完成信号同步

### 4.2 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层 (Application)                      │
│  motor_move_submit() → 提交运动命令到各电机队列              │
│  motor_wait_done()   → 等待多个电机同时完成                  │
├─────────────────────────────────────────────────────────────┤
│               并行控制层 (Parallel Control)                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐          │
│  │  mt1 (X轴)  │  │  mt2 (Y轴)  │  │  mt3 (Z轴)  │          │
│  │ 命令队列    │  │ 命令队列    │  │ 命令队列    │          │
│  │ 状态轮询    │  │ 状态轮询    │  │ 状态轮询    │          │
│  │ 完成标志    │  │ 完成标志    │  │ 完成标志    │          │
│  └─────────────┘  └─────────────┘  └─────────────┘          │
│                    ↓          ↓          ↓                  │
│              s_motor_done_evt (EventGroup)                  │
├─────────────────────────────────────────────────────────────┤
│                    驱动层 (Driver)                           │
│  motor_position_mode() ← s_uart_mutex 保护                  │
│  motor_read_status_fast() ← 快速状态查询                    │
├─────────────────────────────────────────────────────────────┤
│                    硬件层 (Hardware)                         │
│  ESP32-P4 UART2  →  42步进电机驱动器 x 4                    │
│  TX: GPIO11    RX: GPIO12                                   │
└─────────────────────────────────────────────────────────────┘
```

### 4.3 初始化

```c
esp_err_t motor_tasks_init(void);
```

**功能**：创建 4 个 per-motor FreeRTOS 任务和完成事件组

**调用示例**：
```c
stepper_motor_init(UART_NUM_2, GPIO_NUM_11, GPIO_NUM_12);
motor_tasks_init();  // 必须在 stepper_motor_init 之后调用
```

---

### 4.4 提交运动命令（异步）

```c
esp_err_t motor_move_submit(uint8_t motor_id, motor_direction_t dir,
                            uint16_t speed_rpm, uint8_t accel,
                            int32_t pulses, pos_mode_t mode);
```

**功能**：将运动命令提交到指定电机的命令队列（非阻塞）

**参数解析**：与 `motor_position_mode` 相同

**返回值**：
- `ESP_OK`：命令成功入队
- `ESP_FAIL`：队列满，命令被拒绝

**调用示例**：
```c
// 同时提交 X 和 Y 轴运动命令
motor_move_submit(MOTOR_ID_X, DIRECTION_CW, 200, 50, 1600, POS_MODE_RELATIVE);
motor_move_submit(MOTOR_ID_Y, DIRECTION_CW, 200, 50, 1600, POS_MODE_RELATIVE);
// 两个电机立即开始并行运动！
```

---

### 4.5 等待电机完成

```c
esp_err_t motor_wait_done(uint32_t motor_mask, uint32_t timeout_ms);
```

**功能**：等待指定电机组合完成运动（阻塞）

**参数解析**：
| 参数 | 类型 | 说明 |
|------|------|------|
| `motor_mask` | uint32_t | 电机掩码组合 |
| `timeout_ms` | uint32_t | 最大等待时间（毫秒） |

**电机掩码定义**：
```c
#define MOTOR_MASK_X   0x01  // X轴
#define MOTOR_MASK_Y   0x02  // Y轴
#define MOTOR_MASK_Z   0x04  // Z轴
#define MOTOR_MASK_A   0x08  // A轴
```

**返回值**：
- `ESP_OK`：所有指定电机已完成
- `ESP_FAIL`：超时，部分电机未完成

**调用示例**：
```c
// 等待 X 和 Y 轴同时完成
motor_wait_done(MOTOR_MASK_X | MOTOR_MASK_Y, 5000);

// 只等待 Z 轴完成
motor_wait_done(MOTOR_MASK_Z, 3000);

// 等待所有电机完成
motor_wait_done(MOTOR_MASK_X | MOTOR_MASK_Y | MOTOR_MASK_Z, 10000);
```

---

### 4.6 清除完成标志

```c
void motor_clear_done(uint32_t motor_mask);
```

**功能**：清除指定电机的完成标志（用于下一次运动前）

**调用示例**：
```c
motor_clear_done(MOTOR_MASK_X | MOTOR_MASK_Y);
motor_move_submit(MOTOR_ID_X, ...);
motor_move_submit(MOTOR_ID_Y, ...);
motor_wait_done(MOTOR_MASK_X | MOTOR_MASK_Y, 5000);
```

---

### 4.7 查询运动状态

```c
bool motor_is_moving(uint8_t motor_id);
```

**功能**：查询指定电机是否正在运动

**返回值**：
- `true`：电机正在运动
- `false`：电机空闲

**调用示例**：
```c
if (motor_is_moving(MOTOR_ID_X)) {
    // X轴正在运动
}
```

---

### 4.8 完整使用流程

```c
void parallel_move_demo(void)
{
    // 1. 初始化
    stepper_motor_init(UART_NUM_2, GPIO_NUM_11, GPIO_NUM_12);
    motor_tasks_init();
    
    // 2. 使能电机
    motor_enable(MOTOR_ID_X, true);
    motor_enable(MOTOR_ID_Y, true);
    motor_enable(MOTOR_ID_Z, true);
    
    // 3. 清除完成标志
    motor_clear_done(MOTOR_MASK_X | MOTOR_MASK_Y);
    
    // 4. 同时提交 X 和 Y 轴命令
    motor_move_submit(MOTOR_ID_X, DIRECTION_CW, 200, 50, 1600, POS_MODE_RELATIVE);
    motor_move_submit(MOTOR_ID_Y, DIRECTION_CW, 200, 50, 1600, POS_MODE_RELATIVE);
    
    // 5. 等待两个电机都完成
    motor_wait_done(MOTOR_MASK_X | MOTOR_MASK_Y, 5000);
    
    printf("X and Y both reached!\n");
}
```

---

### 4.9 与 GCode 控制器集成

`writer_controller.c` 的 `motor_move_callback` 已自动使用异步 API：

```c
// G1 X30 Y10 F500 执行时：
motor_clear_done(MOTOR_MASK_X | MOTOR_MASK_Y);
motor_move_submit(MOTOR_ID_X, x_dir, speed, accel, pulses_x, POS_MODE_RELATIVE);
motor_move_submit(MOTOR_ID_Y, y_dir, speed, accel, pulses_y, POS_MODE_RELATIVE);
motor_wait_done(MOTOR_MASK_X | MOTOR_MASK_Y, 15000);
```

X 和 Y 轴会**同时启动**，各自独立轮询到位状态，完成时设置 EventGroup 位。

---

## 5. 写字机控制 API

### 5.1 初始化

```c
void plotter_init(uint16_t default_speed, uint8_t default_accel);
void plotter_set_home(float x, float y, float z);
```

**功能**：初始化写字机，设置默认速度和原点位置

**参数解析**：
- `default_speed`：默认速度（RPM）
- `default_accel`：默认加速度档位
- `x/y/z`：原点坐标（mm）

**调用示例**：
```c
plotter_init(500, 10);     // 初始化，速度500RPM，加速度10档
plotter_set_home(0, 0, 0); // 设置原点在(0,0,0)
```

---

### 5.2 抬笔/落笔控制

```c
void plotter_pen_up(void);
void plotter_pen_down(void);
```

**功能**：控制笔尖抬起或落下

**原理**：通过控制Z轴电机移动实现笔尖升降

**调用示例**：
```c
plotter_pen_up();    // 抬起笔尖（移动到上方）
plotter_pen_down();  // 落下笔尖（接触纸张）
```

---

### 5.3 坐标移动

```c
void plotter_move_to(float x, float y, float z);
void plotter_move_relative(float dx, float dy, float dz);
```

**功能**：移动到指定坐标位置

**参数解析**：
- `x/y/z`：目标绝对坐标（mm）
- `dx/dy/dz`：相对位移（mm）

**调用示例**：
```c
plotter_move_to(50, 30, 0);      // 移动到绝对坐标(50,30,0)
plotter_move_relative(10, 0, 0); // 相对当前位置X轴移动10mm
```

**坐标系统说明**：
- X轴：横向移动（从左到右为正）
- Y轴：纵向移动（从前到后为正）
- Z轴：笔尖升降（向上为正）

---

### 5.4 图形绘制

#### 5.4.1 绘制直线

```c
void plotter_draw_line(float x1, float y1, float x2, float y2);
```

**功能**：绘制从(x1,y1)到(x2,y2)的直线

**调用示例**：
```c
plotter_draw_line(10, 10, 100, 50);  // 绘制从(10,10)到(100,50)的直线
```

#### 5.4.2 绘制矩形

```c
void plotter_draw_rectangle(float x, float y, float width, float height);
```

**功能**：绘制矩形

**参数解析**：
- `x/y`：矩形左上角坐标
- `width`：宽度
- `height`：高度

**调用示例**：
```c
plotter_draw_rectangle(20, 20, 80, 50);  // 绘制80x50的矩形
```

#### 5.4.3 绘制圆形

```c
void plotter_draw_circle(float cx, float cy, float radius);
```

**功能**：绘制圆形

**参数解析**：
- `cx/cy`：圆心坐标
- `radius`：半径（mm）

**调用示例**：
```c
plotter_draw_circle(60, 40, 25);  // 绘制圆心在(60,40)，半径25mm的圆
```

#### 4.4.4 绘制三角形

```c
void plotter_draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3);
```

**功能**：绘制三角形

**参数解析**：
- `(x1,y1)/(x2,y2)/(x3,y3)`：三个顶点坐标

**调用示例**：
```c
plotter_draw_triangle(20, 100, 70, 150, 120, 100);  // 绘制三角形
```

---

### 4.5 字符绘制

```c
void plotter_draw_char(float x, float y, char c, float size);
void plotter_draw_string(float x, float y, const char *str, float size);
```

**功能**：绘制单个字符或字符串

**参数解析**：
- `x/y`：字符左下角坐标
- `c`：字符
- `str`：字符串
- `size`：字符大小（mm）

**支持字符**：
- 'A' - 大写字母A
- 'B' - 大写字母B
- 'C' - 大写字母C
- 其他字符显示为方块

**调用示例**：
```c
plotter_draw_char(20, 180, 'A', 20);       // 绘制单个字符A
plotter_draw_string(20, 200, "ABC", 20);  // 绘制字符串"ABC"
```

---

### 4.6 归位

```c
void plotter_home(void);
```

**功能**：抬笔并返回原点位置

**调用示例**：
```c
plotter_home();  // 返回原点
```

---

## 5. 使用流程示例

### 5.1 完整绘图流程

```c
#include "stepper_motor.h"
#include "plotter.h"

void plotter_demo(void)
{
    // 1. 初始化电机
    stepper_motor_init(UART_NUM_1, GPIO_NUM_12, GPIO_NUM_13);
    
    // 2. 初始化写字机
    plotter_init(500, 10);
    plotter_set_home(0, 0, 0);
    
    // 3. 绘制图形
    plotter_draw_rectangle(20, 20, 100, 60);   // 矩形
    plotter_draw_circle(70, 50, 25);           // 圆形
    plotter_draw_triangle(20, 100, 70, 150, 120, 100);  // 三角形
    plotter_draw_line(20, 170, 120, 170);      // 直线
    plotter_draw_string(20, 190, "ABC", 20);   // 字符串
    
    // 4. 返回原点
    plotter_home();
}
```

### 5.2 自定义绘图示例

```c
void draw_square(float x, float y, float size)
{
    plotter_pen_up();
    plotter_move_to(x, y, 0);
    plotter_pen_down();
    
    plotter_move_to(x + size, y, 0);      // 右
    plotter_move_to(x + size, y + size, 0); // 下
    plotter_move_to(x, y + size, 0);      // 左
    plotter_move_to(x, y, 0);             // 上
    
    plotter_pen_up();
}
```

---

## 6. 配置参数

### 6.1 脉冲-毫米转换系数

```c
#define PULSES_PER_MM_X   80.0f   // X轴每毫米脉冲数
#define PULSES_PER_MM_Y   80.0f   // Y轴每毫米脉冲数
#define PULSES_PER_MM_Z   40.0f   // Z轴每毫米脉冲数
```

**根据实际机械结构调整**：
- 16细分下，步进电机每转3200脉冲
- 若丝杆导程为4mm，则 PULSES_PER_MM = 3200 / 4 = 800

### 6.2 笔尖位置

```c
#define PEN_UP_POS        0       // 抬笔位置（相对）
#define PEN_DOWN_POS      50      // 落笔深度（脉冲数）
```

---

## 7. 注意事项

### 7.1 电气安全
- 禁止带电拔插接线
- 电源电压范围：7-32V DC
- 确保所有设备共地

### 7.2 运动控制
- 高速运动建议开启加减速（accel > 0）
- 位置控制需等待到位标志置位
- 紧急情况使用 `motor_stop()` 停止

### 7.3 通讯规范
- 波特率：115200（需与驱动器一致）
- 连续发送指令间隔建议 > 10ms
- 广播地址（0）发送时仅地址1设备回复

### 7.4 调试建议
1. 先使用串口助手调试，确认通讯正常
2. 低速测试运动功能，验证方向和步数
3. 逐步提高速度，观察运行平稳性
4. 记录关键参数，便于问题排查

---

## 8. 故障排查

| 问题现象 | 可能原因 | 解决方法 |
|----------|----------|----------|
| 串口无响应 | TX/RX接反、波特率不匹配、地址错误 | 检查接线、确认波特率、核对设备地址 |
| 电机不转动 | 未使能、堵转保护触发、电源不足 | 发送使能命令、解除堵转保护、检查电源 |
| 电机抖动/异响 | 编码器未校准、电流设置过小 | 重新校准编码器、增大工作电流 |
| 位置不准 | 丢步、机械间隙、加速度过大 | 使用闭环模式、消除机械间隙、降低加速度 |

---

**文档版本**：V1.0  
**适用平台**：ESP32-P4 + Emm42_V5.0 步进电机驱动器  
**更新日期**：2026年5月18日