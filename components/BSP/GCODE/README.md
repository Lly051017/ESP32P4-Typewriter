# GCode Motion Control Module

This module provides a lightweight GCode parser and motion control system for ESP32P4, extracted and simplified from FluidNC for the writing machine project.

**Version**: V2.0 (2026-05-29) - Added multi-motor parallel control support

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  writer_example.c - Demo and test functions                  │
│  writer_test.c    - Unit tests                              │
├─────────────────────────────────────────────────────────────┤
│  writer_controller.c/h - High-level writing machine control  │
│  - Pen up/down control                                       │
│  - Line and arc drawing                                      │
│  - Status callbacks                                          │
│  - GCode command queue                                       │
│  - motor_move_callback() → Parallel motor control            │
├─────────────────────────────────────────────────────────────┤
│  motion_control.c/h - Motion execution                       │
│  - Linear interpolation                                      │
│  - Arc interpolation (G2/G3)                                 │
│  - Dwell commands                                           │
├─────────────────────────────────────────────────────────────┤
│  motion_planner.c/h - Trajectory planning                    │
│  - Acceleration/deceleration profiles                        │
│  - Junction speed calculation                               │
│  - Block buffer management                                  │
├─────────────────────────────────────────────────────────────┤
│  gcode_parser.c/h - GCode parsing                            │
│  - G0/G1/G2/G3 motion commands                              │
│  - G17/G18/G19 plane selection                              │
│  - G20/G21 units                                            │
│  - G90/G91 distance mode                                    │
│  - F feed rate, X/Y/Z coordinates                           │
│  - I/J/K arc offsets                                        │
├─────────────────────────────────────────────────────────────┤
│  stepper_motor.c/h - Multi-motor parallel control            │
│  - motor_tasks_init() - Create per-motor FreeRTOS tasks      │
│  - motor_move_submit() - Submit command to motor queue       │
│  - motor_wait_done() - Wait for completion via EventGroup    │
│  - mt1(X), mt2(Y), mt3(Z), mt4(A) - Independent tasks        │
└─────────────────────────────────────────────────────────────┘
```

---

## Multi-Motor Parallel Control (NEW)

### Overview

The V2.0 architecture implements true parallel motor control:

- **Independent Tasks**: Each motor has its own FreeRTOS task (mt1-mt4)
- **Command Queues**: Each motor has a dedicated command queue
- **Signal Isolation**: Motors only respond to their own driver status
- **Parallel Execution**: X/Y/Z start simultaneously, no waiting for others

### Execution Flow

```
writer_execute_gcode("G1 X30 Y10 F500")
  │
  ↓
g_command_queue (FreeRTOS Queue)
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

### Key Benefits

| Feature | Description |
|---------|-------------|
| **True Parallelism** | X and Y move simultaneously, not sequentially |
| **Independent Completion** | Each motor signals completion independently |
| **No Blocking** | Motor tasks run independently, don't wait for others |
| **Event-driven** | Uses FreeRTOS EventGroup for completion signaling |

---

## Supported GCodes

| Code | Description | Parameters |
|------|-------------|------------|
| G0 | Rapid positioning | X, Y, Z |
| G1 | Linear interpolation | X, Y, Z, F |
| G2 | Clockwise arc | X, Y, Z, I, J, K, F |
| G3 | Counter-clockwise arc | X, Y, Z, I, J, K, F |
| G4 | Dwell | P (seconds) |
| G17 | XY plane selection | - |
| G18 | ZX plane selection | - |
| G19 | YZ plane selection | - |
| G20 | Inch units | - |
| G21 | Millimeter units | - |
| G90 | Absolute positioning | - |
| G91 | Incremental positioning | - |

---

## Quick Start Guide

### Step 1: Include Headers

```c
#include "writer_controller.h"
#include "stepper_motor.h"
```

### Step 2: Initialize Hardware

```c
// Initialize UART for stepper motors
stepper_motor_init(UART_NUM_1, GPIO_NUM_13, GPIO_NUM_12);
```

### Step 3: Configure Writer

```c
writer_config_t config = {
    .steps_per_mm = {80.0f, 80.0f, 40.0f},        // X, Y, Z steps/mm
    .max_rate_mm_min = {3000.0f, 3000.0f, 1000.0f}, // Max speed
    .acceleration_mm_s2 = {500.0f, 500.0f, 200.0f}, // Acceleration
    .max_travel_mm = {200.0f, 200.0f, 50.0f},      // Max travel
    .default_feed_rate = 500.0f,                    // Default speed
    .rapid_rate = 3000.0f,                          // Rapid speed
    .pen_up_pos = 5.0f,                             // Pen up height
    .pen_down_pos = 0.0f,                           // Pen down height
    .pen_lift_delay_ms = 100,                       // Lift delay
    .motor_ids = {MOTOR_ID_X, MOTOR_ID_Y, MOTOR_ID_Z}, // Motor IDs
    .invert_dir = {false, false, false},            // Invert direction
};

writer_init(&config);
```

### Step 4: Execute GCode Commands

```c
// Always wait for idle between commands!
writer_execute_gcode("G21");           // Set units to mm
writer_wait_idle();

writer_execute_gcode("G90");           // Absolute positioning
writer_wait_idle();

writer_execute_gcode("G0 Z5");         // Pen up
writer_wait_idle();

writer_execute_gcode("G0 X10 Y10");    // Move to start position
writer_wait_idle();

writer_execute_gcode("G1 Z0 F500");    // Pen down
writer_wait_idle();

writer_execute_gcode("G1 X90 Y10 F300"); // Draw line
writer_wait_idle();

writer_execute_gcode("G0 Z5");         // Pen up
writer_wait_idle();
```

