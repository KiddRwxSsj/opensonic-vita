/*
 * falglasses.c - falling glasses
 * Copyright (C) 2010  Alexandre Martins <alemartf(at)gmail(dot)com>
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

#include "falglasses.h"
#include "../../core/util.h"
#include "../../core/timer.h"
#include "../../scenes/level.h"

/* falglasses class */
typedef struct falglasses_t falglasses_t;
struct falglasses_t {
    item_t item; /* base class */
};

static void falglasses_init(item_t *item);
static void falglasses_release(item_t* item);
static void falglasses_update(item_t* item, player_t** team, int team_size, brick_list_t* brick_list, item_list_t* item_list, enemy_list_t* enemy_list);
static void falglasses_render(item_t* item, v2d_t camera_position);



/* public methods */
item_t* falglasses_create()
{
    item_t *item = mallocx(sizeof(falglasses_t));

    item->init = falglasses_init;
    item->release = falglasses_release;
    item->update = falglasses_update;
    item->render = falglasses_render;

    return item;
}

void falglasses_set_speed(item_t *item, v2d_t speed)
{
    if(item->actor != NULL)
        item->actor->speed = speed;
}


/* private methods */
void falglasses_init(item_t *item)
{
    item->obstacle = FALSE;
    item->bring_to_back = FALSE;
    item->preserve = FALSE;
    item->actor = actor_create();

    actor_change_animation(item->actor, sprite_get_animation("SD_GLASSES", 4));
    item->actor->hot_spot.y *= 0.5f;
}



void falglasses_release(item_t* item)
{
    actor_destroy(item->actor);
}



void falglasses_update(item_t* item, player_t** team, int team_size, brick_list_t* brick_list, item_list_t* item_list, enemy_list_t* enemy_list)
{
    actor_t *act = item->actor;
    float dt = timer_get_delta();

    act->angle += sign(act->speed.x) * (6.0f * PI * dt);
    act->position = v2d_add(act->position, actor_particle_movement(act, level_gravity()));
}


void falglasses_render(item_t* item, v2d_t camera_position)
{
    actor_render(item->actor, camera_position);
}

