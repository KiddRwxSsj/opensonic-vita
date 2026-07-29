/*
 * on_event.c - Events: if an event is true, then the state is changed
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

#include <string.h>
#include "on_event.h"
#include "../object_vm.h"
#include "../../core/util.h"
#include "../../core/stringutil.h"
#include "../../core/timer.h"
#include "../../scenes/level.h"

/* forward declarations */
typedef struct objectdecorator_onevent_t objectdecorator_onevent_t;
typedef struct eventstrategy_t eventstrategy_t;
typedef struct ontimeout_t ontimeout_t;
typedef struct oncollision_t oncollision_t;
typedef struct onanimationfinished_t onanimationfinished_t;
typedef struct onrandomevent_t onrandomevent_t;
typedef struct onplayercollision_t onplayercollision_t;
typedef struct onplayerattack_t onplayerattack_t;
typedef struct onplayerrectcollision_t onplayerrectcollision_t;
typedef struct onplayershield_t onplayershield_t;
typedef struct onbrickcollision_t onbrickcollision_t;
typedef struct onfloorcollision_t onfloorcollision_t;
typedef struct onceilingcollision_t onceilingcollision_t;
typedef struct onleftwallcollision_t onleftwallcollision_t;
typedef struct onrightwallcollision_t onrightwallcollision_t;

/* objectdecorator_onevent_t class */
struct objectdecorator_onevent_t {
    objectdecorator_t base; /* inherits from objectdecorator_t */
    char *new_state_name; /* state name */
    eventstrategy_t *strategy; /* strategy pattern */
};

/* <<interface>> eventstrategy_t */
struct eventstrategy_t {
    void (*init)(eventstrategy_t*); /* initializes the strategy object */
    void (*release)(eventstrategy_t*); /* releases the strategy object */
    int (*should_trigger_event)(eventstrategy_t*,object_t*,player_t**,int,brick_list_t*,item_list_t*,object_list_t*); /* returns TRUE iff the event should be triggered */
};

