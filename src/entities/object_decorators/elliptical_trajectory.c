/*
 * elliptical_trajectory.h - This decorator makes the object follow an elliptical trajectory
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
#include "elliptical_trajectory.h"
#include "../../core/util.h"
#include "../../core/timer.h"

/* objectdecorator_ellipticaltrajectory_t class */
typedef struct objectdecorator_ellipticaltrajectory_t objectdecorator_ellipticaltrajectory_t;
struct objectdecorator_ellipticaltrajectory_t {
    objectdecorator_t base; /* inherits from objectdecorator_t */
    float amplitude_x, amplitude_y; /* distance from the center of the ellipsis (actor's spawn point) */
    float angularspeed_x, angularspeed_y; /* speed, in radians per second */
    float initialphase_x, initialphase_y; /* initial phase in degrees */
};

/* private methods */
static void init(objectmachine_t *obj);
static void release(objectmachine_t *obj);
static void update(objectmachine_t *obj, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);
static void render(objectmachine_t *obj, v2d_t camera_position);



/* public methods */

/* class constructor */
objectmachine_t* objectdecorator_ellipticaltrajectory_new(objectmachine_t *decorated_machine, float amplitude_x, float amplitude_y, float angularspeed_x, float angularspeed_y, float initialphase_x, float initialphase_y)
{
    objectdecorator_ellipticaltrajectory_t *me = mallocx(sizeof *me);
    objectdecorator_t *dec = (objectdecorator_t*)me;
    objectmachine_t *obj = (objectmachine_t*)dec;

    obj->init = init;
    obj->release = release;
    obj->update = update;
    obj->render = render;
    obj->get_object_instance = objectdecorator_get_object_instance; /* inherits from superclass */
    dec->decorated_machine = decorated_machine;
    me->amplitude_x = amplitude_x;
    me->amplitude_y = amplitude_y;
    me->angularspeed_x = angularspeed_x * (2.0f * PI);
    me->angularspeed_y = angularspeed_y * (2.0f * PI);
    me->initialphase_x = (initialphase_x * PI) / 180.0f;
    me->initialphase_y = (initialphase_y * PI) / 180.0f;

    return obj;
}




/* private methods */
void init(objectmachine_t *obj)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    ; /* empty */

    decorated_machine->init(decorated_machine);
}

void release(objectmachine_t *obj)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    ; /* empty */

    decorated_machine->release(decorated_machine);
    free(obj);
}

void update(objectmachine_t *obj, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;
    objectdecorator_ellipticaltrajectory_t *me = (objectdecorator_ellipticaltrajectory_t*)obj;
    object_t *object = obj->get_object_instance(obj);
    actor_t *act = object->actor;
    float dt = timer_get_delta();
    brick_t *up = NULL, *upright = NULL, *right = NULL, *downright = NULL;
    brick_t *down = NULL, *downleft = NULL, *left = NULL, *upleft = NULL;
    float sqrsize = 0, diff = 0;
    float elapsed_time = timer_get_ticks() * 0.001f;
    v2d_t old_position = act->position;

    /* elliptical trajectory */
    /*
        let C: R -> R^2 be such that:
            C(t) = (
                Ax * cos( Ix + Sx*t ) + Px,
                Ay * sin( Iy + Sy*t ) + Py
            )

        where:
            t  = elapsed_time       (in seconds)
            Ax = me->amplitude_x    (in pixels)
            Ay = me->amplitude_y    (in pixels)
            Sx = me->angularspeed_x (in radians per second)
            Sy = me->angularspeed_y (in radians per second)
            Ix = me->initialphase_x (in radians)
            Iy = me->initialphase_y (in radians)
            Px = act->spawn_point.x (in pixels)
            Py = act->spawn_point.y (in pixels)

        then:
            C'(t) = (
                -Ax * Sx * sin( Ix + Sx*t ),
                 Ay * Sy * cos( Iy + Sy*t )
            )
    */
    act->position.x += (-me->amplitude_x * me->angularspeed_x * sin( me->initialphase_x + me->angularspeed_x * elapsed_time)) * dt;
    act->position.y += ( me->amplitude_y * me->angularspeed_y * cos( me->initialphase_y + me->angularspeed_y * elapsed_time)) * dt;

    /* sensors */
    actor_corners(act, sqrsize, diff, brick_list, &up, &upright, &right, &downright, &down, &downleft, &left, &upleft);
    actor_handle_clouds(act, diff, &up, &upright, &right, &downright, &down, &downleft, &left, &upleft);

    /* I don't want to get stuck into walls */
    if(right != NULL) {
        if(act->position.x > old_position.x)
            act->position.x = act->hot_spot.x - actor_image(act)->w + right->x;
    }

    if(left != NULL) {
        if(act->position.x < old_position.x)
            act->position.x = act->hot_spot.x + left->x + brick_image(left)->w;
    }

    if(down != NULL) {
        if(act->position.y > old_position.y)
            act->position.y = act->hot_spot.y - actor_image(act)->h + down->y;
    }

    if(up != NULL) {
        if(act->position.y < old_position.y)
            act->position.y = act->hot_spot.y + up->y + brick_image(up)->h;
    }

    /* decorator pattern */
    decorated_machine->update(decorated_machine, team, team_size, brick_list, item_list, object_list);
}

void render(objectmachine_t *obj, v2d_t camera_position)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    ; /* empty */

    decorated_machine->render(decorated_machine, camera_position);
}

