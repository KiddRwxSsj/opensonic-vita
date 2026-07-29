/*
 * player.h - player module
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

#ifndef _PLAYER_H
#define _PLAYER_H

#include "actor.h"
#include "brick.h"

/* constants */
#define PLAYER_INITIAL_LIVES        5
#define PLAYER_MAX_INVSTAR          5
#define PLAYER_WALL_NONE            0
#define PLAYER_WALL_TOP             1
#define PLAYER_WALL_RIGHT           2
#define PLAYER_WALL_BOTTOM          4
#define PLAYER_WALL_LEFT            8
#define PLAYER_MAX_BLINK            7.0 /* how many seconds does the player must blink if he/she gets hurt? */
#define PLAYER_MAX_INVINCIBILITY    23.0 /* invincibility timer */
#define PLAYER_MAX_SPEEDSHOES       23.0 /* speed shoes timer */
#define TAILS_MAX_FLIGHT            10.0 /* tails can fly up to 10 seconds */
#define PLAYER_JUMP_SENSITIVITY     0.88 /* jump sensitivity */

/* player list */
#define PL_SONIC                    0
#define PL_TAILS                    1
#define PL_KNUCKLES                 2

/* shield list */
#define SH_NONE                     0 /* no shield */
#define SH_SHIELD                   1 /* regular shield */
#define SH_FIRESHIELD               2 /* fire shield */
#define SH_THUNDERSHIELD            3 /* thunder shield */
#define SH_WATERSHIELD              4 /* water shield */
#define SH_ACIDSHIELD               5 /* acid shield */
#define SH_WINDSHIELD               6 /* wind shield */

/* forward declarations */
typedef struct player_t player_t;

/* player structure */
struct player_t {
    /* general */
    char *name;
    int type; /* PL_* */
    actor_t *actor;
    int disable_movement;
    int in_locked_area;
    int at_some_border;

    /* movement data */
    int spin, spin_dash, braking, flying, climbing, landing, spring;
    int is_fire_jumping; /* am i jumping because IB_FIRE1 was pressed? */
    int on_moveable_platform;
    int lock_accel;
    float flight_timer;
    float disable_jump_for; /* disable jumping for how long? */

    /* got hurt? */
    int getting_hit; /* getting_hit gets FALSE if the player touches the ground */
    int blinking, dying, dead;
    float blink_timer; /* if the player is blinking, then it can't be hurt anymore */
    float death_timer;

    /* glasses */
    int got_glasses; /* got glasses? */
    actor_t *glasses;

    /* shields */
    int shield_type;
    actor_t *shield;

    /* invincibility */
    int invincible;
    float invtimer;
    actor_t* invstar[PLAYER_MAX_INVSTAR];

    /* speed shoes */
    int got_speedshoes;
    float speedshoes_timer;

    /* sonic loops (PLAYER_WALL_*) */
    int disable_wall;
    int entering_loop;
    int at_loopfloortop;
    int bring_to_back;
};

/* public functions */
player_t* player_create(int type);
void player_destroy(player_t *player);
void player_update(player_t *player, player_t *team[3], brick_list_t *brick_list);
void player_render(player_t *player, v2d_t camera_position);
v2d_t player_platform_movement(player_t *player, player_t *team[3], brick_list_t *brick_list, float gravity);
void player_hit(player_t *player);
void player_kill(player_t *player);
void player_bounce(player_t *player);
int player_attacking(player_t *player);
int player_get_rings();
void player_set_rings(int r);
int player_get_lives();
void player_set_lives(int l);
int player_get_score();
void player_set_score(int s);
const char *player_get_sprite_name(player_t *player);

#endif