/* ontimeout_t concrete strategy */
struct ontimeout_t {
    eventstrategy_t base; /* implements eventstrategy_t */
    float timeout; /* timeout value */
    float timer; /* time accumulator */
};
static eventstrategy_t* ontimeout_new(float timeout);
static void ontimeout_init(eventstrategy_t *event);
static void ontimeout_release(eventstrategy_t *event);
static int ontimeout_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* oncollision_t concrete strategy */
struct oncollision_t {
    eventstrategy_t base; /* implements eventstrategy_t */
    char *target_name; /* object name */
};
static eventstrategy_t* oncollision_new(const char *target_name);
static void oncollision_init(eventstrategy_t *event);
static void oncollision_release(eventstrategy_t *event);
static int oncollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onanimationfinished_t concrete strategy */
struct onanimationfinished_t {
    eventstrategy_t base; /* implements eventstrategy_t */
};
static eventstrategy_t* onanimationfinished_new();
static void onanimationfinished_init(eventstrategy_t *event);
static void onanimationfinished_release(eventstrategy_t *event);
static int onanimationfinished_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onrandomevent_t concrete strategy */
struct onrandomevent_t {
    eventstrategy_t base; /* implements eventstrategy_t */
    float probability; /* 0.0 <= probability <= 1.0 */
};
static eventstrategy_t* onrandomevent_new(float probability);
static void onrandomevent_init(eventstrategy_t *event);
static void onrandomevent_release(eventstrategy_t *event);
static int onrandomevent_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onplayercollision_t concrete strategy */
struct onplayercollision_t {
    eventstrategy_t base; /* implements eventstrategy_t */
};
static eventstrategy_t* onplayercollision_new();
static void onplayercollision_init(eventstrategy_t *event);
static void onplayercollision_release(eventstrategy_t *event);
static int onplayercollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onplayerattack_t concrete strategy */
struct onplayerattack_t {
    eventstrategy_t base; /* implements eventstrategy_t */
};
static eventstrategy_t* onplayerattack_new();
static void onplayerattack_init(eventstrategy_t *event);
static void onplayerattack_release(eventstrategy_t *event);
static int onplayerattack_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onplayerrectcollision_t concrete strategy */
struct onplayerrectcollision_t {
    eventstrategy_t base; /* implements eventstrategy_t */
    int x1, y1, x2, y2; /* rectangle offsets (related to the hotspot of the holder object */
};
static eventstrategy_t* onplayerrectcollision_new(int x1, int y1, int x2, int y2);
static void onplayerrectcollision_init(eventstrategy_t *event);
static void onplayerrectcollision_release(eventstrategy_t *event);
static int onplayerrectcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onplayershield_t concrete strategy */
struct onplayershield_t {
    eventstrategy_t base; /* implements eventstrategy_t */
    int shield_type; /* a SH_* constant defined at player.h */
};
static eventstrategy_t* onplayershield_new(int shield_type);
static void onplayershield_init(eventstrategy_t *event);
static void onplayershield_release(eventstrategy_t *event);
static int onplayershield_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onbrickcollision_t concrete strategy */
struct onbrickcollision_t {
    eventstrategy_t base; /* implements eventstrategy_t */
};
static eventstrategy_t* onbrickcollision_new();
static void onbrickcollision_init(eventstrategy_t *event);
static void onbrickcollision_release(eventstrategy_t *event);
static int onbrickcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onfloorcollision_t concrete strategy */
struct onfloorcollision_t {
    eventstrategy_t base; /* implements eventstrategy_t */
};
static eventstrategy_t* onfloorcollision_new();
static void onfloorcollision_init(eventstrategy_t *event);
static void onfloorcollision_release(eventstrategy_t *event);
static int onfloorcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onceilingcollision_t concrete strategy */
struct onceilingcollision_t {
    eventstrategy_t base; /* implements eventstrategy_t */
};
static eventstrategy_t* onceilingcollision_new();
static void onceilingcollision_init(eventstrategy_t *event);
static void onceilingcollision_release(eventstrategy_t *event);
static int onceilingcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onleftwallcollision_t concrete strategy */
struct onleftwallcollision_t {
    eventstrategy_t base; /* implements eventstrategy_t */
};
static eventstrategy_t* onleftwallcollision_new();
static void onleftwallcollision_init(eventstrategy_t *event);
static void onleftwallcollision_release(eventstrategy_t *event);
static int onleftwallcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* onrightwallcollision_t concrete strategy */
struct onrightwallcollision_t {
    eventstrategy_t base; /* implements eventstrategy_t */
};
static eventstrategy_t* onrightwallcollision_new();
static void onrightwallcollision_init(eventstrategy_t *event);
static void onrightwallcollision_release(eventstrategy_t *event);
static int onrightwallcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);

/* private methods */
static objectmachine_t *make_decorator(objectmachine_t *decorated_machine, const char *new_state_name, eventstrategy_t *strategy);
static void init(objectmachine_t *obj);
static void release(objectmachine_t *obj);
static void update(objectmachine_t *obj, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);
static void render(objectmachine_t *obj, v2d_t camera_position);




/* ---------------------------------- */

/* public methods */

objectmachine_t* objectdecorator_ontimeout_new(objectmachine_t *decorated_machine, float timeout, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, ontimeout_new(timeout));
}

objectmachine_t* objectdecorator_oncollision_new(objectmachine_t *decorated_machine, const char *target_name, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, oncollision_new(target_name));
}

objectmachine_t* objectdecorator_onanimationfinished_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onanimationfinished_new());
}

objectmachine_t* objectdecorator_onrandomevent_new(objectmachine_t *decorated_machine, float probability, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onrandomevent_new(probability));
}

objectmachine_t* objectdecorator_onplayercollision_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayercollision_new());
}

objectmachine_t* objectdecorator_onplayerattack_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayerattack_new());
}

objectmachine_t* objectdecorator_onplayerrectcollision_new(objectmachine_t *decorated_machine, int x1, int y1, int x2, int y2, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayerrectcollision_new(x1,y1,x2,y2));
}

objectmachine_t* objectdecorator_onnoshield_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayershield_new(SH_NONE));
}

objectmachine_t* objectdecorator_onshield_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayershield_new(SH_SHIELD));
}

objectmachine_t* objectdecorator_onfireshield_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayershield_new(SH_FIRESHIELD));
}

objectmachine_t* objectdecorator_onthundershield_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayershield_new(SH_THUNDERSHIELD));
}

objectmachine_t* objectdecorator_onwatershield_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayershield_new(SH_WATERSHIELD));
}

objectmachine_t* objectdecorator_onacidshield_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayershield_new(SH_ACIDSHIELD));
}

