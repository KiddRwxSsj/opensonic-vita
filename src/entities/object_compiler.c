/*
 * object_compiler.c - compiles object scripts
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

#include <ctype.h>
#include "object_compiler.h"
#include "../core/util.h"
#include "../core/stringutil.h"

#include "object_decorators/look.h"
#include "object_decorators/lock_camera.h"
#include "object_decorators/gravity.h"
#include "object_decorators/jump.h"
#include "object_decorators/walk.h"
#include "object_decorators/elliptical_trajectory.h"
#include "object_decorators/bounce_player.h"
#include "object_decorators/bullet_trajectory.h"
#include "object_decorators/mosquito_movement.h"
#include "object_decorators/on_event.h"
#include "object_decorators/set_animation.h"
#include "object_decorators/set_obstacle.h"
#include "object_decorators/set_alpha.h"
#include "object_decorators/enemy.h"
#include "object_decorators/move_player.h"
#include "object_decorators/player_movement.h"
#include "object_decorators/player_action.h"
#include "object_decorators/set_player_animation.h"
#include "object_decorators/set_player_speed.h"
#include "object_decorators/set_player_position.h"
#include "object_decorators/hit_player.h"
#include "object_decorators/children.h"
#include "object_decorators/create_item.h"
#include "object_decorators/change_closest_object_state.h"
#include "object_decorators/destroy.h"
#include "object_decorators/dialog_box.h"
#include "object_decorators/audio.h"
#include "object_decorators/clear_level.h"
#include "object_decorators/add_to_score.h"
#include "object_decorators/add_rings.h"
#include "object_decorators/observe_player.h"
#include "object_decorators/attach_to_player.h"

/* private stuff ;) */
#define DEFAULT_STATE                   "main"
#define STACKMAX                        1024
static void compile_command(objectmachine_t** machine_ref, const char *command, int n, const char *param[]);
static int traverse_object(const parsetree_statement_t *stmt, void *object);
static int traverse_object_state(const parsetree_statement_t *stmt, void *machine);
static int push_object_state(const parsetree_statement_t *stmt, void *machine);
static struct { const parsetree_statement_t *stmt; void *machine; } stack[STACKMAX];
static int stacksize;

/* -------------------------------------- */

/*
   available actions:
   -----------------------------------------------
   they all receive:
   1. m         : reference to an object machine (used to add a decorator to the machine)
   2. n         : the length of the array containing the parameters
   3. p[0..n-1] : the array containing the parameters
*/

/* basic actions */
static void set_animation(objectmachine_t** m, int n, const char **p);
static void set_obstacle(objectmachine_t** m, int n, const char **p);
static void set_alpha(objectmachine_t** m, int n, const char **p);
static void hide(objectmachine_t** m, int n, const char **p);
static void show(objectmachine_t** m, int n, const char **p);
static void enemy(objectmachine_t** m, int n, const char **p);

/* player interaction */
static void lock_camera(objectmachine_t** m, int n, const char **p);
static void move_player(objectmachine_t** m, int n, const char **p);
static void hit_player(objectmachine_t** m, int n, const char **p);
static void burn_player(objectmachine_t** m, int n, const char **p);
static void shock_player(objectmachine_t** m, int n, const char **p);
static void acid_player(objectmachine_t** m, int n, const char **p);
static void add_rings(objectmachine_t** m, int n, const char **p);
static void add_to_score(objectmachine_t** m, int n, const char **p);
static void set_player_animation(objectmachine_t** m, int n, const char **p);
static void enable_player_movement(objectmachine_t** m, int n, const char **p);
static void disable_player_movement(objectmachine_t** m, int n, const char **p);
static void set_player_xspeed(objectmachine_t** m, int n, const char **p);
static void set_player_yspeed(objectmachine_t** m, int n, const char **p);
static void set_player_position(objectmachine_t** m, int n, const char **p);
static void bounce_player(objectmachine_t** m, int n, const char **p);
static void observe_player(objectmachine_t** m, int n, const char **p);
static void observe_current_player(objectmachine_t** m, int n, const char **p);
static void observe_active_player(objectmachine_t** m, int n, const char **p);
static void observe_all_players(objectmachine_t** m, int n, const char **p);
static void attach_to_player(objectmachine_t** m, int n, const char **p);
static void springfy_player(objectmachine_t** m, int n, const char **p);
static void roll_player(objectmachine_t** m, int n, const char **p);

