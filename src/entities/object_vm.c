/*
 * object_vm.c - virtual machine of the objects
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

#include "object_vm.h"
#include "../core/util.h"
#include "../core/stringutil.h"
#include "object_decorators/base/objectmachine.h"
#include "object_decorators/base/objectbasicmachine.h"

/* private stuff */
typedef struct objectmachine_list_t objectmachine_list_t;

/* objectvm_t class */
struct objectvm_t
{
    enemy_t* owner;
    objectmachine_list_t* state_list;
    objectmachine_t** reference_to_current_state;
};

/* linked list of enemy machines */
struct objectmachine_list_t {
    char *name;
    objectmachine_t *data;
    objectmachine_list_t *next;
};

static objectmachine_list_t* objectmachine_list_new(objectmachine_list_t* list, const char *name, enemy_t* owner);
static objectmachine_list_t* objectmachine_list_delete(objectmachine_list_t* list);
static objectmachine_list_t* objectmachine_list_find(objectmachine_list_t* list, const char *name);



/* public methods */

objectvm_t* objectvm_create(enemy_t* owner)
{
    objectvm_t *vm = mallocx(sizeof *vm);
    vm->owner = owner;
    vm->state_list = NULL;
    vm->reference_to_current_state = NULL;
    return vm;
}

objectvm_t* objectvm_destroy(objectvm_t* vm)
{
    vm->state_list = objectmachine_list_delete(vm->state_list);
    vm->reference_to_current_state = NULL;
    vm->owner = NULL;
    free(vm);
    return NULL;
}

void objectvm_set_current_state(objectvm_t* vm, const char *name)
{
    objectmachine_list_t *m = objectmachine_list_find(vm->state_list, name);
    if(m != NULL)
        vm->reference_to_current_state = &(m->data);
    else
        fatal_error("Object script error: can't find state \"%s\".", name);
}

objectmachine_t** objectvm_get_reference_to_current_state(objectvm_t* vm)
{
    return vm->reference_to_current_state;
}

void objectvm_create_state(objectvm_t* vm, const char *name)
{
    if(objectmachine_list_find(vm->state_list, name) == NULL)
        vm->state_list = objectmachine_list_new(vm->state_list, name, vm->owner);
    else
        fatal_error("Object script error: can't redefine state \"%s\".", name);
}



/* private methods */

objectmachine_list_t* objectmachine_list_new(objectmachine_list_t* list, const char *name, enemy_t *owner)
{
    objectmachine_list_t *l = mallocx(sizeof *l);
    l->name = str_dup(name);
    l->data = objectbasicmachine_new(owner);
    l->next = list;
    return l;
}

objectmachine_list_t* objectmachine_list_delete(objectmachine_list_t* list)
{
    if(list != NULL) {
        objectmachine_t *machine = list->data;
        objectmachine_list_delete(list->next);
        free(list->name);
        machine->release(machine);
        free(list);
    }

    return NULL;
}

objectmachine_list_t* objectmachine_list_find(objectmachine_list_t* list, const char *name)
{
    if(list != NULL) {
        if(str_icmp(list->name, name) != 0)
            return objectmachine_list_find(list->next, name);
        else
            return list;
    }

    return NULL;
}

