#include "writer_controller.h"
#include "gcode_parser.h"
#include "motion_control.h"
#include "../STEPPER/stepper_motor.h"
#include "esp_err.h"
#include <string.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "uart.h"

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static writer_config_t g_config;
static writer_state_t g_state;
static writer_status_t g_status;
static float g_current_pos[WRITER_MAX_AXES];
static float g_current_feed_rate;

static writer_status_callback_t g_status_callback = NULL;
static writer_complete_callback_t g_complete_callback = NULL;

static QueueHandle_t g_command_queue;
static TaskHandle_t g_writer_task_handle;

#define COMMAND_QUEUE_SIZE 32
#define MAX_LINE_LENGTH 256

typedef struct {
    char line[MAX_LINE_LENGTH];
} gcode_command_t;

static void update_status(void)
{
    g_status.x = g_current_pos[WRITER_X_AXIS];
    g_status.y = g_current_pos[WRITER_Y_AXIS];
    g_status.z = g_current_pos[WRITER_Z_AXIS];
    g_status.feed_rate = g_current_feed_rate;
    g_status.state = g_state;
    
    if (g_status_callback) {
        g_status_callback(&g_status);
    }
}

static void motor_move_callback(float *target, float feed_rate, bool is_rapid)
{
    float dx = target[WRITER_X_AXIS] - g_current_pos[WRITER_X_AXIS];
    float dy = target[WRITER_Y_AXIS] - g_current_pos[WRITER_Y_AXIS];
    float dz = target[WRITER_Z_AXIS] - g_current_pos[WRITER_Z_AXIS];

    int32_t pulses_x = (int32_t)(dx * g_config.steps_per_mm[WRITER_X_AXIS]);
    int32_t pulses_y = (int32_t)(dy * g_config.steps_per_mm[WRITER_Y_AXIS]);
    int32_t pulses_z = (int32_t)(dz * g_config.steps_per_mm[WRITER_Z_AXIS]);

    if (abs(pulses_x) < 2 && abs(pulses_y) < 2 && abs(pulses_z) < 2) {
        memcpy(g_current_pos, target, sizeof(float) * WRITER_MAX_AXES);
        return;
    }

    uint16_t speed_rpm = 200;
    uint8_t accel = 50;
    motor_direction_t x_dir = (pulses_x >= 0) ? DIRECTION_CW : DIRECTION_CCW;
    motor_direction_t y_dir = (pulses_y >= 0) ? DIRECTION_CW : DIRECTION_CCW;
    motor_direction_t z_dir = (pulses_z >= 0) ? DIRECTION_CW : DIRECTION_CCW;

    if (g_config.invert_dir[WRITER_X_AXIS]) x_dir = (x_dir == DIRECTION_CW) ? DIRECTION_CCW : DIRECTION_CW;
    if (g_config.invert_dir[WRITER_Y_AXIS]) y_dir = (y_dir == DIRECTION_CW) ? DIRECTION_CCW : DIRECTION_CW;
    if (g_config.invert_dir[WRITER_Z_AXIS]) z_dir = (z_dir == DIRECTION_CW) ? DIRECTION_CCW : DIRECTION_CW;

    uint32_t mask = 0;

    if (abs(pulses_z) != 0) {
        motor_clear_done(MOTOR_MASK_Z);
        motor_move_submit(MOTOR_ID_Z, z_dir, speed_rpm / 2,
                         accel, abs(pulses_z), POS_MODE_RELATIVE);
        mask |= MOTOR_MASK_Z;
    }

    if (abs(pulses_x) != 0) {
        motor_clear_done(MOTOR_MASK_X);
        motor_move_submit(MOTOR_ID_X, x_dir, speed_rpm,
                         accel, abs(pulses_x), POS_MODE_RELATIVE);
        mask |= MOTOR_MASK_X;
    }

    if (abs(pulses_y) != 0) {
        motor_clear_done(MOTOR_MASK_Y);
        motor_move_submit(MOTOR_ID_Y, y_dir, speed_rpm,
                         accel, abs(pulses_y), POS_MODE_RELATIVE);
        mask |= MOTOR_MASK_Y;
    }

    if (mask) {
        motor_wait_done(mask, 15000);
    }

    memcpy(g_current_pos, target, sizeof(float) * WRITER_MAX_AXES);
    g_current_feed_rate = feed_rate;
    g_status.blocks_executed++;
    update_status();
}

