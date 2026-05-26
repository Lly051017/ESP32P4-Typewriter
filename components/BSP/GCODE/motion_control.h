#ifndef MOTION_CONTROL_H
#define MOTION_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_MAX_AXES 3

typedef enum {
    MC_OK = 0,
    MC_ERROR_PARAM,
    MC_ERROR_FEEDRATE,
    MC_ERROR_LIMIT,
    MC_ERROR_BUSY,
} mc_error_t;

typedef struct {
    float max_travel[MC_MAX_AXES];
    float steps_per_mm[MC_MAX_AXES];
    float max_rate[MC_MAX_AXES];
    float acceleration[MC_MAX_AXES];
    float default_feed_rate;
    float rapid_rate;
    float arc_tolerance;
    uint8_t num_axes;
} mc_config_t;

typedef void (*mc_move_callback_t)(float *target, float feed_rate, bool is_rapid);
typedef void (*mc_arc_callback_t)(float *target, float feed_rate);
typedef void (*mc_dwell_callback_t)(float seconds);

void mc_init(const mc_config_t *config);

void mc_set_callbacks(mc_move_callback_t move_cb, 
                      mc_arc_callback_t arc_cb,
                      mc_dwell_callback_t dwell_cb);

mc_error_t mc_linear(float *target, float feed_rate, bool is_rapid);

mc_error_t mc_arc(float *target, float *offset, float radius,
                  uint8_t axis_0, uint8_t axis_1, uint8_t axis_linear,
                  bool is_clockwise, float feed_rate);

mc_error_t mc_dwell(float seconds);

void mc_sync_position(void);

void mc_set_position(float x, float y, float z);

void mc_get_position(float *pos);

void mc_reset(void);

bool mc_is_running(void);

void mc_stop(void);

#ifdef __cplusplus
}
#endif

#endif
