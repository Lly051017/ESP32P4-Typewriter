#ifndef __PLOTTER_H
#define __PLOTTER_H

#include "stepper_motor.h"

#define PULSES_PER_MM_X   80.0f
#define PULSES_PER_MM_Y   80.0f
#define PULSES_PER_MM_Z   40.0f

#define PEN_UP_POS        0
#define PEN_DOWN_POS      50

typedef struct {
    coord_t current_pos;
    coord_t home_pos;
    bool pen_down;
    uint16_t speed;
    uint8_t accel;
} plotter_t;

void plotter_init(uint16_t default_speed, uint8_t default_accel);
void plotter_set_home(float x, float y, float z);
void plotter_move_to(float x, float y, float z);
void plotter_move_relative(float dx, float dy, float dz);
void plotter_pen_up(void);
void plotter_pen_down(void);
void plotter_draw_line(float x1, float y1, float x2, float y2);
void plotter_draw_rectangle(float x, float y, float width, float height);
void plotter_draw_circle(float cx, float cy, float radius);
void plotter_draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3);
void plotter_draw_char(float x, float y, char c, float size);
void plotter_draw_string(float x, float y, const char *str, float size);
void plotter_home(void);
void plotter_wait_movement_done(uint8_t motor_id);

#endif