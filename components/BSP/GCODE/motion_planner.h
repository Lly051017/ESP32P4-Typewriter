#ifndef MOTION_PLANNER_H
#define MOTION_PLANNER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLANNER_MAX_AXES 3
#define PLANNER_BLOCK_BUFFER_SIZE 16

typedef struct {
    float target[PLANNER_MAX_AXES];
    float feed_rate;
    float entry_speed;
    float max_entry_speed;
    float exit_speed;
    float acceleration;
    float distance;
    float rapid_rate;
    uint32_t steps[PLANNER_MAX_AXES];
    uint32_t step_event_count;
    uint8_t direction_bits;
    bool is_rapid;
    bool is_arc;
} planner_block_t;

typedef struct {
    float max_speed[PLANNER_MAX_AXES];
    float max_accel[PLANNER_MAX_AXES];
    float steps_per_mm[PLANNER_MAX_AXES];
    float min_feed_rate;
    float max_feed_rate;
    float default_feed_rate;
    float default_acceleration;
    float default_rapid_rate;
    uint8_t num_axes;
} planner_config_t;

typedef struct {
    float position[PLANNER_MAX_AXES];
    float previous_speed;
    float previous_unit_vec[PLANNER_MAX_AXES];
} planner_state_t;

void planner_init(const planner_config_t *config);

void planner_reset(void);

bool planner_buffer_line(float *target, float feed_rate, bool is_rapid);

planner_block_t* planner_get_current_block(void);

void planner_discard_current_block(void);

bool planner_has_blocks(void);

uint8_t planner_get_block_count(void);

void planner_sync_position(float *pos);

const planner_state_t* planner_get_state(void);

void planner_set_position(float x, float y, float z);

float planner_get_distance(float *from, float *to);

void planner_recalculate(void);

#ifdef __cplusplus
}
#endif

#endif