static void motor_arc_callback(float *target, float feed_rate)
{
    motor_move_callback(target, feed_rate, false);
}

static void motor_dwell_callback(float seconds)
{
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(seconds * 1000)));
}

static void process_gcode_block(gcode_block_t *block)
{
    if (block->is_dwell) {
        mc_dwell(block->dwell_seconds);
        return;
    }
    
    if (block->has_x || block->has_y || block->has_z) {
        float target[MC_MAX_AXES];
        memcpy(target, block->xyz, sizeof(float) * MC_MAX_AXES);
        
        if (block->is_arc) {
            float offset[3] = {0};
            if (block->has_i) offset[0] = block->ijk[0];
            if (block->has_j) offset[1] = block->ijk[1];
            if (block->has_k) offset[2] = block->ijk[2];
            
            float radius = sqrtf(offset[0] * offset[0] + offset[1] * offset[1]);
            
            uint8_t axis_0 = WRITER_X_AXIS;
            uint8_t axis_1 = WRITER_Y_AXIS;
            uint8_t axis_linear = WRITER_Z_AXIS;
            
            if (block->plane_select == GCODE_PLANE_ZX) {
                axis_0 = WRITER_Z_AXIS;
                axis_1 = WRITER_X_AXIS;
                axis_linear = WRITER_Y_AXIS;
            } else if (block->plane_select == GCODE_PLANE_YZ) {
                axis_0 = WRITER_Y_AXIS;
                axis_1 = WRITER_Z_AXIS;
                axis_linear = WRITER_X_AXIS;
            }
            
            mc_arc(target, offset, radius, axis_0, axis_1, axis_linear, 
                   block->arc_cw, block->feed_rate);
        } else {
            mc_linear(target, block->feed_rate, block->is_rapid);
        }
    }
    
    gcode_update_state(block);
}