objectmachine_t* objectdecorator_onwindshield_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onplayershield_new(SH_WINDSHIELD));
}

objectmachine_t* objectdecorator_onbrickcollision_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onbrickcollision_new());
}

objectmachine_t* objectdecorator_onfloorcollision_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onfloorcollision_new());
}

objectmachine_t* objectdecorator_onceilingcollision_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onceilingcollision_new());
}

objectmachine_t* objectdecorator_onleftwallcollision_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onleftwallcollision_new());
}

objectmachine_t* objectdecorator_onrightwallcollision_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    return make_decorator(decorated_machine, new_state_name, onrightwallcollision_new());
}

/* ---------------------------------- */

/* private methods */

objectmachine_t *make_decorator(objectmachine_t *decorated_machine, const char *new_state_name, eventstrategy_t *strategy)
{
    objectdecorator_onevent_t *me = mallocx(sizeof *me);
    objectdecorator_t *dec = (objectdecorator_t*)me;
    objectmachine_t *obj = (objectmachine_t*)dec;

    obj->init = init;
    obj->release = release;
    obj->update = update;
    obj->render = render;
    obj->get_object_instance = objectdecorator_get_object_instance; /* inherits from superclass */
    dec->decorated_machine = decorated_machine;
    me->new_state_name = str_dup(new_state_name);
    me->strategy = strategy;

    return obj;
}

void init(objectmachine_t *obj)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectdecorator_onevent_t *me = (objectdecorator_onevent_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    me->strategy->init(me->strategy);

    decorated_machine->init(decorated_machine);
}

void release(objectmachine_t *obj)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectdecorator_onevent_t *me = (objectdecorator_onevent_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    me->strategy->release(me->strategy);
    free(me->strategy);
    free(me->new_state_name);

    decorated_machine->release(decorated_machine);
    free(obj);
}

void update(objectmachine_t *obj, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;
    objectdecorator_onevent_t *me = (objectdecorator_onevent_t*)obj;
    object_t *object = obj->get_object_instance(obj);

    if(me->strategy->should_trigger_event(me->strategy, object, team, team_size, brick_list, item_list, object_list))
        objectvm_set_current_state(object->vm, me->new_state_name);
    else
        decorated_machine->update(decorated_machine, team, team_size, brick_list, item_list, object_list);
}

void render(objectmachine_t *obj, v2d_t camera_position)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    ; /* empty */

    decorated_machine->render(decorated_machine, camera_position);
}


/* ---------------------------------- */

/* ontimeout_t strategy */
eventstrategy_t* ontimeout_new(float timeout)
{
    ontimeout_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = ontimeout_init;
    e->release = ontimeout_release;
    e->should_trigger_event = ontimeout_should_trigger_event;

    x->timeout = timeout;
    x->timer = 0.0f;

    return e;
}

void ontimeout_init(eventstrategy_t *event)
{
    ; /* empty */
}

void ontimeout_release(eventstrategy_t *event)
{
    ; /* empty */
}

int ontimeout_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    ontimeout_t *x = (ontimeout_t*)event;

    x->timer += timer_get_delta();
    if(x->timer >= x->timeout) {
        x->timer = 0.0f;
        return TRUE;
    }

    return FALSE;
}

/* oncollision_t strategy */
eventstrategy_t* oncollision_new(const char *target_name)
{
    oncollision_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = oncollision_init;
    e->release = oncollision_release;
    e->should_trigger_event = oncollision_should_trigger_event;
    x->target_name = str_dup(target_name);

    return e;
}

void oncollision_init(eventstrategy_t *event)
{
    ; /* empty */
}

void oncollision_release(eventstrategy_t *event)
{
    oncollision_t *e = (oncollision_t*)event;
    free(e->target_name);
}

int oncollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    oncollision_t *x = (oncollision_t*)event;
    object_list_t *it;

    for(it = object_list; it != NULL; it = it->next) {
        if(strcmp(it->data->name, x->target_name) == 0) {
            if(actor_pixelperfect_collision(it->data->actor, object->actor))
                return TRUE;
        }
    }

    return FALSE;
}


/* onanimationfinished_t strategy */
eventstrategy_t* onanimationfinished_new()
{
    onanimationfinished_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onanimationfinished_init;
    e->release = onanimationfinished_release;
    e->should_trigger_event = onanimationfinished_should_trigger_event;

    return e;
}

