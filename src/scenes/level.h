/*
 * level.h - code for the game levels
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

#ifndef _LEVEL_H
#define _LEVEL_H

#include "../core/v2d.h"
#include "../core/audio.h"
#include "../entities/player.h"
#include "../entities/item.h"
#include "../entities/enemy.h"
#include "../entities/brick.h"


/* use this before pushing the level scene into the stack */
void level_setfile(const char *level);

/* scene methods */
void level_init();
void level_update();
void level_render();
void level_release();

/* useful stuff */
float level_gravity();
int level_player_id();
void level_change_player(int id);
void level_create_particle(image_t *image, v2d_t position, v2d_t speed, int destroy_on_brick);
player_t* level_player();
brick_t* level_create_brick(int type, v2d_t position);
item_t* level_create_item(int type, v2d_t position);
enemy_t* level_create_enemy(const char *name, v2d_t position);
item_list_t* level_item_list();
enemy_list_t* level_enemy_list();
v2d_t level_brick_move_actor(brick_t *brick, actor_t *act);
void level_add_to_score(int score);
item_t* level_create_animal(v2d_t position);
void level_set_camera_focus(actor_t *act);
int level_editmode();
v2d_t level_size();
void level_override_music(sound_t *sample);
void level_set_spawn_point(v2d_t newpos);
void level_clear(actor_t *end_sign);
void level_add_to_secret_bonus(int value);
void level_call_dialogbox(char *title, char *message);
void level_hide_dialogbox();
int level_boss_battle();
void level_kill_all_baddies();
void level_lock_camera(int x1, int y1, int x2, int y2);
void level_unlock_camera();
void level_restore_music();

#endif
