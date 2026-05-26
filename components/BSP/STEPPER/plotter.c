#include "plotter.h"
#include "math.h"
#include "uart.h"

static plotter_t g_plotter;

void plotter_init(uint16_t default_speed, uint8_t default_accel)
{
    uart0_printf("[Plotter] Initializing plotter...\n");
    
    g_plotter.current_pos.x = 0.0f;
    g_plotter.current_pos.y = 0.0f;
    g_plotter.current_pos.z = 0.0f;
    g_plotter.home_pos.x = 0.0f;
    g_plotter.home_pos.y = 0.0f;
    g_plotter.home_pos.z = 0.0f;
    g_plotter.pen_down = false;
    g_plotter.speed = default_speed;
    g_plotter.accel = default_accel;
    
    esp_err_t ret;
    ret = motor_enable(MOTOR_ID_X, true);
    uart0_printf("[Plotter] Enable X motor(%d): %s\n", MOTOR_ID_X, ret == ESP_OK ? "SUCCESS" : "FAILED");
    
    ret = motor_enable(MOTOR_ID_Y, true);
    uart0_printf("[Plotter] Enable Y motor(%d): %s\n", MOTOR_ID_Y, ret == ESP_OK ? "SUCCESS" : "FAILED");
    
    ret = motor_enable(MOTOR_ID_Z, true);
    uart0_printf("[Plotter] Enable Z motor(%d): %s\n", MOTOR_ID_Z, ret == ESP_OK ? "SUCCESS" : "FAILED");
    
    ret = motor_enable(MOTOR_ID_A, true);
    uart0_printf("[Plotter] Enable A motor(%d): %s\n", MOTOR_ID_A, ret == ESP_OK ? "SUCCESS" : "FAILED");
    
    stepper_delay_ms(100);
    
    uint8_t status;
    if (motor_read_status(MOTOR_ID_X, &status) == ESP_OK) {
        uart0_printf("[Plotter] X motor status after enable: enabled=%d, reached=%d\n", 
                     (status&0x01), (status&0x02)>>1);
    }
    
    uart0_printf("[Plotter] Initialization complete\n");
}

void plotter_set_home(float x, float y, float z)
{
    g_plotter.home_pos.x = x;
    g_plotter.home_pos.y = y;
    g_plotter.home_pos.z = z;
}