/* movement */
static void walk(objectmachine_t** m, int n, const char **p);
static void gravity(objectmachine_t** m, int n, const char **p);
static void jump(objectmachine_t** m, int n, const char **p);
static void bullet_trajectory(objectmachine_t** m, int n, const char **p);
static void elliptical_trajectory(objectmachine_t** m, int n, const char **p);
static void mosquito_movement(objectmachine_t** m, int n, const char **p);
static void look_left(objectmachine_t** m, int n, const char **p);
static void look_right(objectmachine_t** m, int n, const char **p);
static void look_at_player(objectmachine_t** m, int n, const char **p);
static void look_at_walking_direction(objectmachine_t** m, int n, const char **p);

/* object management */
static void create_item(objectmachine_t** m, int n, const char **p);
static void change_closest_object_state(objectmachine_t** m, int n, const char **p);
static void create_child(objectmachine_t** m, int n, const char **p);
static void change_child_state(objectmachine_t** m, int n, const char **p);
static void change_parent_state(objectmachine_t** m, int n, const char **p);
static void destroy(objectmachine_t** m, int n, const char **p);

/* events */
static void change_state(objectmachine_t** m, int n, const char **p);
static void on_timeout(objectmachine_t** m, int n, const char **p);
static void on_collision(objectmachine_t** m, int n, const char **p);
static void on_animation_finished(objectmachine_t** m, int n, const char **p);
static void on_random_event(objectmachine_t** m, int n, const char **p);
static void on_player_collision(objectmachine_t** m, int n, const char **p);
static void on_player_attack(objectmachine_t** m, int n, const char **p);
static void on_player_rect_collision(objectmachine_t** m, int n, const char **p);
static void on_no_shield(objectmachine_t** m, int n, const char **p);
static void on_shield(objectmachine_t** m, int n, const char **p);
static void on_fire_shield(objectmachine_t** m, int n, const char **p);
static void on_thunder_shield(objectmachine_t** m, int n, const char **p);
static void on_water_shield(objectmachine_t** m, int n, const char **p);
static void on_acid_shield(objectmachine_t** m, int n, const char **p);
static void on_wind_shield(objectmachine_t** m, int n, const char **p);
static void on_brick_collision(objectmachine_t** m, int n, const char **p);
static void on_floor_collision(objectmachine_t** m, int n, const char **p);
static void on_ceiling_collision(objectmachine_t** m, int n, const char **p);
static void on_left_wall_collision(objectmachine_t** m, int n, const char **p);
static void on_right_wall_collision(objectmachine_t** m, int n, const char **p);

/* level */
static void show_dialog_box(objectmachine_t** m, int n, const char **p);
static void hide_dialog_box(objectmachine_t** m, int n, const char **p);
static void clear_level(objectmachine_t** m, int n, const char **p);

/* audio commands */
static void audio_play_sample(objectmachine_t** m, int n, const char **p);
static void audio_play_music(objectmachine_t** m, int n, const char **p);
static void audio_play_level_music(objectmachine_t** m, int n, const char **p);
static void audio_set_music_volume(objectmachine_t** m, int n, const char **p);

/* -------------------------------------- */

