/*
 * enemy.c - baddies
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

#include <string.h>
#include <math.h>
#include "../core/global.h"
#include "../core/util.h"
#include "../core/audio.h"
#include "../core/timer.h"
#include "../core/input.h"
#include "../core/logfile.h"
#include "../core/stringutil.h"
#include "../core/osspec.h"
#include "../core/nanoparser/nanoparser.h"
#include "../scenes/level.h"
#include "actor.h"
#include "player.h"
#include "enemy.h"
#include "object_vm.h"
#include "object_compiler.h"
#include "item.h"
#include "brick.h"

/* private stuff */
#define MAX_OBJECTS                     1024

typedef struct object_children_t object_children_t;
struct object_children_t {
    char *name;
    enemy_t *data;
    object_children_t *next;
};
static object_children_t* object_children_new();
static object_children_t* object_children_delete(object_children_t* list);
static object_children_t* object_children_add(object_children_t* list, const char *name, enemy_t *data);
static object_children_t* object_children_remove(object_children_t* list, enemy_t *data);
static object_t* object_children_find(object_children_t* list, const char *name);

typedef struct { const char *in_object_name; const parsetree_program_t *out_object_block; } in_out_t;
typedef struct { const char* name[MAX_OBJECTS]; int length; } object_name_data_t;
static int object_name_table_cmp(const void *a, const void *b);
static enemy_t* create_from_script(const char *object_name);
static int find_object_block(const parsetree_statement_t *stmt, void *in_out_param);
static int fill_object_data(const parsetree_statement_t *stmt, void *object_name_data);
static int dirfill(const char *filename, int attrib, void *param); /* file system callback */
static int is_hidden_object(const char *name);

static parsetree_program_t *objects;
static object_name_data_t name_table;


/* ------ public class methods ---------- */

/*
 * objects_init()
 * Initializes this module
 */
void objects_init()
{
    const char *dir = "objects"; /* no wildcard: scan_directory() (osspec.h) filters by extension below */
    const char *extension = ".obj";
    char abs_path[2][1024];
    int j, max_paths;

    logfile_message("Loading objects scripts...");
    objects = NULL;

    /* official and $HOME filepaths */
    absolute_filepath(abs_path[0], dir, sizeof(abs_path[0]));
    home_filepath(abs_path[1], dir, sizeof(abs_path[1]));
    max_paths = (strcmp(abs_path[0], abs_path[1]) == 0) ? 1 : 2;

    /* reading the parse tree */
    for(j=0; j<max_paths; j++)
        scan_directory(abs_path[j], extension, dirfill, (void*)(&objects));

    /* creating the name table */
    name_table.length = 0;
    nanoparser_traverse_program_ex(objects, (void*)(&name_table), fill_object_data);
    qsort(name_table.name, name_table.length, sizeof(name_table.name[0]), object_name_table_cmp);
}

/*
 * objects_release()
 * Releases this module
 */
void objects_release()
{
    objects = nanoparser_deconstruct_tree(objects);
}

/*
 * objects_get_list_of_names()
 * Returns an array v[0..n-1] of available object names
 */
const char** objects_get_list_of_names(int *n)
{
    *n = name_table.length;
    return name_table.name;
}








/* ------ public instance methods ------- */


/*
 * enemy_create()
 * Creates a new enemy
 */
enemy_t *enemy_create(const char *name)
{
    return create_from_script(name);
}


/*
 * enemy_destroy()
 * Destroys an enemy
 */
enemy_t *enemy_destroy(enemy_t *enemy)
{
    object_children_t *it;

    /* tell my children I died */
    for(it=enemy->children; it; it=it->next)
        it->data->parent = NULL;

    /* destroy my children list */
    object_children_delete(enemy->children);

    /* tell my parent I died */
    if(enemy->parent != NULL)
        enemy_remove_child(enemy->parent, enemy);

    /* destroy my virtual machine */
    objectvm_destroy(enemy->vm);

    /* destroy me */
    actor_destroy(enemy->actor);
    free(enemy->name);
    free(enemy);

    /* success */
    return NULL;
}


/*
 * enemy_update()
 * Runs every cycle of the game to update an enemy
 */
void enemy_update(enemy_t *enemy, player_t **team, int team_size, brick_list_t *brick_list, item_list_t *item_list, enemy_list_t *object_list)
{
    objectmachine_t *machine = *(objectvm_get_reference_to_current_state(enemy->vm));
    machine->update(machine, team, team_size, brick_list, item_list, object_list);
}



/*
 * enemy_render()
 * Renders an enemy
 */
void enemy_render(enemy_t *enemy, v2d_t camera_position)
{
    objectmachine_t *machine = *(objectvm_get_reference_to_current_state(enemy->vm));
    if(!enemy->hide_unless_in_editor_mode || (enemy->hide_unless_in_editor_mode && level_editmode()))
        machine->render(machine, camera_position);
}


/*
 * enemy_get_parent()
 * Finds the parent of this object
 */
enemy_t *enemy_get_parent(enemy_t *enemy)
{
    return enemy->parent;
}


/*
 * enemy_get_child()
 * Finds a child of this object
 */
enemy_t *enemy_get_child(enemy_t *enemy, const char *child_name)
{
    return object_children_find(enemy->children, child_name);
}


/*
 * enemy_add_child()
 * Adds a child to this object
 */
void enemy_add_child(enemy_t *enemy, const char *child_name, enemy_t *child)
{
    enemy->children = object_children_add(enemy->children, child_name, child);
    child->parent = enemy;
    child->created_from_editor = FALSE;
}

/*
 * enemy_remove_child()
 * Removes a child from this object (the child is not deleted, though)
 */