void plotter_move_to(float x, float y, float z)
{
    float dx = x - g_plotter.current_pos.x;
    float dy = y - g_plotter.current_pos.y;
    float dz = z - g_plotter.current_pos.z;
    
    int32_t pulses_x = (int32_t)(dx * PULSES_PER_MM_X);
    int32_t pulses_y = (int32_t)(dy * PULSES_PER_MM_Y);
    int32_t pulses_z = (int32_t)(dz * PULSES_PER_MM_Z);
    
    uart0_printf("[Plotter] Move from (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n", 
                 g_plotter.current_pos.x, g_plotter.current_pos.y, g_plotter.current_pos.z,
                 x, y, z);
    uart0_printf("[Plotter] Delta: dx=%.1f, dy=%.1f, dz=%.1f\n", dx, dy, dz);
    uart0_printf("[Plotter] Pulses: X=%d, Y=%d, Z=%d\n", pulses_x, pulses_y, pulses_z);
    
    if (abs(pulses_x) < 2 && abs(pulses_y) < 2 && abs(pulses_z) < 2) {
        uart0_printf("[Plotter] Movement too small, skipping\n");
        g_plotter.current_pos.x = x;
        g_plotter.current_pos.y = y;
        g_plotter.current_pos.z = z;
        return;
    }
    
    motor_direction_t dir_x = (pulses_x >= 0) ? DIRECTION_CW : DIRECTION_CCW;
    motor_direction_t dir_y = (pulses_y >= 0) ? DIRECTION_CW : DIRECTION_CCW;
    motor_direction_t dir_z = (pulses_z >= 0) ? DIRECTION_CW : DIRECTION_CCW;
    
    esp_err_t ret;
    
    if (pulses_z != 0) {
        uart0_printf("[Plotter] Moving Z axis: speed=%d, pulses=%d\n", g_plotter.speed / 2, abs(pulses_z));
        ret = motor_position_mode(MOTOR_ID_Z, dir_z, g_plotter.speed / 2, g_plotter.accel, abs(pulses_z), POS_MODE_RELATIVE);
        uart0_printf("[Plotter] Z command result: %s\n", ret == ESP_OK ? "SUCCESS" : "FAILED");
        motor_wait_reached(MOTOR_ID_Z);
    }
    
    if (pulses_x != 0) {
        uart0_printf("[Plotter] Moving X axis: speed=%d, pulses=%d\n", g_plotter.speed, abs(pulses_x));
        ret = motor_position_mode(MOTOR_ID_X, dir_x, g_plotter.speed, g_plotter.accel, abs(pulses_x), POS_MODE_RELATIVE);
        uart0_printf("[Plotter] X command result: %s\n", ret == ESP_OK ? "SUCCESS" : "FAILED");
        stepper_delay_ms(50);  // 添加延迟，确保命令被正确接收
    }
    if (pulses_y != 0) {
        uart0_printf("[Plotter] Moving Y axis: speed=%d, pulses=%d\n", g_plotter.speed, abs(pulses_y));
        ret = motor_position_mode(MOTOR_ID_Y, dir_y, g_plotter.speed, g_plotter.accel, abs(pulses_y), POS_MODE_RELATIVE);
        uart0_printf("[Plotter] Y command result: %s\n", ret == ESP_OK ? "SUCCESS" : "FAILED");
        stepper_delay_ms(50);  // 添加延迟，确保命令被正确接收
    }
    
    if (pulses_x != 0) motor_wait_reached(MOTOR_ID_X);
    if (pulses_y != 0) motor_wait_reached(MOTOR_ID_Y);
    
    g_plotter.current_pos.x = x;
    g_plotter.current_pos.y = y;
    g_plotter.current_pos.z = z;
    
    uart0_printf("[Plotter] Move completed\n");
}

void plotter_move_relative(float dx, float dy, float dz)
{
    plotter_move_to(
        g_plotter.current_pos.x + dx,
        g_plotter.current_pos.y + dy,
        g_plotter.current_pos.z + dz
    );
}

void plotter_pen_up(void)
{
    if (g_plotter.pen_down) {
        float new_z = g_plotter.current_pos.z + PEN_UP_POS;
        plotter_move_to(g_plotter.current_pos.x, g_plotter.current_pos.y, new_z);
        g_plotter.pen_down = false;
    }
}

void plotter_pen_down(void)
{
    if (!g_plotter.pen_down) {
        float new_z = g_plotter.current_pos.z - PEN_DOWN_POS;
        plotter_move_to(g_plotter.current_pos.x, g_plotter.current_pos.y, new_z);
        g_plotter.pen_down = true;
    }
}

void plotter_draw_line(float x1, float y1, float x2, float y2)
{
    plotter_move_to(x1, y1, g_plotter.current_pos.z);
    plotter_pen_down();
    plotter_move_to(x2, y2, g_plotter.current_pos.z);
    plotter_pen_up();
}

void plotter_draw_rectangle(float x, float y, float width, float height)
{
    plotter_pen_up();
    stepper_delay_ms(100);
    
    plotter_move_to(x, y, g_plotter.current_pos.z);
    stepper_delay_ms(100);
    
    plotter_pen_down();
    stepper_delay_ms(100);
    
    plotter_move_to(x + width, y, g_plotter.current_pos.z);
    stepper_delay_ms(100);
    
    plotter_move_to(x + width, y + height, g_plotter.current_pos.z);
    stepper_delay_ms(100);
    
    plotter_move_to(x, y + height, g_plotter.current_pos.z);
    stepper_delay_ms(100);
    
    plotter_move_to(x, y, g_plotter.current_pos.z);
    stepper_delay_ms(100);
    
    plotter_pen_up();
}