/* command table */
typedef struct { const char *command; void (*action)(objectmachine_t**,int,const char**); } entry_t;
static entry_t command_table[] = {
    /* basic actions */
    { "set_animation", set_animation },
    { "set_obstacle", set_obstacle },
    { "set_alpha", set_alpha },
    { "hide", hide },
    { "show", show },
    { "enemy", enemy },

    /* player interaction */
    { "lock_camera", lock_camera },
    { "move_player", move_player },
    { "hit_player", hit_player },
    { "burn_player", burn_player },
    { "shock_player", shock_player },
    { "acid_player", acid_player },
    { "add_rings", add_rings },
    { "add_to_score", add_to_score },
    { "set_player_animation", set_player_animation },
    { "enable_player_movement", enable_player_movement },
    { "disable_player_movement", disable_player_movement },
    { "set_player_xspeed", set_player_xspeed },
    { "set_player_yspeed", set_player_yspeed },
    { "set_player_position", set_player_position },
    { "bounce_player", bounce_player },
    { "observe_player", observe_player },
    { "observe_current_player", observe_current_player },
    { "observe_active_player", observe_active_player },
    { "observe_all_players", observe_all_players },
    { "observe_next_player", observe_all_players },
    { "attach_to_player", attach_to_player },
    { "springfy_player", springfy_player },
    { "roll_player", roll_player },

    /* movement */
    { "walk", walk },
    { "gravity", gravity },
    { "jump", jump },
    { "move", bullet_trajectory },
    { "bullet_trajectory", bullet_trajectory },
    { "elliptical_trajectory", elliptical_trajectory },
    { "mosquito_movement", mosquito_movement },
    { "look_left", look_left },
    { "look_right", look_right },
    { "look_at_player", look_at_player },
    { "look_at_walking_direction", look_at_walking_direction },

    /* object management */
    { "create_item", create_item },
    { "change_closest_object_state", change_closest_object_state },
    { "create_child", create_child },
    { "change_child_state", change_child_state },
    { "change_parent_state", change_parent_state },
    { "destroy", destroy },

    /* events */
    { "change_state", change_state },
    { "on_timeout", on_timeout },
    { "on_collision", on_collision },
    { "on_animation_finished", on_animation_finished },
    { "on_random_event", on_random_event },
    { "on_player_collision", on_player_collision },
    { "on_player_attack", on_player_attack },
    { "on_player_rect_collision", on_player_rect_collision },
    { "on_no_shield", on_no_shield },
    { "on_shield", on_shield },
    { "on_fire_shield", on_fire_shield },
    { "on_thunder_shield", on_thunder_shield },
    { "on_water_shield", on_water_shield },
    { "on_acid_shield", on_acid_shield },
    { "on_wind_shield", on_wind_shield },
    { "on_brick_collision", on_brick_collision },
    { "on_floor_collision", on_floor_collision },
    { "on_ceiling_collision", on_ceiling_collision },
    { "on_left_wall_collision", on_left_wall_collision },
    { "on_right_wall_collision", on_right_wall_collision },

    /* level */
    { "show_dialog_box", show_dialog_box },
    { "hide_dialog_box", hide_dialog_box },
    { "clear_level", clear_level },

    /* audio commands */
    { "play_sample", audio_play_sample },
    { "play_music", audio_play_music },
    { "play_level_music", audio_play_level_music },
    { "set_music_volume", audio_set_music_volume },

    /* end of table */
    { NULL, NULL }
};




/* -------------------------------------- */

/* public methods */

/*
 * objectcompiler_compile()
 * Compiles the given script
 */
void objectcompiler_compile(object_t *obj, const parsetree_program_t *script)
{
    nanoparser_traverse_program_ex(script, (void*)obj, traverse_object);
    objectvm_set_current_state(obj->vm, DEFAULT_STATE);
}




/* -------------------------------------- */

/* private methods */
int traverse_object(const parsetree_statement_t* stmt, void *object)
{
    object_t *e = (object_t*)object;
    const char *id = nanoparser_get_identifier(stmt);
    const parsetree_parameter_t *param_list = nanoparser_get_parameter_list(stmt);
    objectmachine_t **machine_ref;

    if(str_icmp(id, "state") == 0) {
        const parsetree_parameter_t *p1, *p2;
        const char *state_name;
        const parsetree_program_t *state_code;

        p1 = nanoparser_get_nth_parameter(param_list, 1);
        p2 = nanoparser_get_nth_parameter(param_list, 2);

        nanoparser_expect_string(p1, "Object script error: state name is expected");
        nanoparser_expect_program(p2, "Object script error: state code is expected");

        state_name = nanoparser_get_string(p1);
        state_code = nanoparser_get_program(p2);

        objectvm_create_state(e->vm, state_name);
        objectvm_set_current_state(e->vm, state_name);
        machine_ref = objectvm_get_reference_to_current_state(e->vm);

        stacksize = 0;
        nanoparser_traverse_program_ex(state_code, (void*)machine_ref, push_object_state);
        while(stacksize-- > 0) /* traverse in reverse order - note the order of the decorators */
            traverse_object_state(stack[stacksize].stmt, stack[stacksize].machine);

        (*machine_ref)->init(*machine_ref);
    }
    else if(str_icmp(id, "requires") == 0) {
        if(nanoparser_get_number_of_parameters(param_list) == 1) {
            const parsetree_parameter_t *p1;
            int i, requires[3];

            p1 = nanoparser_get_nth_parameter(param_list, 1);
            nanoparser_expect_string(p1, "Object script error: requires is expected");

            sscanf(nanoparser_get_string(p1), "%d.%d.%d", &requires[0], &requires[1], &requires[2]);
            for(i=0; i<3; i++) requires[i] = clip(requires[i], 0, 99);
            if(game_version_compare(requires[0], requires[1], requires[2]) < 0) {
                fatal_error(
                    "This object script requires version %d.%d.%d or greater of the game engine.\nPlease check our for new versions at %s",
                    requires[0], requires[1], requires[2], GAME_WEBSITE
                );
            }
        }
        else
            fatal_error("Object script error: command 'requires' expects one parameter - minimum required engine version");

    }
    else if(str_icmp(id, "destroy_if_far_from_play_area") == 0) {
        if(nanoparser_get_number_of_parameters(param_list) == 0)
            e->preserve = FALSE;
        else
            fatal_error("Object script error: command 'destroy_if_far_from_play_area' expects no parameters");
    }
    else if(str_icmp(id, "always_active") == 0) {
        if(nanoparser_get_number_of_parameters(param_list) == 0)
            e->always_active = TRUE;
        else
            fatal_error("Object script error: command 'always_active' expects no parameters");
    }
    else if(str_icmp(id, "hide_unless_in_editor_mode") == 0) {
        if(nanoparser_get_number_of_parameters(param_list) == 0)
            e->hide_unless_in_editor_mode = TRUE;
        else
            fatal_error("Object script error: command 'hide_unless_in_editor_mode' expects no parameters");
    }
    else
        fatal_error("Object script error: unknown keyword '%s'", id);

    return 0;
}