void enemy_remove_child(enemy_t *enemy, enemy_t *child)
{
    enemy->children = object_children_remove(enemy->children, child);
}


/*
 * enemy_get_observed_player()
 * returns the observed player
 */
player_t *enemy_get_observed_player(enemy_t *enemy)
{
    return enemy->observed_player != NULL ? enemy->observed_player : level_player();
}

/*
 * enemy_observe_player()
 * observes a new player
 */
void enemy_observe_player(enemy_t *enemy, player_t *player)
{
    enemy->observed_player = player;
}

/*
 * enemy_observe_current_player()
 * observes the current player
 */
void enemy_observe_current_player(enemy_t *enemy)
{
    enemy->observed_player = level_player();
}

/*
 * enemy_observe_active_player()
 * observes the active player
 */
void enemy_observe_active_player(enemy_t *enemy)
{
    enemy->observed_player = NULL;
}



/* ----------- private functions ----------- */

enemy_t* create_from_script(const char *object_name)
{
    enemy_t* e = mallocx(sizeof *e);
    in_out_t param;

    /* setup the object */
    e->name = str_dup(object_name);
    e->state = ES_IDLE;
    e->actor = actor_create();
    e->actor->input = input_create_computer();
    actor_change_animation(e->actor, sprite_get_animation("SD_QUESTIONMARK", 0));
    e->preserve = TRUE;
    e->obstacle = FALSE;
    e->obstacle_angle = 0;
    e->always_active = FALSE;
    e->hide_unless_in_editor_mode = FALSE;
    e->vm = objectvm_create(e);
    e->created_from_editor = TRUE;
    e->parent = NULL;
    e->children = object_children_new();
    e->observed_player = NULL;

    /* finding the code of the object */
    param.in_object_name = object_name;
    param.out_object_block = NULL;
    nanoparser_traverse_program_ex(objects, (void*)(&param), find_object_block);

    /* the code of the object is located in param.out_object_block. Let's compile it. */
    if(param.out_object_block != NULL)
        objectcompiler_compile(e, param.out_object_block);
    else
        fatal_error("Object '%s' does not exist", object_name);

    /* success! */
    return e;
}

int is_hidden_object(const char *name)
{
    return name[0] == '.';
}

int find_object_block(const parsetree_statement_t *stmt, void *in_out_param)
{
    in_out_t* param = (in_out_t*)in_out_param;
    const char *id = nanoparser_get_identifier(stmt);
    const parsetree_parameter_t *param_list = nanoparser_get_parameter_list(stmt);

    if(str_icmp(id, "object") == 0) {
        const parsetree_parameter_t *p1, *p2;
        const char *name;
        const parsetree_program_t *block;

        p1 = nanoparser_get_nth_parameter(param_list, 1);
        p2 = nanoparser_get_nth_parameter(param_list, 2);
        nanoparser_expect_string(p1, "Object script error: object name is expected");
        nanoparser_expect_program(p2, "Object script error: object block is expected");
        name = nanoparser_get_string(p1);
        block = nanoparser_get_program(p2);

        if(str_icmp(name, param->in_object_name) == 0)
            param->out_object_block = block;
    }
    else
        fatal_error("Object script error: unknown keyword '%s'", id);

    return 0;
}

int fill_object_data(const parsetree_statement_t* stmt, void *object_name_data)
{
    object_name_data_t *x = (object_name_data_t*)object_name_data;
    const char *id = nanoparser_get_identifier(stmt);
    const parsetree_parameter_t *param_list = nanoparser_get_parameter_list(stmt);

    if(str_icmp(id, "object") == 0) {
        const parsetree_parameter_t *p1 = nanoparser_get_nth_parameter(param_list, 1);
        nanoparser_expect_string(p1, "Object script error: object name is expected");
        if(x->length < MAX_OBJECTS) {
            const char *name = nanoparser_get_string(p1);
            if(!is_hidden_object(name))
                (x->name)[ (x->length)++ ] = name;
        }
        else
            fatal_error("Object script error: can't have more than %d objects", MAX_OBJECTS);
    }
    else
        fatal_error("Object script error: unknown keyword '%s'", id);

    return 0;
}

int dirfill(const char *filename, int attrib, void *param)
{
    parsetree_program_t** p = (parsetree_program_t**)param;
    *p = nanoparser_append_program(*p, nanoparser_construct_tree(filename));
    return 0;
}


int object_name_table_cmp(const void *a, const void *b)
{
    const char *i = *((const char**)a);
    const char *j = *((const char**)b);
    return str_icmp(i, j);
}

object_children_t* object_children_new()
{
    return NULL;
}

object_children_t* object_children_delete(object_children_t* list)
{
    if(list != NULL) {
        object_children_delete(list->next);
        free(list->name);
        free(list);
    }

    return NULL;
}

object_children_t* object_children_add(object_children_t* list, const char *name, enemy_t *data)
{
    object_children_t *x = mallocx(sizeof *x);
    x->name = str_dup(name);
    x->data = data;
    x->next = list;
    return x;
}

object_t* object_children_find(object_children_t* list, const char *name)
{
    object_children_t *it = list;

    while(it != NULL) {
        if(strcmp(it->name, name) == 0)
            return it->data;
        else
            it = it->next;
    }

    return NULL;
}

object_children_t* object_children_remove(object_children_t* list, enemy_t *data)
{
    object_children_t *it, *next;

    if(list != NULL) {
        if(list->data == data) {
            next = list->next;
            free(list->name);
            free(list);
            return next;
        }
        else {
            it = list;
            while(it->next != NULL && it->next->data != data)
                it = it->next;
            if(it->next != NULL) {
                next = it->next->next;
                free(it->next->name);
                free(it->next);
                it->next = next;
            }
            return list;
        }
    }
    else
        return NULL;
}
