/*
 * brick.h - brick module
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

#ifndef _BRICK_H
#define _BRICK_H

#include "../core/sprite.h"

/* brick properties */
#define BRK_NONE                0
#define BRK_OBSTACLE            1
#define BRK_CLOUD               2

/* brick behavior */
#define BRICKBEHAVIOR_MAXARGS   5
#define BRB_DEFAULT             0
#define BRB_CIRCULAR            1
#define BRB_BREAKABLE           2
#define BRB_FALL                3

/* brick state */
#define BRS_IDLE                0
#define BRS_DEAD                1
#define BRS_ACTIVE              2 /* generic action */

/* misc */
#define BRICK_MAXVALUES         3
#define BRB_FALL_TIME           1.0 /* time in seconds before a BRB_FALL gets destroyed */

/* forward declarations */
typedef struct brickdata_t brickdata_t;
typedef struct brick_t brick_t;
typedef struct brick_list_t brick_list_t;

/* brick data */
struct brickdata_t {
    spriteinfo_t *data; /* this is not stored in the main hash */
    image_t *image; /* pointer to the current brick image in the animation */
    int property; /* BRK_* */
    int behavior; /* BRB_* */
    int angle; /* in degrees, 0 <= angle < 360 */
    float zindex; /* 0.0 (background) <= z-index <= 1.0 (foreground) */
    float behavior_arg[BRICKBEHAVIOR_MAXARGS];
};




/* real bricks */
struct brick_t { /* a real, concrete brick */
    brickdata_t *brick_ref; /* brick metadata */
    int x, y; /* current position */
    int sx, sy; /* spawn point */
    int enabled; /* useful on sonic loops */
    int state; /* BRS_* */
    float value[BRICK_MAXVALUES]; /* alterable values */
    float animation_frame; /* controlled by a timer */
};

struct brick_list_t { /* linked list of bricks */
    brick_t *data;
    brick_list_t *next;
};


/* brick data */
void brickdata_load(const char *filename); /* loads a brick theme */
void brickdata_unload(); /* unloads the current brick theme */
brickdata_t *brickdata_get(int id); /* returns the specified brickdata */
int brickdata_size(); /* number of bricks */

/* brick utilities */
void brick_animate(brick_t *brk);
image_t *brick_image(brick_t *brk);
const char* brick_get_property_name(int property);
const char* brick_get_behavior_name(int behavior);


#endif