int traverse_object_state(const parsetree_statement_t* stmt, void *machine)
{
    objectmachine_t **ref = (objectmachine_t**)machine; /* reference to the current object machine */
    const char *id = nanoparser_get_identifier(stmt); /* command string */
    const parsetree_parameter_t *param_list = nanoparser_get_parameter_list(stmt);
    const char **p_k;
    int i, n;

    /* creates the parameter list: p_k[0..n-1] */
    n = nanoparser_get_number_of_parameters(param_list);
    p_k = mallocx(n * (sizeof *p_k));
    for(i=0; i<n; i++) {
        const parsetree_parameter_t *p = nanoparser_get_nth_parameter(param_list, 1+i);
        nanoparser_expect_string(p, "Object script error: command parameters must be strings");
        p_k[i] = nanoparser_get_string(p);
    }

    /* adds the corresponding decorator to the machine */
    compile_command(ref, id, n, p_k);

    /* releases the parameter list */
    free(p_k);

    /* done! :-) */
    return 0;
}

int push_object_state(const parsetree_statement_t* stmt, void *machine)
{
    if(stacksize < STACKMAX) {
        stack[stacksize].stmt = stmt;
        stack[stacksize].machine = machine;
        stacksize++;
    }
    else
        fatal_error("Object script error: you may write %d commands or less per state", STACKMAX);

    return 0;
}

void compile_command(objectmachine_t** machine_ref, const char *command, int n, const char *param[])
{
    int i = 0;
    entry_t e = command_table[i++];

    /* finds the corresponding command in the table */
    while(e.command != NULL && e.action != NULL) {
        if(str_icmp(e.command, command) == 0) {
            (e.action)(machine_ref, n, (const char**)param);
            return;
        }

        e = command_table[i++];
    }

    fatal_error("Object script error - unknown command: '%s'", command);
}


/* -------------------------------------- */

/* action programming */
void set_animation(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_setanimation_new(*m, p[0], atoi(p[1]));
    else
        fatal_error("Object script error - set_animation expects two parameters: sprite_name, animation_id");
}

void set_obstacle(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_setobstacle_new(*m, atob(p[0]), 0);
    else if(n == 2)
        *m = objectdecorator_setobstacle_new(*m, atob(p[0]), atoi(p[1]));
    else
        fatal_error("Object script error - set_obstacle expects at least one and at most two parameters: is_obstacle (TRUE or FALSE) [, angle]");
}

void set_alpha(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_setalpha_new(*m, atof(p[0]));
    else
        fatal_error("Object script error - set_alpha expects one parameter: alpha (0.0 (transparent) <= alpha <= 1.0 (opaque))");
}

void hide(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_setalpha_new(*m, 0.0f);
    else
        fatal_error("Object script error - hide expects no parameters");
}

