/*
 * children.c - This decorator makes the object create/manipulate other objects
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

#include "children.h"
#include "../object_vm.h"
#include "../../core/util.h"
#include "../../core/stringutil.h"
#include "../../scenes/level.h"

/* objectdecorator_children_t class */
typedef struct objectdecorator_children_t objectdecorator_children_t;
struct objectdecorator_children_t {
    objectdecorator_t base; /* inherits from objectdecorator_t */
    char *object_name; /* I'll create an object called object_name... */
    v2d_t offset; /* ...at this offset */
    char *child_name; /* child name */
    char *new_state_name; /* new state name */
    void (*strategy)(objectdecorator_children_t*); /* strategy pattern */
};

/* private strategies */
static void createchild_strategy(objectdecorator_children_t *me);
static void changechildstate_strategy(objectdecorator_children_t *me);
static void changeparentstate_strategy(objectdecorator_children_t *me);

/* private methods */
static void init(objectmachine_t *obj);
static void release(objectmachine_t *obj);
static void update(objectmachine_t *obj, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list);
static void render(objectmachine_t *obj, v2d_t camera_position);



/* public methods */
objectmachine_t* objectdecorator_createchild_new(objectmachine_t *decorated_machine, const char *object_name, float offset_x, float offset_y, const char *child_name)
{
    objectdecorator_children_t *me = mallocx(sizeof *me);
    objectdecorator_t *dec = (objectdecorator_t*)me;
    objectmachine_t *obj = (objectmachine_t*)dec;

    obj->init = init;
    obj->release = release;
    obj->update = update;
    obj->render = render;
    obj->get_object_instance = objectdecorator_get_object_instance; /* inherits from superclass */
    dec->decorated_machine = decorated_machine;

    me->strategy = createchild_strategy;
    me->offset = v2d_new(offset_x, offset_y);
    me->object_name = str_dup(object_name);
    me->child_name = str_dup(child_name);
    me->new_state_name = NULL;

    return obj;
}

objectmachine_t* objectdecorator_changechildstate_new(objectmachine_t *decorated_machine, const char *child_name, const char *new_state_name)
{
    objectdecorator_children_t *me = mallocx(sizeof *me);
    objectdecorator_t *dec = (objectdecorator_t*)me;
    objectmachine_t *obj = (objectmachine_t*)dec;

    obj->init = init;
    obj->release = release;
    obj->update = update;
    obj->render = render;
    obj->get_object_instance = objectdecorator_get_object_instance; /* inherits from superclass */
    dec->decorated_machine = decorated_machine;

    me->strategy = changechildstate_strategy;
    me->offset = v2d_new(0, 0);
    me->object_name = NULL;
    me->child_name = str_dup(child_name);
    me->new_state_name = str_dup(new_state_name);

    return obj;
}

objectmachine_t* objectdecorator_changeparentstate_new(objectmachine_t *decorated_machine, const char *new_state_name)
{
    objectdecorator_children_t *me = mallocx(sizeof *me);
    objectdecorator_t *dec = (objectdecorator_t*)me;
    objectmachine_t *obj = (objectmachine_t*)dec;

    obj->init = init;
    obj->release = release;
    obj->update = update;
    obj->render = render;
    obj->get_object_instance = objectdecorator_get_object_instance; /* inherits from superclass */
    dec->decorated_machine = decorated_machine;

    me->strategy = changeparentstate_strategy;
    me->offset = v2d_new(0, 0);
    me->object_name = NULL;
    me->child_name = NULL;
    me->new_state_name = str_dup(new_state_name);

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
    objectdecorator_children_t *me = (objectdecorator_children_t*)obj;
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    if(me->child_name != NULL)
        free(me->child_name);

    if(me->object_name != NULL)
        free(me->object_name);

    if(me->new_state_name != NULL)
        free(me->new_state_name);

    decorated_machine->release(decorated_machine);
    free(obj);
}

void update(objectmachine_t *obj, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, object_list_t *object_list)
{
    objectdecorator_children_t *me = (objectdecorator_children_t*)obj;
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    me->strategy(me);

    decorated_machine->update(decorated_machine, team, team_size, brick_list, item_list, object_list);
}

void render(objectmachine_t *obj, v2d_t camera_position)
{
    objectdecorator_t *dec = (objectdecorator_t*)obj;
    objectmachine_t *decorated_machine = dec->decorated_machine;

    ; /* empty */

    decorated_machine->render(decorated_machine, camera_position);
}


/* private strategies */
void createchild_strategy(objectdecorator_children_t *me)
{
    objectmachine_t *obj = (objectmachine_t*)me;
    object_t *object = obj->get_object_instance(obj);
    object_t *child;

    child = level_create_enemy(me->object_name, v2d_add(object->actor->position, me->offset));
    if(child != NULL)
        enemy_add_child(object, me->child_name, child);
}

void changechildstate_strategy(objectdecorator_children_t *me)
{
    objectmachine_t *obj = (objectmachine_t*)me;
    object_t *object = obj->get_object_instance(obj);
    object_t *child;

    child = enemy_get_child(object, me->child_name);
    if(child != NULL)
        objectvm_set_current_state(child->vm, me->new_state_name);
}

void changeparentstate_strategy(objectdecorator_children_t *me)
{
    objectmachine_t *obj = (objectmachine_t*)me;
    object_t *object = obj->get_object_instance(obj);
    object_t *parent;

    parent = enemy_get_parent(object);
    if(parent != NULL)
        objectvm_set_current_state(parent->vm, me->new_state_name);
}