void onanimationfinished_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onanimationfinished_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onanimationfinished_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    return actor_animation_finished(object->actor);
}

/* onrandomevent_t strategy */
eventstrategy_t* onrandomevent_new(float probability)
{
    onrandomevent_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onrandomevent_init;
    e->release = onrandomevent_release;
    e->should_trigger_event = onrandomevent_should_trigger_event;

    x->probability = clip(probability, 0.0f, 1.0f);

    return e;
}

void onrandomevent_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onrandomevent_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onrandomevent_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    int r = 100000 * ((onrandomevent_t*)event)->probability;
    return r > random(100000);
}


/* onplayercollision_t strategy */
eventstrategy_t* onplayercollision_new()
{
    onplayercollision_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onplayercollision_init;
    e->release = onplayercollision_release;
    e->should_trigger_event = onplayercollision_should_trigger_event;

    return e;
}

void onplayercollision_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onplayercollision_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onplayercollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    player_t *player = enemy_get_observed_player(object);
    return actor_pixelperfect_collision(object->actor, player->actor);
}


/* onplayerattack_t strategy */
eventstrategy_t* onplayerattack_new()
{
    onplayerattack_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onplayerattack_init;
    e->release = onplayerattack_release;
    e->should_trigger_event = onplayerattack_should_trigger_event;

    return e;
}

void onplayerattack_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onplayerattack_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onplayerattack_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    player_t *player = enemy_get_observed_player(object);
    return player_attacking(player) && actor_pixelperfect_collision(object->actor, player->actor);
}


/* onplayerrectcollision_t strategy */
eventstrategy_t* onplayerrectcollision_new(int x1, int y1, int x2, int y2)
{
    onplayerrectcollision_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onplayerrectcollision_init;
    e->release = onplayerrectcollision_release;
    e->should_trigger_event = onplayerrectcollision_should_trigger_event;

    x->x1 = min(x1, x2);
    x->y1 = min(y1, y2);
    x->x2 = max(x1, x2);
    x->y2 = max(y1, y2);

    return e;
}

void onplayerrectcollision_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onplayerrectcollision_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onplayerrectcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    onplayerrectcollision_t *me = (onplayerrectcollision_t*)event;
    actor_t *act = object->actor;
    player_t *player = enemy_get_observed_player(object);
    actor_t *pa = player->actor;
    image_t *pi = actor_image(pa);
    float a[4], b[4];

    a[0] = act->position.x + me->x1;
    a[1] = act->position.y + me->y1;
    a[2] = act->position.x + me->x2;
    a[3] = act->position.y + me->y2;

    b[0] = pa->position.x - pa->hot_spot.x;
    b[1] = pa->position.y - pa->hot_spot.y;
    b[2] = pa->position.x - pa->hot_spot.x + pi->w;
    b[3] = pa->position.y - pa->hot_spot.y + pi->h;

    return !player->dying && bounding_box(a, b);
}

/* onplayershield_t strategy */
eventstrategy_t* onplayershield_new(int shield_type)
{
    onplayershield_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onplayershield_init;
    e->release = onplayershield_release;
    e->should_trigger_event = onplayershield_should_trigger_event;
    x->shield_type = shield_type;

    return e;
}

void onplayershield_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onplayershield_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onplayershield_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    onplayershield_t *me = (onplayershield_t*)event;
    player_t *player = enemy_get_observed_player(object);

    return player->shield_type == me->shield_type;
}


/* onbrickcollision_t strategy */
eventstrategy_t* onbrickcollision_new()
{
    onbrickcollision_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onbrickcollision_init;
    e->release = onbrickcollision_release;
    e->should_trigger_event = onbrickcollision_should_trigger_event;

    return e;
}

void onbrickcollision_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onbrickcollision_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onbrickcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    const float sqrsize=1, diff=0;
    actor_t *act = object->actor;
    brick_t *up, *upright, *right, *downright, *down, *downleft, *left, *upleft;

    actor_corners(act, sqrsize, diff, brick_list, &up, &upright, &right, &downright, &down, &downleft, &left, &upleft);

    return
        (up != NULL && up->brick_ref->property == BRK_OBSTACLE) ||
        (upright != NULL && upright->brick_ref->property == BRK_OBSTACLE) ||
        (right != NULL && right->brick_ref->property == BRK_OBSTACLE) ||
        (downright != NULL && downright->brick_ref->property != BRK_NONE) ||
        (down != NULL && down->brick_ref->property != BRK_NONE) ||
        (downleft != NULL && downleft->brick_ref->property != BRK_NONE) ||
        (left != NULL && left->brick_ref->property == BRK_OBSTACLE) ||
        (upleft != NULL && upleft->brick_ref->property == BRK_OBSTACLE)
    ;
}

