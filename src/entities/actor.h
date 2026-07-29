/*
 * actor.h - actor module
 * Copyright (C) 2008-2010  Alexandre Martins <alemartf(at)gmail(dot)com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _ACTOR_H
#define _ACTOR_H

#include "../core/sprite.h"
#include "../core/input.h"
#include "../core/v2d.h"
#include "brick.h"

/* actor structure */
typedef struct actor_t {

    /* movement data */
    v2d_t position, spawn_point;
    v2d_t speed;
    float maxspeed; /* on the x-axis */
    float acceleration; /* on the x-axis */
    float angle; /* angle = ang( actor's x-axis , real x-axis ) */
    float jump_strength;
    int is_jumping;
    int ignore_horizontal;
    input_t *input; /* NULL by default (no input) */

    /* animation */
    animation_t *animation;
    float animation_frame; /* controlled by a timer */
    float animation_speed_factor; /* default value: 1.0 */
    int mirror; /* see the IF_* flags at video.h */
    int visible; /* is this actor visible? */
    float alpha; /* 0.0f (invisible) <= alpha <= 1.0f (opaque) */
    v2d_t hot_spot; /* anchor */

    /* carry */
    v2d_t carry_offset; /* something is carrying me (offset) */
    struct actor_t *carried_by; /* something is carrying me */
    struct actor_t *carrying; /* I'm carrying something */

} actor_t;


/* actor functions */
actor_t* actor_create();
void actor_destroy(actor_t *act);
void actor_render(actor_t *act, v2d_t camera_position);
void actor_render_repeat_xy(actor_t *act, v2d_t camera_position, int repeat_x, int repeat_y);
void actor_move(actor_t *act, v2d_t delta_space); /* uses the orientation angle (you must call the delta timer yourself) ; s = vt */

/* animation */
image_t* actor_image(const actor_t *act);
void actor_change_animation_frame(actor_t *act, int frame);
void actor_change_animation_speed_factor(actor_t *act, float factor); /* default factor: 1.0 */
void actor_change_animation(actor_t *act, animation_t *anim);
int actor_animation_finished(actor_t *act); /* true if the current animation has finished */

/* collision detection */
int actor_collision(const actor_t *a, const actor_t *b); /* tests bounding-box collision between a and b */
int actor_orientedbox_collision(const actor_t *a, const actor_t *b); /* oriented bounding-box collision */
int actor_pixelperfect_collision(const actor_t *a, const actor_t *b); /* tests pixel-perfect collision between a and b */
int actor_brick_collision(actor_t *act, brick_t *brk);

/* sensors */
void actor_render_corners(const actor_t *act, float sqrsize, float diff, v2d_t camera_position);
void actor_corners(actor_t *act, float sqrsize, float diff, brick_list_t *brick_list, brick_t **up, brick_t **upright, brick_t **right, brick_t **downright, brick_t **down, brick_t **downleft, brick_t **left, brick_t **upleft);
void actor_corners_ex(actor_t *act, float sqrsize, v2d_t vup, v2d_t vupright, v2d_t vright, v2d_t vdownright, v2d_t vdown, v2d_t vdownleft, v2d_t vleft, v2d_t vupleft, brick_list_t *brick_list, brick_t **up, brick_t **upright, brick_t **right, brick_t **downright, brick_t **down, brick_t **downleft, brick_t **left, brick_t **upleft);
void actor_corners_set_floor_priority(int floor); /* floor x wall */
void actor_corners_restore_floor_priority();
void actor_corners_set_slope_priority(int slope); /* slope x floor */
void actor_corners_restore_slope_priority();
void actor_corners_disable_detection(int disable_leftwall, int disable_rightwall, int disable_floor, int disable_ceiling);
void actor_get_collision_detectors(actor_t *act, float diff, v2d_t *up, v2d_t *upright, v2d_t *right, v2d_t *downright, v2d_t *down, v2d_t *downleft, v2d_t *left, v2d_t *upleft); /* get collision detectors */

/* platform movement routines */
void actor_handle_clouds(actor_t *act, float diff, brick_t **up, brick_t **upright, brick_t **right, brick_t **downright, brick_t **down, brick_t **downleft, brick_t **left, brick_t **upleft); /* handle clouds */
void actor_handle_collision_detectors(actor_t *act, brick_list_t *brick_list, v2d_t up, v2d_t upright, v2d_t right, v2d_t downright, v2d_t down, v2d_t downleft, v2d_t left, v2d_t upleft, brick_t **brick_up, brick_t **brick_upright, brick_t **brick_right, brick_t **brick_downright, brick_t **brick_down, brick_t **brick_downleft, brick_t **brick_left, brick_t **brick_upleft); /* get bricks detected by the collision detectors */
void actor_handle_carrying(actor_t *act, brick_t *brick_down); /* act is being carried */
void actor_handle_wall_collision(actor_t *act, v2d_t feet, v2d_t left, v2d_t right, brick_t *brick_left, brick_t *brick_right); /* wall collision */
void actor_handle_ceil_collision(actor_t *act, v2d_t feet, v2d_t up, brick_t *brick_up); /* ceiling collision */
void actor_handle_floor_collision(actor_t *act, float diff, float natural_angle, v2d_t *ds, v2d_t *feet, float *friction, brick_t *brick_downleft, brick_t *brick_down, brick_t *brick_downright); /* floor collision */
void actor_handle_slopes(actor_t *act, brick_t *brick_down); /* slopes / speed issues */
void actor_handle_jumping(actor_t *act, float diff, float natural_angle, brick_t *brick_down, brick_t *brick_up); /* jump */
void actor_handle_acceleration(actor_t *act, float friction, brick_t *brick_down); /* x-axis acceleration */

/* pre-defined movement routines */
v2d_t actor_platform_movement(actor_t *act, brick_list_t *brick_list, float gravity);
v2d_t actor_particle_movement(actor_t *act, float gravity);
v2d_t actor_eightdirections_movement(actor_t *act);
v2d_t actor_bullet_movement(actor_t *act);

#endif
