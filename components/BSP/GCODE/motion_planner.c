#include "motion_planner.h"
#include <string.h>
#include <math.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define MINIMUM_JUNCTION_SPEED 0.0f
#define MINIMUM_FEED_RATE 1.0f

static planner_config_t g_config;
static planner_state_t g_state;
static planner_block_t g_block_buffer[PLANNER_BLOCK_BUFFER_SIZE];
static uint8_t g_block_buffer_tail;
static uint8_t g_block_buffer_head;
static uint8_t g_block_buffer_planned;

static uint8_t next_block_index(uint8_t index)
{
    return (index + 1) % PLANNER_BLOCK_BUFFER_SIZE;
}

static uint8_t prev_block_index(uint8_t index)
{
    return (index == 0) ? PLANNER_BLOCK_BUFFER_SIZE - 1 : index - 1;
}

void planner_init(const planner_config_t *config)
{
    if (config) {
        memcpy(&g_config, config, sizeof(planner_config_t));
    } else {
        memset(&g_config, 0, sizeof(planner_config_t));
        g_config.default_feed_rate = 500.0f;
        g_config.default_acceleration = 100.0f;
        g_config.default_rapid_rate = 3000.0f;
        g_config.min_feed_rate = 1.0f;
        g_config.max_feed_rate = 10000.0f;
        g_config.num_axes = 3;
        for (int i = 0; i < PLANNER_MAX_AXES; i++) {
            g_config.steps_per_mm[i] = 80.0f;
            g_config.max_speed[i] = 3000.0f;
            g_config.max_accel[i] = 500.0f;
        }
    }
    
    planner_reset();
}

void planner_reset(void)
{
    memset(&g_state, 0, sizeof(planner_state_t));
    g_block_buffer_tail = 0;
    g_block_buffer_head = 0;
    g_block_buffer_planned = 0;
}

static float compute_distance(float *from, float *to, uint8_t n_axes)
{
    float sum = 0.0f;
    for (int i = 0; i < n_axes; i++) {
        float delta = to[i] - from[i];
        sum += delta * delta;
    }
    return sqrtf(sum);
}

static void compute_unit_vector(float *unit_vec, float *from, float *to, float distance, uint8_t n_axes)
{
    if (distance < 1e-6f) {
        memset(unit_vec, 0, sizeof(float) * n_axes);
        return;
    }
    
    float inv_dist = 1.0f / distance;
    for (int i = 0; i < n_axes; i++) {
        unit_vec[i] = (to[i] - from[i]) * inv_dist;
    }
}

static float limit_acceleration_by_axis(float *unit_vec)
{
    float max_accel = g_config.default_acceleration;
    
    for (int i = 0; i < g_config.num_axes; i++) {
        if (fabsf(unit_vec[i]) > 1e-6f) {
            float axis_accel = g_config.max_accel[i] / fabsf(unit_vec[i]);
            max_accel = MIN(max_accel, axis_accel);
        }
    }
    
    return max_accel;
}

static float limit_speed_by_axis(float *unit_vec, float target_speed)
{
    float max_speed = target_speed;
    
    for (int i = 0; i < g_config.num_axes; i++) {
        if (fabsf(unit_vec[i]) > 1e-6f) {
            float axis_speed = g_config.max_speed[i] / fabsf(unit_vec[i]);
            max_speed = MIN(max_speed, axis_speed);
        }
    }
    
    return max_speed;
}

static float compute_junction_speed(float *prev_unit_vec, float *curr_unit_vec, float accel)
{
    float dot = 0.0f;
    for (int i = 0; i < g_config.num_axes; i++) {
        dot += prev_unit_vec[i] * curr_unit_vec[i];
    }
    
    if (dot > 0.99f) {
        return g_config.max_feed_rate;
    }
    
    float junction_cos_theta = -dot;
    float sin_theta_d2 = sqrtf(0.5f * (1.0f - junction_cos_theta));
    
    float junction_speed = sqrtf(accel * 0.5f / sin_theta_d2);
    
    return junction_speed;
}