/* onfloorcollision_t strategy */
eventstrategy_t* onfloorcollision_new()
{
    onfloorcollision_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onfloorcollision_init;
    e->release = onfloorcollision_release;
    e->should_trigger_event = onfloorcollision_should_trigger_event;

    return e;
}

void onfloorcollision_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onfloorcollision_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onfloorcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    const float sqrsize=1, diff=0;
    actor_t *act = object->actor;
    brick_t *up, *upright, *right, *downright, *down, *downleft, *left, *upleft;

    actor_corners(act, sqrsize, diff, brick_list, &up, &upright, &right, &downright, &down, &downleft, &left, &upleft);

    return
        (downright != NULL && downright->brick_ref->property != BRK_NONE) ||
        (down != NULL && down->brick_ref->property != BRK_NONE) ||
        (downleft != NULL && downleft->brick_ref->property != BRK_NONE)
    ;
}

/* onceilingcollision_t strategy */
eventstrategy_t* onceilingcollision_new()
{
    onceilingcollision_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onceilingcollision_init;
    e->release = onceilingcollision_release;
    e->should_trigger_event = onceilingcollision_should_trigger_event;

    return e;
}

void onceilingcollision_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onceilingcollision_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onceilingcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    const float sqrsize=1, diff=0;
    actor_t *act = object->actor;
    brick_t *up, *upright, *right, *downright, *down, *downleft, *left, *upleft;

    actor_corners(act, sqrsize, diff, brick_list, &up, &upright, &right, &downright, &down, &downleft, &left, &upleft);

    return
        (upleft != NULL && upleft->brick_ref->property == BRK_OBSTACLE) ||
        (up != NULL && up->brick_ref->property == BRK_OBSTACLE) ||
        (upright != NULL && upright->brick_ref->property == BRK_OBSTACLE)
    ;
}

/* onleftwallcollision_t strategy */
eventstrategy_t* onleftwallcollision_new()
{
    onleftwallcollision_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onleftwallcollision_init;
    e->release = onleftwallcollision_release;
    e->should_trigger_event = onleftwallcollision_should_trigger_event;

    return e;
}

void onleftwallcollision_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onleftwallcollision_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onleftwallcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    const float sqrsize=1, diff=0;
    actor_t *act = object->actor;
    brick_t *up, *upright, *right, *downright, *down, *downleft, *left, *upleft;

    actor_corners(act, sqrsize, diff, brick_list, &up, &upright, &right, &downright, &down, &downleft, &left, &upleft);

    return
        (left != NULL && left->brick_ref->property == BRK_OBSTACLE) ||
        (upleft != NULL && upleft->brick_ref->property == BRK_OBSTACLE)
    ;
}

/* onrightwallcollision_t strategy */
eventstrategy_t* onrightwallcollision_new()
{
    onrightwallcollision_t *x = mallocx(sizeof *x);
    eventstrategy_t *e = (eventstrategy_t*)x;

    e->init = onrightwallcollision_init;
    e->release = onrightwallcollision_release;
    e->should_trigger_event = onrightwallcollision_should_trigger_event;

    return e;
}

void onrightwallcollision_init(eventstrategy_t *event)
{
    ; /* empty */
}

void onrightwallcollision_release(eventstrategy_t *event)
{
    ; /* empty */
}

int onrightwallcollision_should_trigger_event(eventstrategy_t *event, object_t *object, player_t** team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    const float sqrsize=1, diff=0;
    actor_t *act = object->actor;
    brick_t *up, *upright, *right, *downright, *down, *downleft, *left, *upleft;

    actor_corners(act, sqrsize, diff, brick_list, &up, &upright, &right, &downright, &down, &downleft, &left, &upleft);

    return
        (right != NULL && right->brick_ref->property == BRK_OBSTACLE) ||
        (upright != NULL && upright->brick_ref->property == BRK_OBSTACLE)
    ;
}