void show(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_setalpha_new(*m, 1.0f);
    else
        fatal_error("Object script error - show expects no parameters");
}

void bullet_trajectory(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_bullettrajectory_new(*m, atof(p[0]), atof(p[1]));
    else
        fatal_error("Object script error - bullet_trajectory expects two parameters: speed_x, speed_y");
}

void create_item(objectmachine_t** m, int n, const char **p)
{
    if(n == 3)
        *m = objectdecorator_createitem_new(*m, atoi(p[0]), atof(p[1]), atof(p[2]));
    else
        fatal_error("Object script error - create_item expects three parameters: item_id, offset_x, offset_y");
}

void create_child(objectmachine_t** m, int n, const char **p)
{
    if(n == 3)
        *m = objectdecorator_createchild_new(*m, p[0], atof(p[1]), atof(p[2]), "\201"); /* dummy child name */
    else if(n == 4)
        *m = objectdecorator_createchild_new(*m, p[0], atof(p[1]), atof(p[2]), p[3]);
    else
        fatal_error("Object script error - create_child expects three or four parameters: object_name, offset_x, offset_y [, child_name]");
}

void change_child_state(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_changechildstate_new(*m, p[0], p[1]);
    else
        fatal_error("Object script error - change_child_state expects two parameters: child_name, new_state_name");
}

void change_parent_state(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_changeparentstate_new(*m, p[0]);
    else
        fatal_error("Object script error - change_parent_state expects one parameter: new_state_name");
}

void destroy(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_destroy_new(*m);
    else
        fatal_error("Object script error - destroy expects no parameters");
}

void elliptical_trajectory(objectmachine_t** m, int n, const char **p)
{
    if(n >= 4 && n <= 6)
        *m = objectdecorator_ellipticaltrajectory_new(*m, atof(p[0]), atof(p[1]), atof(p[2]), atof(p[3]), atof(p[4]), atof(p[5]));
    else
        fatal_error("Object script error - elliptical_trajectory expects at least four and at most six parameters: amplitude_x, amplitude_y, angularspeed_x, angularspeed_y [, initialphase_x [, initialphase_y]]");
}

void gravity(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_gravity_new(*m);
    else
        fatal_error("Object script error - gravity expects no parameters");
}

void look_left(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_lookleft_new(*m);
    else
        fatal_error("Object script error - look_left expects no parameters");
}

void look_right(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_lookright_new(*m);
    else
        fatal_error("Object script error - look_right expects no parameters");
}

void look_at_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_lookatplayer_new(*m);
    else
        fatal_error("Object script error - look_at_player expects no parameters");
}

void look_at_walking_direction(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_lookatwalkingdirection_new(*m);
    else
        fatal_error("Object script error - look_at_walking_direction expects no parameters");
}

void mosquito_movement(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_mosquitomovement_new(*m, atof(p[0]));
    else
        fatal_error("Object script error - mosquito_movement expects one parameter: speed");
}

void move_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_moveplayer_new(*m, atof(p[0]), atof(p[1]));
    else
        fatal_error("Object script error - move_player expects two parameters: speed_x, speed_y");
}

void hit_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_hitplayer_new(*m);
    else
        fatal_error("Object script error - hit_player expects no parameters");
}

void enemy(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_enemy_new(*m, atoi(p[0]));
    else
        fatal_error("Object script error - enemy expects one parameter: score");
}

void walk(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_walk_new(*m, atof(p[0]));
    else
        fatal_error("Object script error - walk expects one parameter: speed");
}

void change_state(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_ontimeout_new(*m, 0.0f, p[0]);
    else
        fatal_error("Object script error - change_state expects one parameter: new_state_name");
}

void on_timeout(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_ontimeout_new(*m, atof(p[0]), p[1]);
    else
        fatal_error("Object script error - on_timeout expects two parameters: timeout (in seconds), new_state_name");
}

void on_collision(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_oncollision_new(*m, p[0], p[1]);
    else
        fatal_error("Object script error - on_collision expects two parameters: object_name, new_state_name");
}

void on_animation_finished(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onanimationfinished_new(*m, p[0]);
    else
        fatal_error("Object script error - on_animation_finished expects one parameter: new_state_name");
}

void on_random_event(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_onrandomevent_new(*m, atof(p[0]), p[1]);
    else
        fatal_error("Object script error - on_random_event expects two parameters: probability (0.0 <= probability <= 1.0), new_state_name");
}

