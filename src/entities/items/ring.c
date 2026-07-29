/*
 * ring.c - rings
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

#include <math.h>
#include "ring.h"
#include "../../scenes/level.h"
#include "../../core/util.h"
#include "../../core/timer.h"
#include "../../core/audio.h"
#include "../../core/soundfactory.h"

/* ring class */
typedef struct ring_t ring_t;
struct ring_t {
    item_t item; /* base class */
    int is_disappearing; /* is this ring disappearing? */
    int is_moving; /* is this ring moving (bouncing) around? */
    float life_time; /* life time (used to destroy the moving ring after some time) */
};

static void ring_init(item_t *item);
static void ring_release(item_t* item);
static void ring_update(item_t* item, player_t** team, int team_size, brick_list_t* brick_list, item_list_t* item_list, enemy_list_t* enemy_list);
static void ring_render(item_t* item, v2d_t camera_position);



/* public methods */
item_t* ring_create()
{
    item_t *item = mallocx(sizeof(ring_t));

    item->init = ring_init;
    item->release = ring_release;
    item->update = ring_update;
    item->render = ring_render;

    return item;
}

void ring_start_bouncing(item_t *ring)
{
    ring_t *me = (ring_t*)ring;

    me->is_moving = TRUE;
    ring->actor->speed.x = ring->actor->maxspeed * (random(100)-50)/100;
    ring->actor->speed.y = -ring->actor->jump_strength + random(ring->actor->jump_strength);
}



/* private methods */
void ring_init(item_t *item)
{
    ring_t *me = (ring_t*)item;

    item->obstacle = FALSE;
    item->bring_to_back = FALSE;
    item->preserve = TRUE;
    item->actor = actor_create();
    item->actor->maxspeed = 220 + random(140);
    item->actor->jump_strength = (350 + random(50)) * 1.2;
    item->actor->input = input_create_computer();

    me->is_disappearing = FALSE;
    me->is_moving = FALSE;
    me->life_time = 0.0f;

    actor_change_animation(item->actor, sprite_get_animation("SD_RING", 0));
}



void ring_release(item_t* item)
{
    actor_destroy(item->actor);
}



void ring_update(item_t* item, player_t** team, int team_size, brick_list_t* brick_list, item_list_t* item_list, enemy_list_t* enemy_list)
{
    int i;
    float dt = timer_get_delta();
    ring_t *me = (ring_t*)item;
    actor_t *act = item->actor;

    /* a player has just got this ring */
    for(i=0; i<team_size; i++) {
        player_t *player = team[i];
        if(
            (!me->is_moving || (me->is_moving && !player->getting_hit && me->life_time >= 0.5f)) &&
            !me->is_disappearing &&
            !player->dying &&
            actor_collision(act, player->actor)
        ) {
            player_set_rings(player_get_rings() + 1);
            me->is_disappearing = TRUE;
            sound_play( soundfactory_get("ring") );
            break;
        }
    }

    /* disappearing animation... */
    if(me->is_disappearing) {
        actor_change_animation(act, sprite_get_animation("SD_RING", 1));
        if(actor_animation_finished(act))
            item->state = IS_DEAD;
    }

    /* this ring is bouncing around... */
    else if(me->is_moving) {
        float sqrsize = 2, diff = -2;
        brick_t *left = NULL, *right = NULL, *down = NULL;
        actor_corners(act, sqrsize, diff, brick_list, NULL, NULL, &right, NULL, &down, NULL, &left, NULL);
        actor_handle_clouds(act, diff, NULL, NULL, &right, NULL, &down, NULL, &left, NULL);
        input_simulate_button_down(act->input, IB_FIRE1);
        item->preserve = FALSE;

        /* who wants to live forever? */
        me->life_time += dt;
        if(me->life_time > 5.0f) {
            int val = 240 + random(20);
            act->visible = ((int)(timer_get_ticks() % val) < val/2);
            if(me->life_time > 8.0f)
                item->state = IS_DEAD;
        }

        /* keep moving... */
        if(right && act->speed.x > 0.0f)
            act->speed.x = -fabs(act->speed.x);

        if(left && act->speed.x < 0.0f)
            act->speed.x = fabs(act->speed.x);

        if(down && act->speed.y > 0.0f)
            act->jump_strength *= 0.95f;

        actor_move(act, actor_platform_movement(act, brick_list, level_gravity()));
    }

    /* this ring is being attracted by the thunder shield */
    else if(level_player()->shield_type == SH_THUNDERSHIELD) {
        float threshold = 120.0f;
        v2d_t diff = v2d_subtract(level_player()->actor->position, act->position);
        if(v2d_magnitude(diff) < threshold) {
            float speed = 320.0f;
            v2d_t d = v2d_multiply(v2d_normalize(diff), speed);
            act->position.x += d.x * dt;
            act->position.y += d.y * dt;
        }
    }
}


void ring_render(item_t* item, v2d_t camera_position)
{
    actor_render(item->actor, camera_position);
}

