/*
 * boss.h - boss module
 * Copyright (C) 2008-2009  Alexandre Martins <alemartf(at)gmail(dot)com>
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

#ifndef _BOSS_H
#define _BOSS_H

#include "../core/util.h"
#include "player.h"

/* boss list */
#define BOSSDATA_MAX        4
#define BT_SIMPLEBOSS       0
#define BT_MECHASHADOW      1
#define BT_SIMPLEBOSSEX     2
#define BT_MECHASHADOWEX    3


/* boss state */
#define BS_IDLE             0
#define BS_DEAD             1
#define BS_ACTIVE           2 /* generic action */


/* boss direction */
#define BD_UP               0
#define BD_RIGHT            1
#define BD_DOWN             2
#define BD_LEFT             3


/* boss struct */
#define BOSSDATA_MAXVALUES  10
typedef struct {

    /* generic data */
    int type;
    int state;
    int direction;
    actor_t *actor;

    /* boss data */
    int bring_to_front;
    int initial_hp; /* initial hp */
    int hp; /* if the boss takes a hit, it loses 1 hp */
    float value[BOSSDATA_MAXVALUES]; /* alterable values */

    /* boss region */
    int rect_x, rect_y, rect_w, rect_h;

} boss_t;



/* public functions */
boss_t *boss_create(int type, v2d_t spawn_point, int rx, int ry, int rw, int rh);
void boss_destroy(boss_t *boss);
void boss_update(boss_t *boss, player_t *team[3], brick_list_t *brick_list);
void boss_render(boss_t *boss, v2d_t camera_position);
int boss_defeated(boss_t *boss); /* returns TRUE if the boss has been defeated. */

#endif
