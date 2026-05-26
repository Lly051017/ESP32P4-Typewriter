#ifndef WRITER_CONTROLLER_H
#define WRITER_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WRITER_MAX_AXES 3
#define WRITER_X_AXIS 0
#define WRITER_Y_AXIS 1
#define WRITER_Z_AXIS 2

typedef enum {
    WRITER_STATE_IDLE = 0,
    WRITER_STATE_RUNNING,
    WRITER_STATE_PAUSED,
    WRITER_STATE_HOMING,
    WRITER_STATE_ERROR,
} writer_state_t;

typedef struct {
    float steps_per_mm[WRITER_MAX_AXES];
    float max_rate_mm_min[WRITER_MAX_AXES];
    float acceleration_mm_s2[WRITER_MAX_AXES];
    float max_travel_mm[WRITER_MAX_AXES];
    float default_feed_rate;
    float rapid_rate;
    float pen_up_pos;
    float pen_down_pos;
    float pen_lift_delay_ms;
    uint8_t motor_ids[WRITER_MAX_AXES];
    bool invert_dir[WRITER_MAX_AXES];
} writer_config_t;

typedef struct {
    float x, y, z;
    float feed_rate;
    writer_state_t state;
    uint32_t blocks_executed;
    uint32_t total_blocks;
} writer_status_t;

typedef void (*writer_status_callback_t)(writer_status_t *status);
typedef void (*writer_complete_callback_t)(void);

esp_err_t writer_init(const writer_config_t *config);

esp_err_t writer_execute_gcode(const char *line);

esp_err_t writer_execute_gcode_file(const char *filepath);

void writer_stop(void);

void writer_pause(void);

void writer_resume(void);

writer_state_t writer_get_state(void);

void writer_get_status(writer_status_t *status);

void writer_set_position(float x, float y, float z);

void writer_home(void);

void writer_set_status_callback(writer_status_callback_t cb);

void writer_set_complete_callback(writer_complete_callback_t cb);

void writer_wait_idle(void);

esp_err_t writer_move_to(float x, float y, float feed_rate);

esp_err_t writer_pen_up(void);

esp_err_t writer_pen_down(void);

esp_err_t writer_draw_line(float x1, float y1, float x2, float y2, float feed_rate);

esp_err_t writer_draw_rectangle(float x, float y, float width, float height, float feed_rate);

esp_err_t writer_draw_circle(float center_x, float center_y, float radius, float feed_rate);

esp_err_t writer_draw_arc(float center_x, float center_y, float radius, 
                          float start_angle, float end_angle, float feed_rate);

#ifdef __cplusplus
}
#endif

#endif