void on_player_collision(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onplayercollision_new(*m, p[0]);
    else
        fatal_error("Object script error - on_player_collision expects one parameter: new_state_name");
}

void on_player_attack(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onplayerattack_new(*m, p[0]);
    else
        fatal_error("Object script error - on_player_attack expects one parameter: new_state_name");
}

void on_player_rect_collision(objectmachine_t** m, int n, const char **p)
{
    if(n == 5)
        *m = objectdecorator_onplayerrectcollision_new(*m, atoi(p[0]), atoi(p[1]), atoi(p[2]), atoi(p[3]), p[4]);
    else
        fatal_error("Object script error - on_player_rect_collision expects five parameters: offset_x1, offset_y1, offset_x2, offset_y2, new_state_name");
}

void on_no_shield(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onnoshield_new(*m, p[0]);
    else
        fatal_error("Object script error - on_no_shield expects one parameter: new_state_name");
}

void on_shield(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onshield_new(*m, p[0]);
    else
        fatal_error("Object script error - on_shield expects one parameter: new_state_name");
}

void on_fire_shield(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onfireshield_new(*m, p[0]);
    else
        fatal_error("Object script error - on_fire_shield expects one parameter: new_state_name");
}

void on_thunder_shield(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onthundershield_new(*m, p[0]);
    else
        fatal_error("Object script error - on_thunder_shield expects one parameter: new_state_name");
}

void on_water_shield(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onwatershield_new(*m, p[0]);
    else
        fatal_error("Object script error - on_water_shield expects one parameter: new_state_name");
}

void on_acid_shield(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onacidshield_new(*m, p[0]);
    else
        fatal_error("Object script error - on_acid_shield expects one parameter: new_state_name");
}

void on_wind_shield(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onwindshield_new(*m, p[0]);
    else
        fatal_error("Object script error - on_wind_shield expects one parameter: new_state_name");
}

void on_brick_collision(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onbrickcollision_new(*m, p[0]);
    else
        fatal_error("Object script error - on_brick_collision expects one parameter: new_state_name");
}

void on_floor_collision(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onfloorcollision_new(*m, p[0]);
    else
        fatal_error("Object script error - on_floor_collision expects one parameter: new_state_name");
}

void on_ceiling_collision(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onceilingcollision_new(*m, p[0]);
    else
        fatal_error("Object script error - on_ceiling_collision expects one parameter: new_state_name");
}

void on_left_wall_collision(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onleftwallcollision_new(*m, p[0]);
    else
        fatal_error("Object script error - on_left_wall_collision expects one parameter: new_state_name");
}

void on_right_wall_collision(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_onrightwallcollision_new(*m, p[0]);
    else
        fatal_error("Object script error - on_right_wall_collision expects one parameter: new_state_name");
}

void change_closest_object_state(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_changeclosestobjectstate_new(*m, p[0], p[1]);
    else
        fatal_error("Object script error - change_closest_object_state expects two parameters: object_name, new_state_name");
}

void burn_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_burnplayer_new(*m);
    else
        fatal_error("Object script error - burn_player expects no parameters");
}

void shock_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_shockplayer_new(*m);
    else
        fatal_error("Object script error - shock_player expects no parameters");
}

void acid_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_acidplayer_new(*m);
    else
        fatal_error("Object script error - acid_player expects no parameters");
}

void add_rings(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_addrings_new(*m, atoi(p[0]));
    else
        fatal_error("Object script error - add_rings expects one parameter: number_of_rings");
}

void add_to_score(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_addtoscore_new(*m, atoi(p[0]));
    else
        fatal_error("Object script error - add_to_score expects one parameter: score");
}

void audio_play_sample(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_playsample_new(*m, p[0], 1.0f, 0.0f, 1.0f, 0);
    else if(n == 2)
        *m = objectdecorator_playsample_new(*m, p[0], atof(p[1]), 0.0f, 1.0f, 0);
    else if(n == 3)
        *m = objectdecorator_playsample_new(*m, p[0], atof(p[1]), atof(p[2]), 1.0f, 0);
    else if(n == 4)
        *m = objectdecorator_playsample_new(*m, p[0], atof(p[1]), atof(p[2]), atof(p[3]), 0);
    else if(n == 5)
        *m = objectdecorator_playsample_new(*m, p[0], atof(p[1]), atof(p[2]), atof(p[3]), atoi(p[4]));
    else
        fatal_error("Object script error - play_sample expects at least one and at most five parameters: sound_name [, volume [, pan [, frequency [, loops]]]]");
}