static void writer_task(void *arg)
{
    gcode_command_t cmd;
    gcode_block_t block;
    
    while (1) {
        if (xQueueReceive(g_command_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (g_state == WRITER_STATE_IDLE || g_state == WRITER_STATE_RUNNING) {
                g_state = WRITER_STATE_RUNNING;
                
                gcode_error_t err = gcode_parse_line(cmd.line, &block);
                if (err == GCODE_OK) {
                    process_gcode_block(&block);
                }
                
                if (uxQueueMessagesWaiting(g_command_queue) == 0) {
                    g_state = WRITER_STATE_IDLE;
                    if (g_complete_callback) {
                        g_complete_callback();
                    }
                }
            }
        }
        
        update_status();
    }
}

esp_err_t writer_init(const writer_config_t *config)
{
    if (config) {
        memcpy(&g_config, config, sizeof(writer_config_t));
    } else {
        memset(&g_config, 0, sizeof(writer_config_t));
        g_config.steps_per_mm[WRITER_X_AXIS] = 80.0f;
        g_config.steps_per_mm[WRITER_Y_AXIS] = 80.0f;
        g_config.steps_per_mm[WRITER_Z_AXIS] = 100.0f;
        g_config.max_rate_mm_min[WRITER_X_AXIS] = 3000.0f;
        g_config.max_rate_mm_min[WRITER_Y_AXIS] = 3000.0f;
        g_config.max_rate_mm_min[WRITER_Z_AXIS] = 1000.0f;
        g_config.acceleration_mm_s2[WRITER_X_AXIS] = 500.0f;
        g_config.acceleration_mm_s2[WRITER_Y_AXIS] = 500.0f;
        g_config.acceleration_mm_s2[WRITER_Z_AXIS] = 200.0f;
        g_config.max_travel_mm[WRITER_X_AXIS] = 200.0f;
        g_config.max_travel_mm[WRITER_Y_AXIS] = 200.0f;
        g_config.max_travel_mm[WRITER_Z_AXIS] = 50.0f;
        g_config.default_feed_rate = 500.0f;
        g_config.rapid_rate = 3000.0f;
        g_config.pen_up_pos = 5.0f;
        g_config.pen_down_pos = 0.0f;
        g_config.pen_lift_delay_ms = 100;
        g_config.motor_ids[WRITER_X_AXIS] = 1;
        g_config.motor_ids[WRITER_Y_AXIS] = 2;
        g_config.motor_ids[WRITER_Z_AXIS] = 3;
        g_config.invert_dir[WRITER_X_AXIS] = false;
        g_config.invert_dir[WRITER_Y_AXIS] = false;
        g_config.invert_dir[WRITER_Z_AXIS] = false;
    }
    
    mc_config_t mc_config = {
        .default_feed_rate = g_config.default_feed_rate,
        .rapid_rate = g_config.rapid_rate,
        .arc_tolerance = 0.002f,
        .num_axes = WRITER_MAX_AXES,
    };
    memcpy(mc_config.steps_per_mm, g_config.steps_per_mm, sizeof(float) * WRITER_MAX_AXES);
    memcpy(mc_config.max_rate, g_config.max_rate_mm_min, sizeof(float) * WRITER_MAX_AXES);
    memcpy(mc_config.acceleration, g_config.acceleration_mm_s2, sizeof(float) * WRITER_MAX_AXES);
    memcpy(mc_config.max_travel, g_config.max_travel_mm, sizeof(float) * WRITER_MAX_AXES);
    
    mc_init(&mc_config);
    gcode_parser_init();
    
    mc_set_callbacks(motor_move_callback, motor_arc_callback, motor_dwell_callback);
    
    g_command_queue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(gcode_command_t));
    if (!g_command_queue) {
        return ESP_FAIL;
    }
    
    xTaskCreate(writer_task, "writer_task", 4096, NULL, 10, &g_writer_task_handle);
    
    g_state = WRITER_STATE_IDLE;
    memset(&g_status, 0, sizeof(g_status));
    memset(g_current_pos, 0, sizeof(g_current_pos));
    g_current_feed_rate = g_config.default_feed_rate;
    
    uart0_printf("[Writer] Enabling motors in writer_init...\n");
    
    esp_err_t ret;
    ret = motor_enable(g_config.motor_ids[WRITER_X_AXIS], true);
    uart0_printf("[Writer] Enable X motor(%d): %s\n", g_config.motor_ids[WRITER_X_AXIS], ret == ESP_OK ? "SUCCESS" : "FAILED");
    
    ret = motor_enable(g_config.motor_ids[WRITER_Y_AXIS], true);
    uart0_printf("[Writer] Enable Y motor(%d): %s\n", g_config.motor_ids[WRITER_Y_AXIS], ret == ESP_OK ? "SUCCESS" : "FAILED");
    
    ret = motor_enable(g_config.motor_ids[WRITER_Z_AXIS], true);
    uart0_printf("[Writer] Enable Z motor(%d): %s\n", g_config.motor_ids[WRITER_Z_AXIS], ret == ESP_OK ? "SUCCESS" : "FAILED");
    
    stepper_delay_ms(100);
    
    uint8_t status;
    if (motor_read_status(g_config.motor_ids[WRITER_X_AXIS], &status) == ESP_OK) {
        uart0_printf("[Writer] X motor(%d) status: enabled=%d, reached=%d\n", 
                     g_config.motor_ids[WRITER_X_AXIS], (status&0x01), (status&0x02)>>1);
    }
    
    return ESP_OK;
}

