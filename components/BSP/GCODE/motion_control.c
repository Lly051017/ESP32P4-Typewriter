#include "motion_control.h"
#include "motion_planner.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static mc_config_t g_mc_config;
static float g_position[MC_MAX_AXES];
static bool g_is_running;
static bool g_stop_requested;

static mc_move_callback_t g_move_callback = NULL;
static mc_arc_callback_t g_arc_callback = NULL;
static mc_dwell_callback_t g_dwell_callback = NULL;

void mc_init(const mc_config_t *config)
{
    if (config) 
    {
        memcpy(&g_mc_config, config, sizeof(mc_config_t));
    } 
    else 
    {
        memset(&g_mc_config, 0, sizeof(mc_config_t));
        g_mc_config.default_feed_rate = 500.0f;
        g_mc_config.rapid_rate = 3000.0f;
        g_mc_config.arc_tolerance = 0.002f;
        g_mc_config.num_axes = 3;
        for (int i = 0; i < MC_MAX_AXES; i++) {
            g_mc_config.steps_per_mm[i] = 80.0f;
            g_mc_config.max_rate[i] = 3000.0f;
            g_mc_config.acceleration[i] = 500.0f;
            g_mc_config.max_travel[i] = 200.0f;
        }
    }
    
    planner_config_t pl_config = 
    {
        .default_feed_rate = g_mc_config.default_feed_rate,
        .default_rapid_rate = g_mc_config.rapid_rate,
        .default_acceleration = g_mc_config.acceleration[0],
        .min_feed_rate = 1.0f,
        .max_feed_rate = g_mc_config.rapid_rate,
        .num_axes = g_mc_config.num_axes,
    };
    memcpy(pl_config.steps_per_mm, g_mc_config.steps_per_mm, sizeof(float) * MC_MAX_AXES);
    memcpy(pl_config.max_speed, g_mc_config.max_rate, sizeof(float) * MC_MAX_AXES);
    memcpy(pl_config.max_accel, g_mc_config.acceleration, sizeof(float) * MC_MAX_AXES);
    
    planner_init(&pl_config);
    
    mc_reset();
}

void mc_set_callbacks(mc_move_callback_t move_cb, 
                      mc_arc_callback_t arc_cb,
                      mc_dwell_callback_t dwell_cb)
{
    g_move_callback = move_cb;
    g_arc_callback = arc_cb;
    g_dwell_callback = dwell_cb;
}

void mc_reset(void)
{
    memset(g_position, 0, sizeof(g_position));
    g_is_running = false;
    g_stop_requested = false;
    planner_reset();
}

mc_error_t mc_linear(float *target, float feed_rate, bool is_rapid)
{
    if (!target) 
    {
        return MC_ERROR_PARAM;
    }
    
    if (!is_rapid && feed_rate <= 0) 
    {
        return MC_ERROR_FEEDRATE;
    }
    
    if (g_stop_requested) 
    {
        g_stop_requested = false;
        return MC_ERROR_BUSY;
    }
    
    g_is_running = true;
    
    if (g_move_callback) 
    {
        g_move_callback(target, feed_rate, is_rapid);
    }
    
    planner_buffer_line(target, feed_rate, is_rapid);
    
    memcpy(g_position, target, sizeof(float) * MC_MAX_AXES);
    
    g_is_running = false;
    
    return MC_OK;
}

static float compute_arc_segment_count(float radius, float angular_travel, float linear_travel, float tolerance)
{
    float arc_length = radius * angular_travel;
    float chord_length = 2.0f * radius * sinf(angular_travel / 2.0f);
    
    if (chord_length < tolerance) {
        return 1;
    }
    
    float num_segments = ceilf(arc_length / sqrtf(2.0f * tolerance * radius));
    num_segments = MAX(num_segments, 8);
    
    return num_segments;
}

mc_error_t mc_arc(float *target, float *offset, float radius,
                  uint8_t axis_0, uint8_t axis_1, uint8_t axis_linear,
                  bool is_clockwise, float feed_rate)
{
    if (!target || !offset) 
    {
        return MC_ERROR_PARAM;
    }
    
    if (feed_rate <= 0) 
    {
        return MC_ERROR_FEEDRATE;
    }
    
    if (g_stop_requested) 
    {
        g_stop_requested = false;
        return MC_ERROR_BUSY;
    }
    
    g_is_running = true;
    
    float center_axis0 = g_position[axis_0] + offset[axis_0];
    float center_axis1 = g_position[axis_1] + offset[axis_1];
    
    float rt_axis0 = target[axis_0] - center_axis0;
    float rt_axis1 = target[axis_1] - center_axis1;
    
    float angular_travel = atan2f(rt_axis1 * offset[axis_0] - rt_axis0 * offset[axis_1], 
                                   rt_axis0 * offset[axis_0] + rt_axis1 * offset[axis_1]);
    
    if (is_clockwise) 
    {
        if (angular_travel >= 0) 
        {
            angular_travel -= 2.0f * M_PI;
        }
    } else {
        if (angular_travel <= 0) 
        {
            angular_travel += 2.0f * M_PI;
        }
    }
    
    float linear_travel = target[axis_linear] - g_position[axis_linear];
    
    uint32_t segments = (uint32_t)compute_arc_segment_count(radius, fabsf(angular_travel), 
                                                            fabsf(linear_travel), g_mc_config.arc_tolerance);
    
    float theta_per_segment = angular_travel / segments;
    float linear_per_segment = linear_travel / segments;
    
    float cos_theta = cosf(theta_per_segment);
    float sin_theta = sinf(theta_per_segment);
    
    float position[MC_MAX_AXES];
    memcpy(position, g_position, sizeof(position));
    
    float radius_axis0 = -offset[axis_0];
    float radius_axis1 = -offset[axis_1];
    
    for (uint32_t i = 1; i <= segments; i++) 
    {
        if (g_stop_requested) {
            g_is_running = false;
            g_stop_requested = false;
            return MC_ERROR_BUSY;
        }
        
        if (i < segments) {
            float new_radius_axis0 = radius_axis0 * cos_theta - radius_axis1 * sin_theta;
            radius_axis1 = radius_axis0 * sin_theta + radius_axis1 * cos_theta;
            radius_axis0 = new_radius_axis0;
        } else {
            radius_axis0 = rt_axis0;
            radius_axis1 = rt_axis1;
        }
        
        position[axis_0] = center_axis0 + radius_axis0;
        position[axis_1] = center_axis1 + radius_axis1;
        position[axis_linear] += linear_per_segment;
        
        if (g_arc_callback) {
            g_arc_callback(position, feed_rate);
        }
        
        planner_buffer_line(position, feed_rate, false);
    }
    
    memcpy(g_position, target, sizeof(float) * MC_MAX_AXES);
    
    g_is_running = false;
    
    return MC_OK;
}

mc_error_t mc_dwell(float seconds)
{
    if (seconds <= 0) 
    {
        return MC_OK;
    }
    
    if (g_dwell_callback) 
    {
        g_dwell_callback(seconds);
    }
    
    return MC_OK;
}

void mc_sync_position(void)
{
    planner_sync_position(g_position);
}

void mc_set_position(float x, float y, float z)
{
    g_position[0] = x;
    g_position[1] = y;
    g_position[2] = z;
    planner_set_position(x, y, z);
}

void mc_get_position(float *pos)
{
    if (pos) 
    {
        memcpy(pos, g_position, sizeof(float) * MC_MAX_AXES);
    }
}

bool mc_is_running(void)
{
    return g_is_running || planner_has_blocks();
}

void mc_stop(void)
{
    g_stop_requested = true;
    planner_reset();
}