void audio_play_music(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_playmusic_new(*m, p[0], 0);
    else if(n == 2)
        *m = objectdecorator_playmusic_new(*m, p[0], atoi(p[1]));
    else
        fatal_error("Object script error - play_music expects at least one and at most two parameters: music_name [, loops]");
}

void audio_play_level_music(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_playlevelmusic_new(*m);
    else
        fatal_error("Object script error - play_level_music expects no parameters");
}

void audio_set_music_volume(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_setmusicvolume_new(*m, atof(p[0]));
    else
        fatal_error("Object script error - set_music_volume expects one parameter: volume");
}

void show_dialog_box(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_showdialogbox_new(*m, p[0], p[1]);
    else
        fatal_error("Object script error - show_dialog_box expects two parameters: title, message");
}

void hide_dialog_box(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_hidedialogbox_new(*m);
    else
        fatal_error("Object script error - hide_dialog_box expects no parameters");
}

void clear_level(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_clearlevel_new(*m);
    else
        fatal_error("Object script error - clear_level expects no parameters");
}

void jump(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_jump_new(*m, atof(p[0]));
    else
        fatal_error("Object script error - jump expects one parameter: jump_strength");
}

void set_player_animation(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_setplayeranimation_new(*m, p[0], atoi(p[1]));
    else
        fatal_error("Object script error - set_player_animation expects two parameters: sprite_name, animation_id");
}

void enable_player_movement(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_enableplayermovement_new(*m);
    else
        fatal_error("Object script error - enable_player_movement expects no parameters");
}

void disable_player_movement(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_disableplayermovement_new(*m);
    else
        fatal_error("Object script error - disable_player_movement expects no parameters");
}

void set_player_xspeed(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_setplayerxspeed_new(*m, atof(p[0]));
    else
        fatal_error("Object script error - set_player_xspeed expects one parameter: speed");
}

void set_player_yspeed(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_setplayeryspeed_new(*m, atof(p[0]));
    else
        fatal_error("Object script error - set_player_yspeed expects one parameter: speed");
}

void set_player_position(objectmachine_t** m, int n, const char **p)
{
    if(n == 2)
        *m = objectdecorator_setplayerposition_new(*m, atoi(p[0]), atoi(p[1]));
    else
        fatal_error("Object script error - set_player_position expects two parameters: xpos, ypos");
}

void bounce_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_bounceplayer_new(*m);
    else
        fatal_error("Object script error - bounce_player expects no parameters");
}

void lock_camera(objectmachine_t** m, int n, const char **p)
{
    if(n == 4)
        *m = objectdecorator_lockcamera_new(*m, atoi(p[0]), atoi(p[1]), atoi(p[2]), atoi(p[3]));
    else
        fatal_error("Object script error - lock_camera expects four parameters: x1, y1, x2, y2");
}

void observe_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 1)
        *m = objectdecorator_observeplayer_new(*m, p[0]);
    else
        fatal_error("Object script error - observe_player expects one parameter: player_name");
}

void observe_current_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_observecurrentplayer_new(*m);
    else
        fatal_error("Object script error - observe_current_player expects no parameters");
}

void observe_active_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_observeactiveplayer_new(*m);
    else
        fatal_error("Object script error - observe_active_player expects no parameters");
}

void observe_all_players(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_observeallplayers_new(*m);
    else
        fatal_error("Object script error - observe_all_players expects no parameters");
}

void attach_to_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_attachtoplayer_new(*m, 0, 0);
    else if(n == 1)
        *m = objectdecorator_attachtoplayer_new(*m, atoi(p[0]), 0);
    else if(n == 2)
        *m = objectdecorator_attachtoplayer_new(*m, atoi(p[0]), atoi(p[1]));
    else
        fatal_error("Object script error - attach_to_player expects at most two parameters: [offset_x [, offset_y]]");
}

void springfy_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_springfyplayer_new(*m);
    else
        fatal_error("Object script error - springfy_player expects no parameters");
}

void roll_player(objectmachine_t** m, int n, const char **p)
{
    if(n == 0)
        *m = objectdecorator_rollplayer_new(*m);
    else
        fatal_error("Object script error - roll_player expects no parameters");
}