bool planner_buffer_line(float *target, float feed_rate, bool is_rapid)
{
    uint8_t next_head = next_block_index(g_block_buffer_head);
    
    if (next_head == g_block_buffer_tail) {
        return false;
    }
    
    planner_block_t *block = &g_block_buffer[g_block_buffer_head];
    memset(block, 0, sizeof(planner_block_t));
    
    float distance = compute_distance(g_state.position, target, g_config.num_axes);
    if (distance < 1e-6f) {
        return false;
    }
    
    memcpy(block->target, target, sizeof(float) * PLANNER_MAX_AXES);
    block->distance = distance;
    block->is_rapid = is_rapid;
    
    float unit_vec[PLANNER_MAX_AXES];
    compute_unit_vector(unit_vec, g_state.position, target, distance, g_config.num_axes);
    
    if (is_rapid) {
        block->feed_rate = g_config.default_rapid_rate;
        block->rapid_rate = g_config.default_rapid_rate;
    } else {
        block->feed_rate = limit_speed_by_axis(unit_vec, feed_rate);
        block->feed_rate = MAX(block->feed_rate, MINIMUM_FEED_RATE);
        block->rapid_rate = g_config.default_rapid_rate;
    }
    
    block->acceleration = limit_acceleration_by_axis(unit_vec);
    
    for (int i = 0; i < g_config.num_axes; i++) {
        float delta = target[i] - g_state.position[i];
        block->steps[i] = (uint32_t)(fabsf(delta * g_config.steps_per_mm[i]) + 0.5f);
        if (delta < 0) {
            block->direction_bits |= (1 << i);
        }
    }
    
    block->step_event_count = 0;
    for (int i = 0; i < g_config.num_axes; i++) {
        if (block->steps[i] > block->step_event_count) {
            block->step_event_count = block->steps[i];
        }
    }
    
    float junction_speed = compute_junction_speed(g_state.previous_unit_vec, unit_vec, block->acceleration);
    block->max_entry_speed = MIN(block->feed_rate, junction_speed);
    block->entry_speed = MINIMUM_JUNCTION_SPEED;
    
    float max_exit_speed = sqrtf(block->entry_speed * block->entry_speed + 2.0f * block->acceleration * block->distance);
    block->exit_speed = MIN(block->feed_rate, max_exit_speed);
    
    memcpy(g_state.previous_unit_vec, unit_vec, sizeof(float) * PLANNER_MAX_AXES);
    g_state.previous_speed = block->feed_rate;
    memcpy(g_state.position, target, sizeof(float) * PLANNER_MAX_AXES);
    
    g_block_buffer_head = next_head;
    
    planner_recalculate();
    
    return true;
}

void planner_recalculate(void)
{
    if (g_block_buffer_head == g_block_buffer_tail) {
        return;
    }
    
    uint8_t block_index = prev_block_index(g_block_buffer_head);
    planner_block_t *current = &g_block_buffer[block_index];
    
    current->entry_speed = MIN(current->max_entry_speed, 
                               sqrtf(2.0f * current->acceleration * current->distance));
    
    block_index = prev_block_index(block_index);
    while (block_index != g_block_buffer_planned && block_index != g_block_buffer_tail) {
        planner_block_t *next = current;
        current = &g_block_buffer[block_index];
        
        float entry_speed_sqr = next->entry_speed * next->entry_speed + 
                                2.0f * current->acceleration * current->distance;
        if (entry_speed_sqr < current->max_entry_speed * current->max_entry_speed) {
            current->entry_speed = sqrtf(entry_speed_sqr);
        } else {
            current->entry_speed = current->max_entry_speed;
        }
        
        block_index = prev_block_index(block_index);
    }
    
    planner_block_t *next = &g_block_buffer[g_block_buffer_planned];
    block_index = next_block_index(g_block_buffer_planned);
    
    while (block_index != g_block_buffer_head) {
        current = next;
        next = &g_block_buffer[block_index];
        
        if (current->entry_speed < next->entry_speed) {
            float entry_speed_sqr = current->entry_speed * current->entry_speed + 
                                    2.0f * current->acceleration * current->distance;
            if (entry_speed_sqr < next->entry_speed * next->entry_speed) {
                next->entry_speed = sqrtf(entry_speed_sqr);
            }
        }
        
        block_index = next_block_index(block_index);
    }
}

planner_block_t* planner_get_current_block(void)
{
    if (g_block_buffer_head == g_block_buffer_tail) {
        return NULL;
    }
    return &g_block_buffer[g_block_buffer_tail];
}

void planner_discard_current_block(void)
{
    if (g_block_buffer_head != g_block_buffer_tail) {
        g_block_buffer_tail = next_block_index(g_block_buffer_tail);
        if (g_block_buffer_planned != g_block_buffer_tail) {
            g_block_buffer_planned = g_block_buffer_tail;
        }
    }
}

bool planner_has_blocks(void)
{
    return g_block_buffer_head != g_block_buffer_tail;
}

uint8_t planner_get_block_count(void)
{
    if (g_block_buffer_head >= g_block_buffer_tail) {
        return g_block_buffer_head - g_block_buffer_tail;
    }
    return PLANNER_BLOCK_BUFFER_SIZE - g_block_buffer_tail + g_block_buffer_head;
}

void planner_sync_position(float *pos)
{
    memcpy(g_state.position, pos, sizeof(float) * PLANNER_MAX_AXES);
}

const planner_state_t* planner_get_state(void)
{
    return &g_state;
}

void planner_set_position(float x, float y, float z)
{
    g_state.position[0] = x;
    g_state.position[1] = y;
    g_state.position[2] = z;
}

float planner_get_distance(float *from, float *to)
{
    return compute_distance(from, to, g_config.num_axes);
}
