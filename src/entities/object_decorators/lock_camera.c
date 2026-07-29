/*
 * lock_camera.c - Locks an area of the playfield
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
#include "lock_camera.h"
#include "../../core/util.h"
#include "../../core/image.h"
#include "../../core/video.h"
#include "../../scenes/level.h"
#include "../../entities/player.h"

/* objectdecorator_lockcamera_t class */
typedef struct objectdecorator_lockcamera_t objectdecorator_lockcamera_t;
struct objectdecorator_lockcamera_t {
    objectdecorator_t base; /* inherits from objectdecorator_t */
    int x1, y1, x2, y2;
    image_t *cute_image;
    int has_locked_somebody;
};

/* private methods */
static void init(objectmachine_t *obj);
static void release(objectmachine_t *obj);
static void update(objectmachine_t *obj, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);
static void render(objectmachine_t *obj, v2d_t camera_position);

static image_t* create_cute_image(int w, int h);




/* public methods */

/* class constructor */
objectmachine_t* objectdecorator_lockcamera_new(objectmachine_t *decorated_machine, int x1, int y1, int x2, int y2)
{
    objectdecorator_lockcamera_t *me = mallocx(sizeof *me);
    objectdecorator_t *dec = (objectdecorator_t*)me;
    objectmachine_t *obj = (objectmachine_t*)dec;

    obj->init = init;
    obj->release = release;
    obj->update = update;
    obj->render = render;
    obj->get_object_instance = objectdecorator_get_object_instance; /* inherits from superclass */
    dec->decorated_machine = decorated_machine;

    me->x1 = min(x1, x2);
    me->y1 = min(y1, y2);
    me->x2 = max(x1, x2);
    me->y2 = max(y1, y2);

    return obj;
}





/* private methods */
void init(objectmachine_t *obj)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;
    objectdecorator_lockcamera_t *me = (objectdecorator_lockcamera_t*)obj;
    int w, h;

    w = abs(me->x2 - me->x1);
    h = abs(me->y2 - me->y1);

    if(w*h <= 0)
        fatal_error("The rectangle passed to lock_camera must have a positive area");

    me->cute_image = create_cute_image(w, h);
    me->has_locked_somebody = FALSE;

    decorated_machine->init(decorated_machine);
}

void release(objectmachine_t *obj)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;
    objectdecorator_lockcamera_t *me = (objectdecorator_lockcamera_t*)obj;
    player_t *player = enemy_get_observed_player(obj->get_object_instance(obj));

    image_destroy(me->cute_image);
    if(me->has_locked_somebody) {
        player->in_locked_area = FALSE;
        level_unlock_camera();
    }

    decorated_machine->release(decorated_machine);
    free(obj);
}

void update(objectmachine_t *obj, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;
    object_t *object = obj->get_object_instance(obj);
    player_t *player = enemy_get_observed_player(object);
    objectdecorator_lockcamera_t *me = (objectdecorator_lockcamera_t*)obj;
    actor_t *act = object->actor, *ta;
    float rx, ry, rw, rh;
    int i;

    /* my rectangle, in world coordinates */
    rx = act->position.x + me->x1;
    ry = act->position.y + me->y1;
    rw = me->x2 - me->x1;
    rh = me->y2 - me->y1;

    /* only the observed player can enter this area */
    for(i=0; i<team_size; i++) {
        ta = team[i]->actor;

        if(team[i] != player || (NULL != ta->carrying)) {
            /* hey, you can't enter here! */
            float border = 30.0f;
            if(ta->position.x > rx - border && ta->position.x < rx) {
                ta->position.x = rx - border;
                ta->speed.x = 0.0f;
            }
            if(ta->position.x > rx + rw && ta->position.x < rx + rw + border) {
                ta->position.x = rx + rw + border;
                ta->speed.x = 0.0f;
            }
        }
        else {
            /* test if the player has got inside my rectangle */
            float a[4], b[4];

            a[0] = ta->position.x;
            a[1] = ta->position.y;
            a[2] = ta->position.x + 1;
            a[3] = ta->position.y + 1;

            b[0] = rx;
            b[1] = ry;
            b[2] = rx + rw;
            b[3] = ry + rh;

            if(bounding_box(a, b)) {
                /* welcome, player! You have been locked. BWHAHAHA!!! */
                me->has_locked_somebody = TRUE;
                team[i]->in_locked_area = TRUE;
                level_lock_camera(rx, ry, rx+rw, ry+rh);
            }
        }
    }


    /* cage */
    if(me->has_locked_somebody) {
        ta = player->actor;
        if(ta->position.x < rx) {
            ta->position.x = rx;
            ta->speed.x = max(0.0f, ta->speed.x);
            player->at_some_border = TRUE;
        }
        if(ta->position.x > rx + rw) {
            ta->position.x = rx + rw;
            ta->speed.x = min(0.0f, ta->speed.x);
            player->at_some_border = TRUE;
        }
        ta->position.y = clip(ta->position.y, ry, ry + rh);
    }

    decorated_machine->update(decorated_machine, team, team_size, brick_list, item_list, object_list);
}

void render(objectmachine_t *obj, v2d_t camera_position)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    if(level_editmode()) {
        objectdecorator_lockcamera_t *me = (objectdecorator_lockcamera_t*)obj;
        actor_t *act = obj->get_object_instance(obj)->actor;
        int x, y;

        x = (act->position.x + me->x1) - (camera_position.x - VIDEO_SCREEN_W/2);
        y = (act->position.y + me->y1) - (camera_position.y - VIDEO_SCREEN_H/2);
        image_draw(me->cute_image, video_get_backbuffer(), x, y, IF_NONE);
    }

    decorated_machine->render(decorated_machine, camera_position);
}


image_t* create_cute_image(int w, int h)
{
    image_t *image = image_create(w, h);
    uint32 color = image_rgb(255, 0, 0);

    image_clear(image, video_get_maskcolor());
    image_line(image, 0, 0, w-1, 0, color);
    image_line(image, 0, 0, 0, h-1, color);
    image_line(image, w-1, h-1, w-1, 0, color);
    image_line(image, w-1, h-1, 0, h-1, color);

    return image;
}