---

## Important Usage Notes

### 1. Always Wait for Idle

**Critical**: Always call `writer_wait_idle()` after each GCode command:

```c
// CORRECT - Wait for completion
writer_execute_gcode("G1 X100 Y100");
writer_wait_idle();  // Wait until motor reaches position

// WRONG - Will cause command queue overflow
writer_execute_gcode("G1 X100 Y100");
writer_execute_gcode("G1 X0 Y0");  // May be ignored or cause errors
```

### 2. Motor IDs Configuration

The motor IDs must match your hardware configuration:

| Axis | Default ID | Description |
|------|------------|-------------|
| X | 1 | X-axis motor (left-right) |
| Y | 2 | Y-axis motor (forward-backward) |
| Z | 3 | Z-axis motor (pen lift) |

```c
// Configure motor IDs in writer_config_t
.motor_ids = {1, 2, 3},  // X=1, Y=2, Z=3
```

### 3. Steps per Millimeter

Calculate based on your motor and pulley system:

```c
// Example: 1.8° stepper, 16x microstep, 20T pulley, 2mm pitch belt
// Steps per revolution = 200 * 16 = 3200
// mm per revolution = 20 * 2 = 40mm
// steps_per_mm = 3200 / 40 = 80
.steps_per_mm = {80.0f, 80.0f, 40.0f},
```

### 4. Feed Rate Considerations

- Maximum feed rate is limited by `max_rate_mm_min`
- Feed rate is automatically converted to motor RPM
- Z-axis speed is automatically reduced to 50% of X/Y speed

---

## API Reference

### writer_controller.h

| Function | Description |
|----------|-------------|
| `writer_init()` | Initialize writer with configuration |
| `writer_deinit()` | Deinitialize writer |
| `writer_execute_gcode()` | Execute a GCode string |
| `writer_wait_idle()` | Block until all commands complete |
| `writer_draw_line()` | Draw a line between two points |
| `writer_draw_circle()` | Draw a circle |
| `writer_pen_up()` | Lift pen |
| `writer_pen_down()` | Lower pen |
| `writer_get_status()` | Get current status |

### writer_example.h

| Function | Description |
|----------|-------------|
| `writer_example_run_demo()` | Run complete demo sequence |
| `writer_example_draw_square()` | Draw square pattern |
| `writer_example_draw_circle()` | Draw circle pattern |

---

## Complete Example

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"
#include "stepper_motor.h"
#include "writer_controller.h"

void writing_task(void *arg)
{
    // Wait for hardware
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Initialize UART for motors
    stepper_motor_init(UART_NUM_1, GPIO_NUM_13, GPIO_NUM_12);
    
    // Configure writer
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
    
    // Draw a square (80x80mm)
    writer_execute_gcode("G21");               writer_wait_idle();
    writer_execute_gcode("G90");               writer_wait_idle();
    writer_execute_gcode("G0 Z5");             writer_wait_idle();
    writer_execute_gcode("G0 X10 Y10");        writer_wait_idle();
    writer_execute_gcode("G1 Z0 F500");        writer_wait_idle();
    writer_execute_gcode("G1 X90 Y10 F300");   writer_wait_idle();
    writer_execute_gcode("G1 X90 Y90");        writer_wait_idle();
    writer_execute_gcode("G1 X10 Y90");        writer_wait_idle();
    writer_execute_gcode("G1 X10 Y10");        writer_wait_idle();
    writer_execute_gcode("G0 Z5");             writer_wait_idle();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    uart_init(UART_NUM_0, 115200, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreate(&writing_task, "writing_task", 4096, NULL, 5, NULL);
}
```

---

## Debugging Tips

### 1. Check Motor Status

```c
uint8_t status;
if (motor_read_status(MOTOR_ID_X, &status) == ESP_OK) {
    uart0_printf("Motor X: enabled=%d, reached=%d\n", 
                 (status&0x01), (status&0x02)>>1);
}
```

### 2. Verify Command Execution

The module provides detailed debug output when enabled:

```
[Motor] Current: X=0.0 Y=0.0 Z=5.0
[Motor] Target: X=100.0 Y=50.0 Z=5.0
[Motor] Delta: dx=100.00 dy=50.00 dz=0.00
[Motor] Pulses: X=8000 Y=4000 Z=0
[Motor] Moving X axis: motor_id=1, pulses=8000
[Motor] X axis command result: 0
[Motor] Moving Y axis: motor_id=2, pulses=4000
[Motor] Y axis command result: 0
[Motor] Waiting for X axis...
[Motor] X axis reached
[Motor] Waiting for Y axis...
[Motor] Y axis reached
[Motor] Move completed
```

### 3. Common Issues

| Issue | Possible Cause | Solution |
|-------|---------------|----------|
| Motor not moving | Command queue not processed | Call `writer_wait_idle()` |
| Motor moves wrong direction | Direction inverted | Set `invert_dir` in config |
| Motor moves too fast/slow | Incorrect steps_per_mm | Recalculate steps/mm |
| Motor stalls | Acceleration too high | Reduce `acceleration_mm_s2` |
| Position inaccurate | Steps/mm incorrect | Calibrate steps/mm |

---

## Files

| File | Description |
|------|-------------|
| gcode_parser.h/c | GCode parsing and state management |
| motion_planner.h/c | Trajectory planning and acceleration |
| motion_control.h/c | Motion execution and arc interpolation |
| writer_controller.h/c | High-level writing machine control |
| writer_example.h/c | Demo and test functions |
| writer_test.h/c | Unit tests |

---

## See Also

- [STEPPER Module](../STEPPER/README.md) - Low-level motor driver
- [main.c](../../main/main.c) - Example application
