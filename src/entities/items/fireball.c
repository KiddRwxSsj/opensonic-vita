/*
 * fireball.c - fire ball
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

#include "fireball.h"
#include "../../core/util.h"
#include "../../core/soundfactory.h"
#include "../../scenes/level.h"

/* fireball class */
typedef struct fireball_t fireball_t;
struct fireball_t {
    item_t item; /* base class */
    void (*run)(item_t*,brick_list_t*);
};

static void fireball_init(item_t *item);
static void fireball_release(item_t* item);
static void fireball_update(item_t* item, player_t** team, int team_size, brick_list_t* brick_list, item_list_t* item_list, enemy_list_t* enemy_list);
static void fireball_render(item_t* item, v2d_t camera_position);

static void fireball_set_behavior(item_t *fireball, void (*behavior)(item_t*,brick_list_t*));

static void falling_behavior(item_t *fireball, brick_list_t *brick_list);
static void disappearing_behavior(item_t *fireball, brick_list_t *brick_list);
static void smallfire_behavior(item_t *fireball, brick_list_t *brick_list);



/* public methods */
item_t* fireball_create()
{
    item_t *item = mallocx(sizeof(fireball_t));

    item->init = fireball_init;
    item->release = fireball_release;
    item->update = fireball_update;
    item->render = fireball_render;

    return item;
}


/* private methods */
void fireball_set_behavior(item_t *fireball, void (*behavior)(item_t*,brick_list_t*))
{
    fireball_t *me = (fireball_t*)fireball;
    me->run = behavior;
}

void fireball_init(item_t *item)
{
    item->obstacle = FALSE;
    item->bring_to_back = FALSE;
    item->preserve = FALSE;
    item->actor = actor_create();

    fireball_set_behavior(item, falling_behavior);
    actor_change_animation(item->actor, sprite_get_animation("SD_FIREBALL", 0));
}



void fireball_release(item_t* item)
{
    actor_destroy(item->actor);
}



void fireball_update(item_t* item, player_t** team, int team_size, brick_list_t* brick_list, item_list_t* item_list, enemy_list_t* enemy_list)
{
    int i;
    actor_t *act = item->actor;
    fireball_t *me = (fireball_t*)item;

    /* hit the player */
    for(i=0; i<team_size; i++) {
        player_t *player = team[i];
        if(!player->dying && actor_collision(act, player->actor)) {
            item->state = IS_DEAD;
            if(player->shield_type != SH_FIRESHIELD)
                player_hit(player);
        }
    }

    /* my behavior */
    me->run(item, brick_list);
}


void fireball_render(item_t* item, v2d_t camera_position)
{
    actor_render(item->actor, camera_position);
}


/* private behaviors */
void falling_behavior(item_t *fireball, brick_list_t *brick_list)
{
    int i, n;
    float sqrsize = 2, diff = -2;
    actor_t *act = fireball->actor;
    brick_t *down;

    /* movement & animation */
    act->speed.x = 0.0f;
    act->mirror = (act->speed.y < 0.0f) ? IF_VFLIP : IF_NONE;
    actor_move(act, actor_particle_movement(act, level_gravity()));
    actor_change_animation(act, sprite_get_animation("SD_FIREBALL", 0));

    /* collision detection */
    actor_corners(act, sqrsize, diff, brick_list, NULL, NULL, NULL, NULL, &down, NULL, NULL, NULL);
    actor_handle_clouds(act, diff, NULL, NULL, NULL, NULL, &down, NULL, NULL, NULL);
    if(down) {
        /* I have just touched the ground */
        fireball_set_behavior(fireball, disappearing_behavior);
        sound_play( soundfactory_get("fire2") );

        /* create small fire balls */
        n = 2 + random(3);
        for(i=0; i<n; i++) {
            item_t *obj = level_create_item(IT_FIREBALL, act->position);
            fireball_set_behavior(obj, smallfire_behavior);
            obj->actor->speed = v2d_new(((float)i/(float)n)*400.0f-200.0f, -120.0f-random(240.0f));
        }
    }
}

void disappearing_behavior(item_t *fireball, brick_list_t *brick_list)
{
    actor_t *act = fireball->actor;

    actor_change_animation(act, sprite_get_animation("SD_FIREBALL", 1));
    if(actor_animation_finished(act))
        fireball->state = IS_DEAD;
}

void smallfire_behavior(item_t *fireball, brick_list_t *brick_list)
{
    float sqrsize = 2, diff = -2;
    actor_t *act = fireball->actor;
    brick_t *down;

    /* movement & animation */
    actor_move(act, actor_particle_movement(act, level_gravity()));
    actor_change_animation(act, sprite_get_animation("SD_FIREBALL", 2));

    /* collision detection */
    actor_corners(act, sqrsize, diff, brick_list, NULL, NULL, NULL, NULL, &down, NULL, NULL, NULL);
    actor_handle_clouds(act, diff, NULL, NULL, NULL, NULL, &down, NULL, NULL, NULL);
    if(down && act->speed.y > 0.0f)
        fireball->state = IS_DEAD;
}