void plotter_draw_circle(float cx, float cy, float radius)
{
    const int steps = 32;
    float angle = 0.0f;
    float step_angle = (2.0f * (float)M_PI) / steps;
    
    float x = cx + radius;
    float y = cy;
    
    plotter_move_to(x, y, g_plotter.current_pos.z);
    plotter_pen_down();
    stepper_delay_ms(100);
    
    for (int i = 1; i <= steps; i++) {
        angle = i * step_angle;
        x = cx + radius * cosf(angle);
        y = cy + radius * sinf(angle);
        plotter_move_to(x, y, g_plotter.current_pos.z);
        stepper_delay_ms(50);
    }
    
    plotter_pen_up();
}

void plotter_draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3)
{
    plotter_move_to(x1, y1, g_plotter.current_pos.z);
    plotter_pen_down();
    plotter_move_to(x2, y2, g_plotter.current_pos.z);
    plotter_move_to(x3, y3, g_plotter.current_pos.z);
    plotter_move_to(x1, y1, g_plotter.current_pos.z);
    plotter_pen_up();
}

void plotter_draw_char(float x, float y, char c, float size)
{
    float w = size;
    float h = size * 1.2f;
    
    switch(c) {
        case 'A':
            plotter_move_to(x + w/2, y + h, g_plotter.current_pos.z);
            plotter_pen_down();
            plotter_move_to(x, y, g_plotter.current_pos.z);
            plotter_move_to(x + w/4, y + h/2, g_plotter.current_pos.z);
            plotter_move_to(x + w*3/4, y + h/2, g_plotter.current_pos.z);
            plotter_move_to(x + w, y, g_plotter.current_pos.z);
            plotter_move_to(x + w/2, y + h, g_plotter.current_pos.z);
            plotter_pen_up();
            break;
        case 'B':
            plotter_move_to(x, y + h, g_plotter.current_pos.z);
            plotter_pen_down();
            plotter_move_to(x, y, g_plotter.current_pos.z);
            plotter_move_to(x + w*3/4, y, g_plotter.current_pos.z);
            plotter_move_to(x + w, y + h/3, g_plotter.current_pos.z);
            plotter_move_to(x + w*3/4, y + h/3, g_plotter.current_pos.z);
            plotter_move_to(x, y + h/3, g_plotter.current_pos.z);
            plotter_move_to(x, y + h*2/3, g_plotter.current_pos.z);
            plotter_move_to(x + w*3/4, y + h*2/3, g_plotter.current_pos.z);
            plotter_move_to(x + w, y + h, g_plotter.current_pos.z);
            plotter_move_to(x, y + h, g_plotter.current_pos.z);
            plotter_pen_up();
            break;
        case 'C':
            plotter_move_to(x + w, y + h, g_plotter.current_pos.z);
            plotter_pen_down();
            plotter_move_to(x, y + h, g_plotter.current_pos.z);
            plotter_move_to(x, y, g_plotter.current_pos.z);
            plotter_move_to(x + w, y, g_plotter.current_pos.z);
            plotter_pen_up();
            break;
        default:
            plotter_draw_rectangle(x, y, w, h);
            break;
    }
}

void plotter_draw_string(float x, float y, const char *str, float size)
{
    float spacing = size * 0.3f;
    float current_x = x;
    
    while (*str) {
        plotter_draw_char(current_x, y, *str, size);
        current_x += size + spacing;
        str++;
    }
}

void plotter_home(void)
{
    plotter_pen_up();
    plotter_move_to(g_plotter.home_pos.x, g_plotter.home_pos.y, g_plotter.home_pos.z);
}

void plotter_wait_movement_done(uint8_t motor_id)
{
    uint8_t status;
    int timeout = 0;
    do {
        if (motor_read_status(motor_id, &status) == ESP_OK) {
            if (status & 0x02) {
                break;
            }
        }
        stepper_delay_ms(10);
        timeout++;
    } while (timeout < 500);
}