esp_err_t writer_execute_gcode(const char *line)
{
    if (!line || !g_command_queue) {
        return ESP_FAIL;
    }
    
    gcode_command_t cmd;
    strncpy(cmd.line, line, MAX_LINE_LENGTH - 1);
    cmd.line[MAX_LINE_LENGTH - 1] = '\0';
    
    if (xQueueSend(g_command_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_FAIL;
    }
    
    g_status.total_blocks++;
    
    return ESP_OK;
}

esp_err_t writer_execute_gcode_file(const char *filepath)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void writer_stop(void)
{
    if (g_command_queue) {
        xQueueReset(g_command_queue);
    }
    
    motor_stop(g_config.motor_ids[WRITER_X_AXIS]);
    motor_stop(g_config.motor_ids[WRITER_Y_AXIS]);
    motor_stop(g_config.motor_ids[WRITER_Z_AXIS]);
    
    mc_stop();
    g_state = WRITER_STATE_IDLE;
    update_status();
}

void writer_pause(void)
{
    if (g_state == WRITER_STATE_RUNNING) {
        g_state = WRITER_STATE_PAUSED;
    }
}

void writer_resume(void)
{
    if (g_state == WRITER_STATE_PAUSED) {
        g_state = WRITER_STATE_RUNNING;
    }
}

writer_state_t writer_get_state(void)
{
    return g_state;
}

void writer_get_status(writer_status_t *status)
{
    if (status) {
        memcpy(status, &g_status, sizeof(writer_status_t));
    }
}

void writer_set_position(float x, float y, float z)
{
    g_current_pos[WRITER_X_AXIS] = x;
    g_current_pos[WRITER_Y_AXIS] = y;
    g_current_pos[WRITER_Z_AXIS] = z;
    mc_set_position(x, y, z);
}

void writer_home(void)
{
    g_state = WRITER_STATE_HOMING;
    
    motor_homing(g_config.motor_ids[WRITER_X_AXIS], HOMING_MODE_LIMIT_SWITCH);
    motor_homing(g_config.motor_ids[WRITER_Y_AXIS], HOMING_MODE_LIMIT_SWITCH);
    
    motor_wait_reached(g_config.motor_ids[WRITER_X_AXIS]);
    motor_wait_reached(g_config.motor_ids[WRITER_Y_AXIS]);
    
    writer_set_position(0, 0, 0);
    
    g_state = WRITER_STATE_IDLE;
    update_status();
}

void writer_set_status_callback(writer_status_callback_t cb)
{
    g_status_callback = cb;
}

void writer_set_complete_callback(writer_complete_callback_t cb)
{
    g_complete_callback = cb;
}

void writer_wait_idle(void)
{
    while (g_state != WRITER_STATE_IDLE || uxQueueMessagesWaiting(g_command_queue) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t writer_move_to(float x, float y, float feed_rate)
{
    char line[64];
    snprintf(line, sizeof(line), "G1 X%.3f Y%.3f F%.1f", x, y, feed_rate);
    return writer_execute_gcode(line);
}

esp_err_t writer_pen_up(void)
{
    char line[64];
    snprintf(line, sizeof(line), "G0 Z%.3f", g_config.pen_up_pos);
    return writer_execute_gcode(line);
}

esp_err_t writer_pen_down(void)
{
    char line[64];
    snprintf(line, sizeof(line), "G0 Z%.3f", g_config.pen_down_pos);
    return writer_execute_gcode(line);
}

esp_err_t writer_draw_line(float x1, float y1, float x2, float y2, float feed_rate)
{
    esp_err_t ret;
    
    ret = writer_pen_up();
    if (ret != ESP_OK) return ret;
    
    ret = writer_move_to(x1, y1, feed_rate);
    if (ret != ESP_OK) return ret;
    
    ret = writer_pen_down();
    if (ret != ESP_OK) return ret;
    
    ret = writer_move_to(x2, y2, feed_rate);
    if (ret != ESP_OK) return ret;
    
    return writer_pen_up();
}

esp_err_t writer_draw_arc(float center_x, float center_y, float radius, 
                          float start_angle, float end_angle, float feed_rate)
{
    float start_x = center_x + radius * cosf(start_angle);
    float start_y = center_y + radius * sinf(start_angle);
    float end_x = center_x + radius * cosf(end_angle);
    float end_y = center_y + radius * sinf(end_angle);
    
    float i = center_x - start_x;
    float j = center_y - start_y;
    
    esp_err_t ret;
    
    ret = writer_pen_up();
    if (ret != ESP_OK) return ret;
    
    char line[128];
    snprintf(line, sizeof(line), "G0 X%.3f Y%.3f", start_x, start_y);
    ret = writer_execute_gcode(line);
    if (ret != ESP_OK) return ret;
    
    ret = writer_pen_down();
    if (ret != ESP_OK) return ret;
    
    bool clockwise = (end_angle > start_angle) ? false : true;
    snprintf(line, sizeof(line), "%s X%.3f Y%.3f I%.3f J%.3f F%.1f",
             clockwise ? "G2" : "G3", end_x, end_y, i, j, feed_rate);
    ret = writer_execute_gcode(line);
    if (ret != ESP_OK) return ret;
    
    return writer_pen_up();
}
