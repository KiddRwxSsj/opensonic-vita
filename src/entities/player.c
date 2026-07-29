/*
 * player.c - player module
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

#include <math.h>
#include "actor.h"
#include "player.h"
#include "brick.h"
#include "enemy.h"
#include "item.h"
#include "items/ring.h"
#include "items/falglasses.h"
#include "../core/global.h"
#include "../core/audio.h"
#include "../core/util.h"
#include "../core/stringutil.h"
#include "../core/timer.h"
#include "../core/logfile.h"
#include "../core/input.h"
#include "../core/sprite.h"
#include "../core/soundfactory.h"
#include "../scenes/level.h"


/* Uncomment to show the collision detectors */
/* #define SHOW_COLLISION_DETECTORS */


/* private data */
#define NATURAL_ANGLE       0
#define LOCKACCEL_NONE      0
#define LOCKACCEL_LEFT      1
#define LOCKACCEL_RIGHT     2
static int rings, hundred_rings;
static int lives;
static int score;
static const char *get_sprite_id(int player_type);
static void update_shield(player_t *p);
static void update_glasses(player_t *p);
static void drop_glasses(player_t *p);
static int inside_loop(player_t *p);
static int got_crushed(player_t *p, brick_t *brick_up, brick_t *brick_right, brick_t *brick_down, brick_t *brick_left);
static void stickyphysics_hack(player_t *player, brick_list_t *brick_list, brick_t **brick_downleft, brick_t **brick_down, brick_t **brick_downright);


/*
 * player_create()
 * Creates a player
 */
player_t *player_create(int type)
{
    int i;
    player_t *p = mallocx(sizeof *p);

    logfile_message("player_create(%d)", type);

    switch(type) {
        case PL_SONIC: p->name = str_dup("Surge"); break;
        case PL_TAILS: p->name = str_dup("Neon"); break;
        case PL_KNUCKLES: p->name = str_dup("Charge"); break;
        default: p->name = str_dup("Unknown"); break;
    }

    p->type = type;
    p->actor = actor_create();
    p->disable_movement = FALSE;
    p->in_locked_area = FALSE;
    p->at_some_border = FALSE;

    p->spin = p->spin_dash = p->braking = p->flying = p->climbing = p->landing = p->spring = FALSE;
    p->is_fire_jumping = FALSE;
    p->getting_hit = p->dying = p->dead = p->blinking = FALSE;
    p->on_moveable_platform = FALSE;
    p->lock_accel = LOCKACCEL_NONE;
    p->flight_timer = p->blink_timer = p->death_timer = 0.0;
    p->disable_jump_for = 0.0;

    p->glasses = actor_create();
    p->got_glasses = FALSE;

    p->shield = actor_create();
    p->shield_type = SH_NONE;

    p->invincible = FALSE;
    p->invtimer = 0;
    for(i=0; i<PLAYER_MAX_INVSTAR; i++) {
        p->invstar[i] = actor_create();
        actor_change_animation(p->invstar[i], sprite_get_animation("SD_INVSTAR", 0));
    }

    p->got_speedshoes = FALSE;
    p->speedshoes_timer = 0;

    p->disable_wall = PLAYER_WALL_NONE;
    p->entering_loop = FALSE;
    p->at_loopfloortop = FALSE;
    p->bring_to_back = FALSE;

    switch(p->type) {
        case PL_SONIC:
            p->actor->acceleration = 250;
            p->actor->maxspeed = 700;
            p->actor->jump_strength = 400;
            p->actor->input = input_create_user();
            actor_change_animation( p->actor , sprite_get_animation(get_sprite_id(PL_SONIC), 0) );
            break;

        case PL_TAILS:
            p->actor->acceleration = 200;
            p->actor->maxspeed = 600;
            p->actor->jump_strength = 360;
            p->actor->input = input_create_user();
            actor_change_animation( p->actor , sprite_get_animation(get_sprite_id(PL_TAILS), 0) );
            break;

        case PL_KNUCKLES:
            p->actor->acceleration = 200;
            p->actor->maxspeed = 600;
            p->actor->jump_strength = 360;
            p->actor->input = input_create_user();
            actor_change_animation( p->actor , sprite_get_animation(get_sprite_id(PL_KNUCKLES), 0) );
            break;
    }

    hundred_rings = rings = 0;
    logfile_message("player_create() ok");
    return p;
}


/*
 * player_destroy()
 * Destroys a player
 */
void player_destroy(player_t *player)
{
    int i;

    for(i=0; i<PLAYER_MAX_INVSTAR; i++)
        actor_destroy(player->invstar[i]);

    actor_destroy(player->glasses);
    actor_destroy(player->actor);
    free(player->name);
    free(player);
}



/*
 * player_update()
 * Updates the player
 */
void player_update(player_t *player, player_t *team[3], brick_list_t *brick_list)
{
    actor_t *act = player->actor;

    if(player->blinking) {
        player->blink_timer += timer_get_delta();
        act->visible = (timer_get_ticks() % 250) < 125;
        if(player->blink_timer >= PLAYER_MAX_BLINK) {
            player->getting_hit = player->blinking = FALSE;
            act->visible = TRUE;
        }
    }

    if(player->disable_movement) {
        if(player->spin)
            actor_change_animation(player->actor, sprite_get_animation(get_sprite_id(player->type), 3));
        else if(player->spring)
            actor_change_animation(player->actor, sprite_get_animation(get_sprite_id(player->type), 13));
    }
    else
        actor_move(act, player_platform_movement(player, team, brick_list, level_gravity()));
}


/*
 * player_render()
 * Rendering function
 */
void player_render(player_t *player, v2d_t camera_position)
{
    actor_t *act = player->actor;
    v2d_t hot_spot = act->hot_spot;
    v2d_t position = act->position;
    v2d_t s_hot_spot = v2d_new(0,0);
    v2d_t starpos;
    int i, invangle[PLAYER_MAX_INVSTAR];
    float x, angoff, ang = act->angle, s_ang = 0;



    /* invencibility stars */
    if(player->invincible) {
        int maxf = sprite_get_animation("SD_INVSTAR", 0)->frame_count;
        player->invtimer += timer_get_delta();

        for(i=0; i<PLAYER_MAX_INVSTAR; i++) {
            invangle[i] = (180*4) * timer_get_ticks()*0.001 + (i+1)*(360/PLAYER_MAX_INVSTAR);
            starpos.x = 30*cos(invangle[i]*PI/180);
            starpos.y = ((timer_get_ticks()+i*400)%2000)/40;
            starpos = v2d_rotate(starpos,ang);
            player->invstar[i]->position.x = act->position.x + starpos.x;
            player->invstar[i]->position.y = act->position.y - starpos.y + 5;
            actor_change_animation_frame(player->invstar[i], random(maxf));
        }

        if(player->invtimer >= PLAYER_MAX_INVINCIBILITY)
            player->invincible = FALSE;
    }


    /* shields and glasses */
    if(player->got_glasses)
        update_glasses(player);

    if(player->shield_type != SH_NONE)
        update_shield(player);



    /* player's specific routines (before rendering) */
    switch(player->type) {
        case PL_SONIC:
            break;

        case PL_TAILS:
            /* tails' jump hack */
            if(act->is_jumping && act->animation == sprite_get_animation(get_sprite_id(PL_TAILS), 3)) {
                int rotate = ((fabs(act->speed.x)>100) || input_button_down(act->input,IB_RIGHT) || input_button_down(act->input,IB_LEFT));
                int left = (act->mirror & IF_HFLIP);
                act->hot_spot = v2d_new(actor_image(act)->w*0.5, actor_image(act)->h*0.9);
                if(act->speed.y > 0 && !rotate) act->hot_spot.x *= 0.9/0.5;
                if(act->speed.y < 0) {
                    angoff = left ? 3*PI/2 : PI/2;
                    act->angle = ang + angoff;
                    if(rotate)
                        act->angle -= (left?-1:1) * (PI/2) * (act->jump_strength+act->speed.y)/act->jump_strength;
                    else
                        act->position.x -= actor_image(act)->h*(left?0.5:0.0);
                }
                else {
                    angoff = left ? PI/2 : 3*PI/2;
                    act->angle = ang + angoff;
                    if(rotate) {
                        if(act->speed.y < act->jump_strength)
                            act->angle += (left?-1:1) * (PI/2) * (act->jump_strength-act->speed.y)/act->jump_strength;
                    }
                    else
                        act->position.x += actor_image(act)->h*(left?0.1:-0.2);
                }

                /* fix shield position */
                if(player->shield_type != SH_NONE) {
                    v2d_t voff;
                    if(rotate)
                        voff = v2d_rotate(v2d_new(left?-13:13,-13), -act->angle);
                    else if(act->mirror & IF_HFLIP)
                        voff = v2d_new((act->speed.y>0) ? -13 : 13, -15);
                    else
                        voff = v2d_new((act->speed.y>0 ? 7 : -7), -15);
                    s_ang = player->shield->angle;
                    s_hot_spot = player->shield->hot_spot;
                    player->shield->position = v2d_add(act->position, voff);
                }
            }
            break;

        case PL_KNUCKLES:
            break;
    }


    /* rendering */
    for(i=0;i<PLAYER_MAX_INVSTAR && player->invincible;i++) {
        if(invangle[i]%360 >= 180)
            actor_render(player->invstar[i], camera_position);
    }

    x = act->angle;
    act->angle = (act->is_jumping || player->spin) ? x : old_school_angle(x);
    actor_render(act, camera_position);
    act->angle = x;

    if(player->got_glasses)
        actor_render(player->glasses, camera_position);
    if(player->shield_type != SH_NONE)
        actor_render(player->shield, camera_position);
    for(i=0;i<PLAYER_MAX_INVSTAR && player->invincible;i++) {
        if(invangle[i]%360 < 180)
            actor_render(player->invstar[i], camera_position);
    }


    /* player's specific routines (after rendering) */
    switch(player->type) {
        case PL_SONIC:
            break;

        case PL_TAILS:
            if(act->is_jumping && act->animation == sprite_get_animation(get_sprite_id(PL_TAILS), 3)) {
                act->position = position;
                act->angle = ang;
                act->hot_spot = hot_spot;

                if(player->shield_type != SH_NONE) {
                    player->shield->angle = s_ang;
                    player->shield->hot_spot = s_hot_spot;
                }
            }
            break;

        case PL_KNUCKLES:
            break;
    }


    /* show collision detectors (debug) */
#ifdef SHOW_COLLISION_DETECTORS
    if(player->type == PL_TAILS) {
    float diff = -2, sqrsize = 2, top=0, middle=0, lateral=0;
    int slope = !((fabs(act->angle)<EPSILON)||(fabs(act->angle-PI/2)<EPSILON)||(fabs(act->angle-PI)<EPSILON)||(fabs(act->angle-3*PI/2)<EPSILON));
    switch(player->type) {
        case PL_SONIC:
            if(!slope) { top = 0.7; middle = 0.5; lateral = 0.4; }
            else       { top = 1.0; middle = 0.8; lateral = 0.5; }
            break;

        case PL_TAILS:
            if(!slope) { top = 0.7; middle = 0.5; lateral = 0.25; }
            else       { top = 1.0; middle = 0.7; lateral = 0.25; }
            break;

        case PL_KNUCKLES:
            if(!slope) { top = 0.7; middle = 0.5; lateral = 0.25; }
            else       { top = 1.0; middle = 0.7; lateral = 0.25; }
            break;
    }
    {
    float frame_width=actor_image(act)->w, frame_height=actor_image(act)->h;
    float offx=camera_position.x-VIDEO_SCREEN_W/2;
    float offy=camera_position.y-VIDEO_SCREEN_H/2;
    v2d_t feet      = act->position;
    v2d_t up        = v2d_add ( feet , v2d_rotate( v2d_new(0, -frame_height*top+diff), -act->angle) );
    v2d_t down      = v2d_add ( feet , v2d_rotate( v2d_new(0, -diff), -act->angle) ); 
    v2d_t left      = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -frame_height*middle), -act->angle) );
    v2d_t right     = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -frame_height*middle), -act->angle) );
    v2d_t upleft    = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -frame_height*top+diff), -act->angle) );
    v2d_t upright   = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -frame_height*top+diff), -act->angle) );
    v2d_t downleft  = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -diff), -act->angle) );
    v2d_t downright = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -diff), -act->angle) );
    if(player->type == PL_TAILS && act->carrying && fabs(act->angle)<EPSILON) { float h=actor_image(act->carrying)->h, k=act->speed.y>5?h*0.7:0; downleft.y += k; downright.y += k; down.y += k; left.y += h*middle+random(h)-h*0.5; right.y = left.y; }
    float cd_up[4] = { up.x-sqrsize-offx , up.y-sqrsize-offy , up.x+sqrsize-offx , up.y+sqrsize-offy };
    float cd_down[4] = { down.x-sqrsize-offx , down.y-sqrsize-offy , down.x+sqrsize-offx , down.y+sqrsize-offy };
    float cd_left[4] = { left.x-sqrsize-offx , left.y-sqrsize-offy , left.x+sqrsize-offx , left.y+sqrsize-offy };
    float cd_right[4] = { right.x-sqrsize-offx , right.y-sqrsize-offy , right.x+sqrsize-offx , right.y+sqrsize-offy };
    float cd_downleft[4] = { downleft.x-sqrsize-offx , downleft.y-sqrsize-offy , downleft.x+sqrsize-offx , downleft.y+sqrsize-offy };
    float cd_downright[4] = { downright.x-sqrsize-offx , downright.y-sqrsize-offy , downright.x+sqrsize-offx , downright.y+sqrsize-offy };
    float cd_upright[4] = { upright.x-sqrsize-offx , upright.y-sqrsize-offy , upright.x+sqrsize-offx , upright.y+sqrsize-offy };
    float cd_upleft[4] = { upleft.x-sqrsize-offx , upleft.y-sqrsize-offy , upleft.x+sqrsize-offx , upleft.y+sqrsize-offy };
    rectfill(video_get_backbuffer()->data, cd_up[0], cd_up[1], cd_up[2], cd_up[3], 0xFFFFFF);
    rectfill(video_get_backbuffer()->data, cd_down[0], cd_down[1], cd_down[2], cd_down[3], 0xFFFFFF);
    rectfill(video_get_backbuffer()->data, cd_left[0], cd_left[1], cd_left[2], cd_left[3], 0xFFFFFF);
    rectfill(video_get_backbuffer()->data, cd_right[0], cd_right[1], cd_right[2], cd_right[3], 0xFFFFFF);
    rectfill(video_get_backbuffer()->data, cd_downleft[0], cd_downleft[1], cd_downleft[2], cd_downleft[3], random(0xFFFFFF));
    rectfill(video_get_backbuffer()->data, cd_downright[0], cd_downright[1], cd_downright[2], cd_downright[3], random(0xFFFFFF));
    rectfill(video_get_backbuffer()->data, cd_upright[0], cd_upright[1], cd_upright[2], cd_upright[3], random(0xFFFFFF));
    rectfill(video_get_backbuffer()->data, cd_upleft[0], cd_upleft[1], cd_upleft[2], cd_upleft[3], random(0xFFFFFF));
    }
    }
#endif
}



/*
 * player_platform_movement()
 * Platform movement. Returns
 * a delta_space vector.
 *
 * Note: the actor's hot spot must
 * be defined on its feet.
 */
v2d_t player_platform_movement(player_t *player, player_t *team[3], brick_list_t *brick_list, float gravity)
{
    actor_t *act = player->actor;
    const char *sprite_id = get_sprite_id(player->type);
    float dt = timer_get_delta();
    float max_y_speed = 480, friction = 0, gravity_factor = 1.0;
    float maxspeed = act->maxspeed;
    v2d_t ds = v2d_new(0,0);
    int pushing_a_wall;
    int angle_question;
    int was_jumping = FALSE;
    int is_walking = (player->actor->animation == sprite_get_animation(sprite_id, 1));
    int at_right_border = FALSE, at_left_border = FALSE;
    int climbing_a_slope = FALSE;
    int block_tails_flight = FALSE;
    animation_t *animation = NULL;

    /* actor's collision detectors */
    int frame_width = actor_image(act)->w, frame_height = actor_image(act)->h;
    int slope = !((fabs(act->angle)<EPSILON)||(fabs(act->angle-PI/2)<EPSILON)||(fabs(act->angle-PI)<EPSILON)||(fabs(act->angle-3*PI/2)<EPSILON));
    float diff = -2, sqrsize = 2, top=0, middle=0, lateral=0;
    brick_t *brick_up, *brick_down, *brick_right, *brick_left;
    brick_t *brick_upright, *brick_downright, *brick_downleft, *brick_upleft;
    brick_t *brick_tmp;
    v2d_t up, upright, right, downright, down, downleft, left, upleft;
    v2d_t feet = act->position;
    switch(player->type) {
        case PL_SONIC:
            if(!slope) { top = 0.7; middle = 0.5; lateral = 0.4; }
            else       { top = 1.0; middle = 0.8; lateral = 0.5; }
            break;

        case PL_TAILS:
            if(!slope) { top = 0.7; middle = 0.5; lateral = 0.25; }
            else       { top = 1.0; middle = 0.7; lateral = 0.25; }
            break;

        case PL_KNUCKLES:
            if(!slope) { top = 0.7; middle = 0.5; lateral = 0.25; }
            else       { top = 1.0; middle = 0.7; lateral = 0.25; }
            break;
    }
    up        = v2d_add ( feet , v2d_rotate( v2d_new(0, -frame_height*top+diff), -act->angle) );
    down      = v2d_add ( feet , v2d_rotate( v2d_new(0, -diff), -act->angle) ); 
    left      = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -frame_height*middle), -act->angle) );
    right     = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -frame_height*middle), -act->angle) );
    upleft    = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -frame_height*top+diff), -act->angle) );
    upright   = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -frame_height*top+diff), -act->angle) );
    downleft  = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -diff), -act->angle) );
    downright = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -diff), -act->angle) );
    if(player->type == PL_TAILS && act->carrying && fabs(act->angle)<EPSILON) { float h=actor_image(act->carrying)->h, k=act->speed.y>5?h*0.7:0; downleft.y += k; downright.y += k; down.y += k; left.y += h*middle+random(h)-h*0.5; right.y = left.y; }
    actor_corners_disable_detection(player->disable_wall & PLAYER_WALL_LEFT, player->disable_wall & PLAYER_WALL_RIGHT, player->disable_wall & PLAYER_WALL_BOTTOM, player->disable_wall & PLAYER_WALL_TOP);
    actor_corners_set_floor_priority( (player->disable_wall & PLAYER_WALL_BOTTOM) ? FALSE : TRUE );
    actor_corners_ex(act, sqrsize, up, upright, right, downright, down, downleft, left, upleft, brick_list, &brick_up, &brick_upright, &brick_right, &brick_downright, &brick_down, &brick_downleft, &brick_left, &brick_upleft);
    actor_corners_restore_floor_priority();

    /* is the player dying? */
    if(player->dying) {
        act->speed.x = 0;
        act->speed.y = min(max_y_speed, act->speed.y+gravity*dt);
        act->mirror = IF_NONE;
        act->angle = 0;
        act->visible = TRUE;
        player->blinking = FALSE;
        player->death_timer += dt;
        player->dead = (player->death_timer >= 2.5);
        actor_change_animation(act, sprite_get_animation(sprite_id, 8));
        return v2d_new(0, act->speed.y*dt + 0.5*gravity*dt*dt);
    }
    else if(player->dead)
        return v2d_new(0,0);




    /* clouds */
    actor_handle_clouds(act, diff, &brick_up, &brick_upright, &brick_right, &brick_downright, &brick_down, &brick_downleft, &brick_left, &brick_upleft);



    /* carry */
    switch(player->type) {
        case PL_SONIC: act->carry_offset = v2d_new((act->mirror&IF_HFLIP)?7:-9,-40); break;
        case PL_TAILS: act->carry_offset = v2d_new((act->mirror&IF_HFLIP)?7:-7,-42); break;
        case PL_KNUCKLES: act->carry_offset = v2d_new((act->mirror&IF_HFLIP)?7:-7,-42); break;
    }

    /* I'm being carried */
    if(act->carried_by != NULL) {
        player_t *host = NULL;
        int i, host_id = 0, my_id = 0;
        actor_t *car = act->carried_by;

        /* my id? */
        for(i=0; i<3; i++) {
            if(team[i] == player) {
                my_id = i;
                break;
            }
        }

        /* host = who is carrying me? */
        for(i=0; i<3; i++) {
            if(team[i]->actor == car) {
                /* I've found the host! */
                host = team[i];
                host_id = i;

                /* setting up some common flags... */
                player->disable_wall = host->disable_wall;
                player->entering_loop = host->entering_loop;
                player->at_loopfloortop = host->at_loopfloortop;
                player->bring_to_back = host->bring_to_back;

                /* done */
                break;
            }
        }

        /* actions */
        if(host && ((host->type == PL_TAILS && !host->flying) || host->getting_hit || host->dying || host->dead)) { /* what should host do? */
            /* put me down! */
            act->position = act->carried_by->position;
            act->carried_by->carrying = NULL;
            act->carried_by = NULL;
        }
        else if((brick_down && brick_down->brick_ref->angle == 0 && (int)car->speed.y >= 5) || player->getting_hit || player->dying || player->dead) { /* what should I do? */
            /* put me down! */
            act->position = act->carried_by->position;
            act->carried_by->carrying = NULL;
            act->carried_by = NULL;
        }
        else {
            /* carry me! */
            v2d_t offset = my_id < host_id ? v2d_multiply(car->speed, dt) : v2d_new(0,0);
            act->speed = v2d_new(0,0);
            act->mirror = car->mirror;
            act->angle = 0;
            actor_change_animation(act, sprite_get_animation(sprite_id, 25));
            act->position = v2d_subtract(v2d_add(car->position, offset), act->carry_offset);
            return v2d_new(0,0);
        }
    }




    /* oh no, I got crushed! */
    if(got_crushed(player, brick_up, brick_right, brick_down, brick_left)) {
        player_kill(player);
        return v2d_new(0,0);
    }


    /* speed shoes */
    if(player->got_speedshoes) {
        if(player->speedshoes_timer > PLAYER_MAX_SPEEDSHOES)
            player->got_speedshoes = FALSE;
        else {
            maxspeed *= 1.5;
            player->speedshoes_timer += dt;
        }
    }

    /* if the player jumps inside a loop, enable the floor collision detection */
    if(inside_loop(player)) {
        if(act->is_jumping)
            player->disable_wall &= ~PLAYER_WALL_BOTTOM;
    }


    /* disable spring mode */
    if(player->spring) {
        if((brick_down && (int)act->speed.y >= 0) || player->flying || player->climbing)
            player->spring = FALSE;
    }


    /* useful flags */
    pushing_a_wall = ((brick_right && input_button_down(act->input, IB_RIGHT)) || (brick_left && input_button_down(act->input, IB_LEFT))) && brick_down;
    player->on_moveable_platform = (v2d_magnitude(level_brick_move_actor(brick_down,act)) > EPSILON);


    /* wall collision */
    climbing_a_slope = brick_down && ((act->angle > 0 && act->angle < PI/2 && act->speed.x>0) || (act->angle > 3*PI/2 && act->angle < 2*PI && act->speed.x<0));
    if((climbing_a_slope && (brick_upleft || brick_upright)) || (fabs(act->angle) < EPSILON || fabs(act->angle-PI) < EPSILON)){
        if(brick_right) {
            if(brick_right->brick_ref->angle % 90 == 0 && (act->speed.x > EPSILON || right.x > brick_right->x)) {
                if(!climbing_a_slope || (climbing_a_slope && brick_right->brick_ref->angle != 90)) {
                    act->speed.x = 0;
                    act->position.x = brick_right->x + (feet.x-right.x);
                    if(!act->is_jumping && !player->flying && !player->climbing && fabs(act->speed.y)<EPSILON)
                        animation = sprite_get_animation(sprite_id, pushing_a_wall ? 14 : 0);
                    if(climbing_a_slope) return v2d_new(-5,0);
                }
            }
        }

        if(brick_left) {
            if(brick_left->brick_ref->angle % 90 == 0 && (act->speed.x < -EPSILON || left.x < brick_left->x+brick_left->brick_ref->image->w)) {
                if(!climbing_a_slope || (climbing_a_slope && brick_left->brick_ref->angle != 270)) {
                    act->speed.x = 0;
                    act->position.x = (brick_left->x+brick_left->brick_ref->image->w) + (feet.x-left.x);
                    if(!act->is_jumping && !player->flying && !player->climbing && fabs(act->speed.y)<EPSILON)
                        animation = sprite_get_animation(sprite_id, pushing_a_wall ? 14 : 0);
                    if(climbing_a_slope) return v2d_new(5,0);
                }
            }
        }

        if(act->position.x <= act->hot_spot.x) {
            player->spin = FALSE;
            at_left_border = TRUE;

            if(act->position.x < act->hot_spot.x) {
                act->speed.x = 0;
                act->position.x = act->hot_spot.x;
                if(brick_down) {
                    pushing_a_wall = TRUE;
                    animation = sprite_get_animation(sprite_id, 1);
                }
            }
        }

        if(act->position.x >= level_size().x - (actor_image(act)->w - act->hot_spot.x)) {
            player->spin = FALSE;
            at_right_border = TRUE;

            if(act->position.x > level_size().x - (actor_image(act)->w - act->hot_spot.x)) {
                act->speed.x = 0;
                act->position.x = level_size().x - (actor_image(act)->w - act->hot_spot.x);
                if(brick_down) {
                    pushing_a_wall = TRUE;
                    animation = sprite_get_animation(sprite_id, 1);
                }
            }
        }
    }


    /* y-axis */
    stickyphysics_hack(player, brick_list, &brick_downleft, &brick_down, &brick_downright);
    if(!player->climbing) {
        if(brick_down) {
            int ang = brick_down->brick_ref->angle;
            int spin_block;
            float factor, jump_sensitivity = 1.0;
            was_jumping = TRUE;
            act->ignore_horizontal = FALSE;
            player->is_fire_jumping = FALSE;
            act->is_jumping = FALSE;

            /* falling bricks? */
            if(brick_down->brick_ref && brick_down->brick_ref->behavior == BRB_FALL && brick_down->state == BRS_IDLE)
                brick_down->state = BRS_ACTIVE;

            /* stopped, walking, running, spinning... */
            if(fabs(act->speed.x) < EPSILON) {
                if(ang%180==0) player->spin = FALSE;

                /* look down */
                if(input_button_down(act->input, IB_DOWN)) {
                    /* crouch down */
                    if(!player->spin_dash)
                        animation = sprite_get_animation(sprite_id, 4);

                    /* spin dash - start */
                    if(input_button_pressed(act->input, IB_FIRE1)) {
                        animation = sprite_get_animation(sprite_id, 6);
                        player->spin_dash = TRUE;
                        sound_play( soundfactory_get("charge") );
                    }
                }
                else if(!pushing_a_wall) {
                    if(input_button_down(act->input, IB_UP)) { /* look up */
                        if(!(is_walking && player->at_some_border))
                            animation = sprite_get_animation(sprite_id, 5);
                    }
                    else if(!inside_loop(player)) {
                        /* stopped / ledge */
                        brick_t *minileft, *miniright;
                        v2d_t vminileft  = v2d_add ( feet , v2d_rotate( v2d_new(-8, 0), -act->angle) );
                        v2d_t vminiright = v2d_add ( feet , v2d_rotate( v2d_new(5, 0), -act->angle) );
                        v2d_t v = v2d_new(0,0);
                        actor_corners_ex(act, sqrsize, v, v, v, vminiright, v, vminileft, v, v, brick_list, NULL, NULL, NULL, &miniright, NULL, &minileft, NULL, NULL);
                        if(((!miniright && !(act->mirror&IF_HFLIP)) || (!minileft && (act->mirror&IF_HFLIP))) && !player->on_moveable_platform)
                            animation = sprite_get_animation(sprite_id, 10);
                        else {
                            if( !((input_button_down(act->input, IB_LEFT) && (at_left_border || player->at_some_border)) || (input_button_down(act->input, IB_RIGHT) && (at_right_border || player->at_some_border))) )
                                animation = sprite_get_animation(sprite_id, 0);
                            else {
                                act->mirror = at_left_border ? IF_HFLIP : IF_NONE;
                                animation = sprite_get_animation(sprite_id, 1);
                            }
                        }
                    }
                    else /* stopped */
                        animation = sprite_get_animation(sprite_id, 0);
                }
               
                /* spin dash */
                if(player->spin_dash) {

                    /* particles */
                    int a, sd_sig = act->mirror&IF_HFLIP ? 1 : -1, r;
                    v2d_t sd_relativepos, sd_speed;
                    image_t *pixel;

                    for(a=0; a<3; a++) {
                        r = 128+random(128);
                        pixel = image_create(1,1);
                        image_clear(pixel, image_rgb(r,r,r));

                        sd_relativepos = v2d_new(sd_sig*(7+random(7)), 2);
                        sd_speed = v2d_new(sd_sig * (50+random(200)), -random(200));

                        level_create_particle(pixel, v2d_add(act->position,sd_relativepos), sd_speed, TRUE);
                    }

                    /* end */
                    if(input_button_up(act->input, IB_DOWN) || level_editmode()) {
                        player->spin = TRUE;
                        player->spin_dash = FALSE;
                        if( ((act->mirror&IF_HFLIP)&&!brick_left&&!at_left_border) || (!(act->mirror&IF_HFLIP)&&!brick_right&&!at_right_border) )
                            act->speed.x = ( act->mirror & IF_HFLIP ? -1 : 1 )*maxspeed*1.35;
                        sound_play( soundfactory_get("release") );
                        player->disable_jump_for = 0.05; /* disable jumping for how long? */
                    }
                }


            }
            else {
                if(input_button_down(act->input, IB_DOWN)) {
                    if(!player->spin)
                        sound_play( soundfactory_get("roll") );
                    player->spin = TRUE;
                }


                if(!player->spin && !player->braking) {
                    float max_walking_speed = maxspeed * 0.75;
                    float min_braking_speed = maxspeed * 0.35;

                    /* animation */
                    if(fabs(act->speed.x) < max_walking_speed) {
                        if(!pushing_a_wall && act->speed.y >= 0) {
                               animation = sprite_get_animation(sprite_id, 1); /* walking animation */
                               actor_change_animation_speed_factor(act, 0.5 + 1.5*(fabs(act->speed.x) / max_walking_speed)); /* animation speed */
                        }
                    }
                    else
                        animation = sprite_get_animation(sprite_id, 2); /* running animation */

                    /* brake */
                    if(fabs(act->speed.x) >= min_braking_speed) {
                        if( (input_button_down(act->input, IB_RIGHT)&&(act->speed.x<0)) || (input_button_down(act->input, IB_LEFT)&&(act->speed.x>0)) ) {
                            sound_play( soundfactory_get("brake") );
                            player->braking = TRUE;
                        }
                    }

                }
                else if(player->spin)
                    animation = sprite_get_animation(sprite_id, 3); /* spinning */
                else if(player->braking) {
                    /* particles */
                    int r, sd_sig = act->mirror&IF_HFLIP ? 1 : -1;
                    v2d_t sd_relativepos, sd_speed;
                    image_t *pixel;

                    r = 128+random(128);
                    pixel = image_create(1,1);
                    image_clear(pixel, image_rgb(r,r,r));
                    sd_relativepos = v2d_new(sd_sig*(10-random(21)), 0);
                    sd_speed = v2d_new(sd_sig * (50+random(200)), -random(200));
                    level_create_particle(pixel, v2d_add(act->position,sd_relativepos), sd_speed, TRUE);

                    /* braking */
                    animation = sprite_get_animation(sprite_id, 7);
                    if(fabs(act->speed.x)<10) player->braking = FALSE;
                }
            }

            /* disable jump? */
            player->disable_jump_for = max(player->disable_jump_for - dt, 0.0);
            if(fabs(act->speed.x) < EPSILON)
                player->disable_jump_for = 0.0;

            /* jump */
            spin_block = !player->spin_dash;
            if(input_button_down(act->input, IB_FIRE1) && (player->disable_jump_for <= 0.0) && !input_button_down(act->input, IB_DOWN) && !brick_up && !player->landing && spin_block && !act->is_jumping) {
                if(act->speed.y >= 0 && (player->type != PL_KNUCKLES || (player->type == PL_KNUCKLES && !player->flying)))
                    sound_play( soundfactory_get("jump") );
                act->angle = NATURAL_ANGLE;
                act->is_jumping = TRUE;
                player->is_fire_jumping = TRUE;
                block_tails_flight = TRUE;
                player->spin = FALSE;
                animation = sprite_get_animation(sprite_id, 3);
                if(ang == 0) {
                    act->speed.y = (-act->jump_strength) * jump_sensitivity;
                }
                else if(ang > 0 && ang < 90) {
                    if(ang > 45) {
                        act->speed.x = min(act->speed.x, (-0.7*act->jump_strength) * jump_sensitivity);
                        act->speed.y = (-0.7*act->jump_strength) * jump_sensitivity;
                    }
                    else {
                        act->speed.x *= act->speed.x > 0 ? 0.5 : 1.0;
                        act->speed.y = (-act->jump_strength) * jump_sensitivity;
                    }
                }
                else if(ang == 90) {
                    actor_move(act, v2d_new(20*diff, 0));
                    act->speed.x = min(act->speed.x, (-act->jump_strength) * jump_sensitivity);
                    act->speed.y = (-act->jump_strength/2) * jump_sensitivity;
                }
                else if(ang > 90 && ang < 180) {
                    actor_move(act, v2d_new(0, -20*diff));
                    act->speed.x = min(act->speed.x, (-0.7*act->jump_strength) * jump_sensitivity);
                    act->speed.y = (act->jump_strength) * jump_sensitivity;
                }
                else if(ang == 180) {
                    actor_move(act, v2d_new(0, -20*diff));
                    act->speed.x *= -1;
                    act->speed.y = (act->jump_strength) * jump_sensitivity;
                }
                else if(ang > 180 && ang < 270) {
                    actor_move(act, v2d_new(0, -20*diff));
                    act->speed.x = max(act->speed.x, (0.7*act->jump_strength) * jump_sensitivity);
                    act->speed.y = (act->jump_strength) * jump_sensitivity;
                }
                else if(ang == 270) {
                    actor_move(act, v2d_new(-20*diff, 0));
                    act->speed.x = max(act->speed.x, (act->jump_strength) * jump_sensitivity);
                    act->speed.y = (-act->jump_strength/2) * jump_sensitivity;
                }
                else if(ang > 270 && ang < 360) {
                    if(ang < 315) {
                        act->speed.x = max(act->speed.x, (0.7*act->jump_strength) * jump_sensitivity);
                        act->speed.y = (-0.7*act->jump_strength) * jump_sensitivity;
                    }
                    else {
                        act->speed.x *= act->speed.x < 0 ? 0.5 : 1.0;
                        act->speed.y = (-act->jump_strength) * jump_sensitivity;
                    }
                }
            }


            /* slopes / speed issues */
            if(!act->is_jumping) {
                float mytan, super = 1.2, push = 25.0;
                if(ang > 0 && ang < 90) {
                    mytan = min(1, tan( ang*PI/180.0 ))*0.8;
                    if(fabs(act->speed.y) > EPSILON)
                        act->speed.x = (was_jumping && ang<=45) ? act->speed.x : max(-super*maxspeed, -1*mytan*act->speed.y);
                    else {
                        factor = (!(act->mirror & IF_HFLIP) ? 1.0 : 2.0) * mytan;
                        if(player->braking && ang<45)
                            factor *= 8.0 * (act->speed.x<0 ? -1.0/2.0 : 1.0/1.0);
                        else if(fabs(act->speed.x)<5) {
                            factor *= sin(ang*PI/180.0)*push;
                            player->lock_accel = LOCKACCEL_RIGHT;
                        }
                        act->speed.x = max(act->speed.x - factor*700*dt, -super*maxspeed);
                    }
                }
                else if(ang > 270 && ang < 360) {
                    mytan = min(1, -tan( ang*PI/180.0 ))*0.8;
                    if(fabs(act->speed.y) > EPSILON)
                        act->speed.x = (was_jumping && ang>=315) ? act->speed.x : min(super*maxspeed, 1*mytan*act->speed.y);
                    else {
                        factor = ((act->mirror & IF_HFLIP) ? 1.0 : 2.0) * mytan;
                        if(player->braking && ang>315)
                            factor *= 8.0 * (act->speed.x>0 ? -1.0/2.0 : 1.0/1.0);
                        else if(fabs(act->speed.x)<5) {
                            factor *= -sin(ang*PI/180.0)*push;
                            player->lock_accel = LOCKACCEL_LEFT;
                        }
                        act->speed.x = min(act->speed.x + factor*700*dt, super*maxspeed);
                    }
                }
            }

            if(ang%90 == 0)
                player->lock_accel = LOCKACCEL_NONE;

            if(brick_downleft && brick_downright && fabs(act->speed.x) < 40) {
                if(brick_downleft->brick_ref->angle > 270 && brick_downleft->brick_ref->angle < 360 && brick_downright->brick_ref->angle > 0 && brick_downright->brick_ref->angle < 90) {
                    if(!input_button_down(act->input, IB_LEFT) && !input_button_down(act->input, IB_RIGHT))
                        act->speed.x = 0;
                }
            }
        }
        else { /* not brick_down */
            player->braking = FALSE;
            player->lock_accel = LOCKACCEL_NONE;

            if(player->spin_dash) {
                player->spin_dash = FALSE;
                animation = sprite_get_animation(sprite_id, 1);
            }

            if(act->animation == sprite_get_animation(sprite_id, 0) || act->animation == sprite_get_animation(sprite_id, 10) || act->animation == sprite_get_animation(sprite_id, 5))
                animation = sprite_get_animation(sprite_id, 1);

            if(player->spring || is_walking || act->speed.y < 0)
                player->spin = FALSE;

            if(!inside_loop(player))
                act->angle = NATURAL_ANGLE;
        }


        /* jump sensitivity */
        if(!brick_down) {
            if(player->is_fire_jumping && act->speed.y < -act->jump_strength*PLAYER_JUMP_SENSITIVITY) {
                if(input_button_up(act->input, IB_FIRE1))
                    act->speed.y *= 0.7;
            }
        }

        /* who can fly? */
        if(player->type == PL_TAILS && player->flying) {
            gravity_factor = (player->flight_timer < TAILS_MAX_FLIGHT) ? 0.15 : 0.8;
            max_y_speed *= 0.3;
        }
        else
            gravity_factor = 1.0;

        /* y-axis movement */
        ds.y = (fabs(act->speed.y) > EPSILON) ? act->speed.y*dt + 0.5*(gravity*gravity_factor)*(dt*dt) : 0;
        if(!(player->type == PL_KNUCKLES && player->flying))
            act->speed.y = min(act->speed.y + (gravity*gravity_factor)*dt, max_y_speed);




        /* ceiling collision */
        angle_question = (brick_up && brick_up->brick_ref->angle%90!=0) && fabs(act->angle)<EPSILON;
        if(brick_up && (brick_up->brick_ref->angle % 90 == 0 || angle_question) && act->speed.y < -EPSILON) {
            act->position.y = (brick_up->y+brick_up->brick_ref->image->h) + (feet.y-up.y);
            act->speed.y = 10;

            /* this is a moving brick... and it's moving down */
            if(brick_up->brick_ref->behavior == BRB_CIRCULAR) {
                if(sin(brick_up->brick_ref->behavior_arg[3] * brick_up->value[0]) > 0) {
                    act->speed.y = 100;
                    ds = v2d_add(ds, v2d_multiply(level_brick_move_actor(brick_up, act), dt));
                    return ds;
                }
            }
        }



        /* floor collision */
        brick_tmp = brick_down;
        if(brick_tmp && !act->is_jumping) {
            int ang = brick_tmp->brick_ref->angle;
            act->speed.y = ds.y = 0;
            act->angle = ang * PI / 180.0;

            /* 0 floor */
            if(ang == 0) {
                v2d_t mov = level_brick_move_actor(brick_down, act); /* moveable platforms I */
                feet.y = brick_tmp->y;
                friction = 0;
                if(mov.y > EPSILON) /* if the moveable brick is going down... */
                    ds.y += mov.y*dt;
                else
                    act->position.y = feet.y+diff+1;
            }

            /* (0-90) slope */
            else if(ang > 0 && ang < 90) {
                feet.y = brick_tmp->y + brick_tmp->brick_ref->image->h - (act->position.x-brick_tmp->x)*tan(act->angle);
                if(act->speed.x<0) feet.y += 2.0;
                act->position.y = feet.y+diff;
                if(!(act->mirror & IF_HFLIP)) friction = 0.2;
            }

            /* 90 wall */
            else if(ang == 90) {
                if(fabs(act->speed.x) > 5) {
                    int myang = brick_downright ? brick_downright->brick_ref->angle : -1;
                    if(brick_downright && (myang >= ang && myang < ang+90)) {
                        feet.y = brick_tmp->x;
                        if(!player->flying) act->position.x = feet.y+diff;
                    }
                    else {
                        act->angle = NATURAL_ANGLE;
                        act->is_jumping = TRUE;
                        if(!player->spin && !player->flying) animation = sprite_get_animation(sprite_id, 1);
                        if(!inside_loop(player)) {
                            if(!player->flying) actor_move(act, v2d_new(6.5*diff, 0));
                            act->speed = v2d_new(0, -0.9*fabs(act->speed.x));
                        }
                    }
                }
                else {
                    act->angle = NATURAL_ANGLE;
                    if(!player->flying) actor_move(act, v2d_new(5*diff, 0));
                    act->is_jumping = TRUE;
                    act->ignore_horizontal = FALSE;
                }
                if(!(act->mirror & IF_HFLIP)) friction = 1.5;
            }

            /* (90-180) slope */
            else if(ang > 90 && ang < 180) {
                if(fabs(act->speed.x) > 5) {
                    feet.y = brick_tmp->y - (act->position.x-brick_tmp->x)*tan(act->angle);
                    act->position.y = feet.y-diff;
                }
                else {
                    act->angle = NATURAL_ANGLE;
                    actor_move(act, v2d_new(0, -15*diff));
                    act->is_jumping = TRUE;
                }
                friction = 1.5;
            }

            /* 180 ceiling */
            else if(ang == 180) {
                if(fabs(act->speed.x) > 5) {
                    feet.y = brick_tmp->y + brick_tmp->brick_ref->image->h;
                    act->position.y = feet.y-diff;

                    /* end of ceil */
                    if( (act->speed.x > 0 && !brick_downright) || (act->speed.x < 0 && !brick_downleft) ) {
                        actor_move(act, v2d_new(0, 15*diff));
                        act->is_jumping = TRUE;
                        act->speed.x *= -1;
                        act->mirror = act->speed.x<0 ? IF_HFLIP : IF_NONE;
                        act->angle = NATURAL_ANGLE;
                    }
                }
                else {
                    act->angle = NATURAL_ANGLE;
                    actor_move(act, v2d_new(0, -20*diff));
                    act->is_jumping = TRUE;
                    act->speed.x = 0;
                }
                friction = 1.2;
            }

            /* (180-270) slope */
            else if(ang > 180 && ang < 270) {
                if(fabs(act->speed.x) > 5) {
                    feet.y = brick_tmp->y + brick_tmp->brick_ref->image->h - (act->position.x-brick_tmp->x)*tan(act->angle);
                    act->position.y = feet.y-diff;
                }
                else {
                    act->angle = NATURAL_ANGLE;
                    actor_move(act, v2d_new(0, -15*diff));
                    act->is_jumping = TRUE;
                }
                friction = 1.5;
            }

            /* 270 wall */
            else if(ang == 270) {
                if(fabs(act->speed.x) > 5) {
                    int myang = brick_downleft ? brick_downleft->brick_ref->angle : -1;
                    if(brick_downleft && (myang > ang-90 && myang <= ang)) {
                        feet.y = brick_tmp->x + brick_tmp->brick_ref->image->w;
                        if(!player->flying) act->position.x = feet.y-diff;
                    }
                    else {
                        act->angle = NATURAL_ANGLE;
                        act->is_jumping = TRUE;
                        if(!player->spin && !player->flying) animation = sprite_get_animation(sprite_id, 1);
                        if(!inside_loop(player)) {
                            if(!player->flying) actor_move(act, v2d_new(-6.5*diff, 0));
                            act->speed = v2d_new(0, -0.9*fabs(act->speed.x));
                        }
                    }

                }
                else {
                    act->angle = NATURAL_ANGLE;
                    if(!player->flying) actor_move(act, v2d_new(-5*diff, 0));
                    act->is_jumping = TRUE;
                    act->ignore_horizontal = FALSE;
                }
                if(act->mirror & IF_HFLIP) friction = 1.5;
            }

            /* (270-360) slope */
            else if(ang > 270 && ang < 360) {
                feet.y = brick_tmp->y - (act->position.x-brick_tmp->x)*tan(act->angle);
                if(act->speed.x>0) feet.y += 2.0;
                act->position.y = feet.y+diff;
                if(act->mirror & IF_HFLIP) friction = 0.2;
            }
        }


        /* x-axis */
        ds.x = (fabs(act->speed.x) > EPSILON) ? act->speed.x*dt + 0.5*((1.0-friction)*act->acceleration)*(dt*dt) : 0;
        if(input_button_down(act->input, IB_LEFT) && !input_button_down(act->input, IB_RIGHT) && !player->spin && !player->braking && !player->landing && !player->getting_hit && player->lock_accel != LOCKACCEL_LEFT && !at_left_border) {
            if(!act->ignore_horizontal && (act->is_jumping || player->spring || is_walking || !input_button_down(act->input, IB_DOWN))) {
                act->mirror = IF_HFLIP;
                friction = (act->speed.x > 0) ? -1.0 : friction;
                if(act->speed.x >= -maxspeed*1.1)
                    act->speed.x = max(act->speed.x - (1.0-friction)*act->acceleration*dt, -maxspeed);
            }
        }
        else if(input_button_down(act->input, IB_RIGHT) && !input_button_down(act->input, IB_LEFT) && !player->spin && !player->braking && !player->landing && !player->getting_hit && player->lock_accel != LOCKACCEL_RIGHT && !at_right_border) {
            if(!act->ignore_horizontal && (act->is_jumping || player->spring || is_walking || !input_button_down(act->input, IB_DOWN))) {
                act->mirror = IF_NONE;
                friction = (act->speed.x < 0) ? -1.0 : friction;
                if(act->speed.x <= maxspeed*1.1)
                    act->speed.x = min(act->speed.x + (1.0-friction)*act->acceleration*dt, maxspeed);
            }
        }
        else if(brick_down) {
            int signal = 0;
            int ang = brick_down->brick_ref->angle;
            float factor;
            
            /* deceleration factor */
            if(player->spin)
                factor = 0.65;
            else if(player->braking)
                factor = 4.5;
            else if(player->landing)
                factor = 0.6;
            else
                factor = 1.0;

            /* deceleration */
            if(ang % 90 == 0) {
                if(ang == 90)
                    signal = -1;
                else if(ang == 270)
                    signal = 1;
                else {
                    if(act->speed.x > EPSILON) signal = -1;
                    else if(-act->speed.x > EPSILON) signal = 1;
                    else signal = 0;
                }
            }
            else if((ang > 90 && ang < 180) || (ang > 180 && ang < 270)){
                if(act->speed.x > EPSILON) signal = -1;
                else if(-act->speed.x > EPSILON) signal = 1;
                else signal = 0;
            }

            act->speed.x += signal*factor*act->acceleration*dt;
        }
    }


    /* spring mode */
    if(player->spring) {
        animation = sprite_get_animation(sprite_id, act->speed.y <= 0 ? 13 : 1);
        if(act->speed.y > 0) {
            player->spring = FALSE;
            act->is_jumping = FALSE;
        }
    }


    /* got hurt? */
    if(player->getting_hit) {
        if(!brick_down)
            animation = sprite_get_animation(sprite_id, 11);
        else
            player->getting_hit = FALSE;
    }



    /* character's specific routines */
    switch(player->type) {
        case PL_SONIC:
            break;

        case PL_TAILS:
            /* tails can fly */
            player->flight_timer += dt;
            if(brick_down && brick_down->brick_ref->angle != 90 && brick_down->brick_ref->angle != 270) { player->flying = FALSE; player->flight_timer = 0; }
            if(((act->is_jumping && act->speed.y>-act->jump_strength/3 && !block_tails_flight && !player->getting_hit) || player->flying) && input_button_pressed(act->input, IB_FIRE1) && !player->getting_hit) {
                if(player->flight_timer < TAILS_MAX_FLIGHT) {
                    if(!player->flying) player->flight_timer = 0;
                    act->speed.y = -level_gravity()*0.1;
                    player->flying = TRUE;
                    act->is_jumping = FALSE;
                    player->is_fire_jumping = FALSE;
                }
            }
            if(player->flying) {
                animation = sprite_get_animation(sprite_id, act->carrying ? 16 : 20);
                act->speed.x = clip(act->speed.x, -act->maxspeed/2, act->maxspeed/2);
                if(player->flight_timer >= TAILS_MAX_FLIGHT) { 
                    /* i'm tired of flying... */
                    sound_t *smp = soundfactory_get("tired of flying");
                    if(!sound_is_playing(smp)) sound_play(smp);
                    animation = sprite_get_animation(sprite_id, 19);
                }
                else {
                    sound_t *smp;
                    int i;

                    /* i'm flying! :) */
                    if(inside_loop(player)) act->angle = NATURAL_ANGLE;
                    smp = soundfactory_get("flying");
                    if(!sound_is_playing(smp)) sound_play(smp);

                    /* pick up: let's carry someone... */
                    for(i=0; i<3 && act->carrying == NULL; i++) {
                        if(team[i] != player && (int)act->speed.y <= 0) {
                            float ra[4] = { team[i]->actor->position.x+actor_image(team[i]->actor)->w*0.3, team[i]->actor->position.y, team[i]->actor->position.x+actor_image(team[i]->actor)->w*0.7, team[i]->actor->position.y+actor_image(team[i]->actor)->h*0.2 };
                            float rb[4] = { act->position.x+actor_image(act)->w*0.3, act->position.y+actor_image(act)->h*0.7, act->position.x+actor_image(act)->w*0.7, act->position.y+actor_image(act)->h };
                            int collision = bounding_box(ra, rb);
                            int can_be_carried = (team[i]->actor->carried_by == NULL && !team[i]->dying && !team[i]->dead && !team[i]->climbing && !team[i]->landing && !team[i]->getting_hit);
                            if(collision && can_be_carried && !brick_down) {
                                act->carrying = team[i]->actor;
                                team[i]->actor->carried_by = act;
                                team[i]->spin = team[i]->spin_dash = team[i]->braking = team[i]->flying = team[i]->spring = team[i]->on_moveable_platform = FALSE;
                                sound_play( soundfactory_get("touch the wall") );
                            }
                        }
                    }
                }
            }
            else if(act->animation == sprite_get_animation(sprite_id, act->carrying ? 16 : 20))
                animation = sprite_get_animation(sprite_id, 1); /* if you're not flying, don't play the flying animation */

            break;

        case PL_KNUCKLES:
            /* knuckles can fly too! */
            if(((act->is_jumping && act->speed.y>-0.7*act->jump_strength) || player->flying) && input_button_pressed(act->input, IB_FIRE1) && !brick_down && !player->getting_hit) {
                act->speed.y = 50;
                player->flying = TRUE;
                act->is_jumping = FALSE;
                player->is_fire_jumping = FALSE;
                act->speed.x = (act->mirror & IF_HFLIP) ? min(-100, act->speed.x) : max(100, act->speed.x);
            }


            /* fly? */
            if(player->flying) {
                int turning = (input_button_down(act->input, IB_LEFT) && act->speed.x > 0) || (input_button_down(act->input, IB_RIGHT) && act->speed.x < 0);
                int floor = (brick_down && fabs(brick_down->brick_ref->angle*PI/180.0 - NATURAL_ANGLE) < EPSILON);
                turning += (act->animation == sprite_get_animation(sprite_id, 21)) && !actor_animation_finished(act);

                /* i'm flying... */
                if(!floor && act->animation != sprite_get_animation(sprite_id, 19) && !player->landing) {
                    if(!(act->mirror & IF_HFLIP)) {
                        animation = sprite_get_animation(sprite_id, turning ? 21 : 20);
                        act->speed.x = min(act->speed.x + (0.5*act->acceleration)*dt, maxspeed/2);
                    }
                    else {
                        animation = sprite_get_animation(sprite_id, turning ? 21 : 20);
                        act->speed.x = max(act->speed.x - (0.5*act->acceleration)*dt, -maxspeed/2);
                    }
                }

                /* end of flight */
                if(floor) {
                    /* collided with the floor */
                    player->landing = TRUE;
                    act->is_jumping = FALSE;
                    animation = sprite_get_animation(sprite_id, 19);
                    act->speed.y = 0; ds.y = 0;
                    player->climbing = FALSE;
                }
                else if(input_button_up(act->input, IB_FIRE1)) {
                    /* knuckles doesn't want to fly anymore */
                    player->flying = FALSE;
                    animation = sprite_get_animation(sprite_id, 18);
                }
                else {
                    int t;
                    brick_t *try_me[5] = { brick_left , brick_downleft , brick_right , brick_downright , brick_down };
                    for(t=0; t<5; t++) {
                        brick_tmp = try_me[t];
                        if(brick_tmp && brick_tmp->brick_ref->angle%90!=0) {
                            /* collided with a slope while flying? */
                            player->flying = FALSE;
                            player->landing = FALSE;
                        }
                    }
                }

                /* wall climbing - begin */
                if(!floor && !brick_up) {
                    if((brick_left && brick_left->brick_ref->angle%90==0) || (brick_right && brick_right->brick_ref->angle%90==0)) {
                        player->climbing = TRUE;                        
                        player->flying = FALSE;
                        sound_play( soundfactory_get("touch the ground") );
                    }
                }
            }


            /* no more landing */
            if(player->landing) {
                if(fabs(act->speed.x) < EPSILON || !brick_down)
                    player->flying = player->landing = FALSE;
            }


            /* wall climbing */
            if(player->climbing) {
                v2d_t pre_ds = v2d_new(0,0);
                act->speed.x = ds.x = 0;
                if(brick_left && !brick_right) act->mirror |= IF_HFLIP;
                if(brick_right && !brick_left) act->mirror &= ~IF_HFLIP;

                /* climbing a moving brick */
                pre_ds = v2d_add(pre_ds, v2d_multiply(level_brick_move_actor(brick_left, act), dt));
                pre_ds = v2d_add(pre_ds, v2d_multiply(level_brick_move_actor(brick_right, act), dt));
                if((pre_ds.y <= 0 && !brick_up) || (pre_ds.y >= 0 && !brick_down) || (!brick_left && brick_right))
                    ds = v2d_add(ds, pre_ds);

                /* climbing... */
                if(brick_left || brick_right) {

                    /* knuckles doesn't want to climb the wall anymore */
                    if(input_button_pressed(act->input, IB_FIRE1)) {
                        animation_t *an_a = sprite_get_animation(sprite_id, 17);
                        animation_t *an_b = sprite_get_animation(sprite_id, 22);
                        if(act->animation == an_a || act->animation == an_b) { /* no wall kicking */
                            player->climbing = FALSE; 
                            act->is_jumping = TRUE;
                            player->is_fire_jumping = TRUE;
                            act->speed.x = ((act->mirror&IF_HFLIP)?1:-1)*0.7*act->jump_strength;
                            act->speed.y = -0.5*act->jump_strength;
                            if(brick_left && !brick_right) act->mirror &= ~IF_HFLIP;
                            if(!brick_left && brick_right) act->mirror |= IF_HFLIP;
                            animation = sprite_get_animation(sprite_id, 3);
                            sound_play( soundfactory_get("jump") );
                        }
                    }
                    else {
                        /* up or down? */
                        if(input_button_down(act->input, IB_UP)) {
                            if(!brick_up) {
                                ds.y = (-maxspeed*0.1) * dt;
                                animation = sprite_get_animation(sprite_id, 17);
                            }
                        }
                        else if(input_button_down(act->input, IB_DOWN)) {
                            if(!brick_down) {
                                ds.y = (maxspeed*0.1) * dt;
                                animation = sprite_get_animation(sprite_id, 17);
                            }
                            else
                                player->climbing = FALSE; /* reached the ground */
                        }
                        else
                            animation = sprite_get_animation(sprite_id, 22);
                    }
                }

                /* end of wall climbing */
                else {
                    brick_tmp = (act->mirror&IF_HFLIP) ? brick_downleft : brick_downright;
                    if(brick_tmp) {
                        animation = sprite_get_animation(sprite_id, 23);
                        act->ignore_horizontal = TRUE;
                        ds = v2d_add(ds, v2d_multiply(level_brick_move_actor(brick_tmp, act), dt));
                        if(actor_animation_finished(act)) {
                            player->climbing = FALSE;
                            act->ignore_horizontal = FALSE;
                            act->speed = v2d_new(((act->mirror&IF_HFLIP)?-1:1)*maxspeed*0.15, -level_gravity()/12.5);
                            ds.x = ((act->mirror&IF_HFLIP)?-1:1)*5;
                        }
                    }
                    else {
                        player->climbing = FALSE;
                        act->is_jumping = TRUE;
                        animation = sprite_get_animation(sprite_id, 3);
                    }
                }


            }
            break;
    }


    /* almost done... */
    player->at_some_border = FALSE;
    if(animation) actor_change_animation(act, animation);
    if(fabs(act->speed.x) < 4) {
        player->braking = FALSE;
        if( (!input_button_down(act->input, IB_RIGHT) && !input_button_down(act->input, IB_LEFT)) || (input_button_down(act->input, IB_RIGHT) && input_button_down(act->input, IB_LEFT)) || player->spin || player->landing ) {
            ds.x = 0;
            act->speed.x = 0;
        }
    }
    ds.x += level_brick_move_actor(brick_down,act).x*dt; /* moveable platforms II */
    ds.x = ((act->position.x<=act->hot_spot.x) && act->speed.x<0) ? 0 : ds.x;
    ds.x = ((act->position.x>=level_size().x - (actor_image(act)->w-act->hot_spot.x)) && act->speed.x>0) ? 0 : ds.x;
    return ds;
}


/*
 * player_bounce()
 * Bounces
 */
void player_bounce(player_t *player)
{
    input_simulate_button_down(player->actor->input, IB_FIRE1);
    player->spring = FALSE;
    player->actor->speed.y = -player->actor->jump_strength;
    player->actor->is_jumping = TRUE;
    player->is_fire_jumping = FALSE;
    player->flying = FALSE;
}


/*
 * player_get_rings()
 * Returns the amount of rings
 * the player has got so far
 */
int player_get_rings()
{
    return rings;
}



/*
 * player_set_rings()
 * Sets a new amount of rings
 */
void player_set_rings(int r)
{
    rings = clip(r, 0, 9999);

    /* (100+) * k rings (k integer) = new life! */
    if(r/100 > hundred_rings) {
        hundred_rings = r/100;
        player_set_lives( player_get_lives()+1 );
        level_override_music( soundfactory_get("1up") );
    }
}



/*
 * player_get_lives()
 * How many lives does the player have?
 */
int player_get_lives()
{
    return lives;
}



/*
 * player_set_lives()
 * Sets the number of lives
 */
void player_set_lives(int l)
{
    lives = max(0, l);
}



/*
 * player_get_score()
 * Returns the score
 */
int player_get_score()
{
    return score;
}



/*
 * player_set_score()
 * Sets the score
 */
void player_set_score(int s)
{
    score = max(0, s);
}





/*
 * player_hit()
 * Hits a player. If it has no rings, then
 * it must die
 */
void player_hit(player_t *player)
{
    actor_t *act = player->actor;
    item_t *ring;
    int i;
    int get_hit = FALSE;

    if(!player->blinking && !player->dying && !player->invincible) {
        drop_glasses(player);
        if(player->shield_type != SH_NONE) {
            /* lose shield */
            get_hit = TRUE;
            player->shield_type = SH_NONE;
            sound_play( soundfactory_get("death") );
        }
        else if(rings > 0) {
            /* lose rings */
            get_hit = TRUE;
            for(i=0; i<min(player_get_rings(), 30); i++) {
                ring = level_create_item(IT_RING, act->position);
                ring_start_bouncing(ring);
            }
            player_set_rings(0);
            sound_play( soundfactory_get("ringless") );
        }
        else {
            /* death */
            player_kill(player);
        }
    }

    if(get_hit) {
        player->getting_hit = TRUE;
        player->flying = player->landing = player->climbing = player->spring = FALSE;
        player->is_fire_jumping = FALSE;
        player->spin_dash = player->spin = FALSE;
        player->blinking = TRUE;
        player->blink_timer = 0;
        act->speed.x = act->mirror&IF_HFLIP ? 200 : -200;
        act->speed.y = -act->jump_strength*0.75;
        actor_move(act, v2d_new(0, -5));
    }
}



/*
 * player_kill()
 * Kills a player
 */
void player_kill(player_t *player)
{
    if(!player->dying) {
        drop_glasses(player);
        player->shield_type = SH_NONE;
        player->invincible = FALSE;
        player->got_speedshoes = FALSE;
        player->dying = TRUE;
        player->death_timer = 0;
        player->spring = FALSE;
        player->actor->speed.y = -player->actor->jump_strength*1.2;
        player->flying = player->climbing = player->landing = FALSE;
        player->is_fire_jumping = FALSE;
        player->spin = player->spin_dash = FALSE;
        player->blinking = FALSE;
        sound_play( soundfactory_get("death") );
    }
}



/*
 * player_attacking()
 * Returns TRUE if a given player is attacking;
 * FALSE otherwise
 */
int player_attacking(player_t *player)
{
    animation_t *jump = sprite_get_animation(get_sprite_id(player->type), 3);
    return player->spin || player->spin_dash ||
           (/*player->actor->is_jumping &&*/ player->actor->animation == jump) ||
           (player->type == PL_KNUCKLES && (player->landing || player->flying));
}


/*
 * player_get_sprite_name()
 * Returns the name of the sprite used by the player
 */
const char *player_get_sprite_name(player_t *player)
{
    return get_sprite_id(player->type);
}



/* private functions */
const char *get_sprite_id(int player_type)
{
    switch(player_type) {
        case PL_SONIC:
            return "SD_SONIC";

        case PL_TAILS:
            return "SD_TAILS";

        case PL_KNUCKLES:
            return "SD_KNUCKLES";

        default:
            return "null";
    }
}


void update_glasses(player_t *p)
{
    int frame_id = 0, hflip = p->actor->mirror & IF_HFLIP;
    int visible = TRUE;
    float ang = old_school_angle(p->actor->angle);
    v2d_t gpos = v2d_new(0,0);
    v2d_t top = v2d_subtract(p->actor->position,v2d_rotate(v2d_new(0,p->actor->hot_spot.y),-ang));
    animation_t *anim = p->actor->animation;



    switch(p->type) {


        case PL_SONIC:
            if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 0)) {
                /* stopped */
                gpos = v2d_new(3,24);
                frame_id = 1;
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 1)) {
                /* walking */
                switch((int)p->actor->animation_frame) {
                    case 0: frame_id = 2; gpos = v2d_new(5,23); break;
                    case 1: frame_id = 2; gpos = v2d_new(4,25); break;
                    case 2: frame_id = 1; gpos = v2d_new(7,25); break;
                    case 3: frame_id = 1; gpos = v2d_new(5,23); break;
                    case 4: frame_id = 1; gpos = v2d_new(5,23); break;
                    case 5: frame_id = 1; gpos = v2d_new(4,24); break;
                    case 6: frame_id = 2; gpos = v2d_new(6,24); break;
                    case 7: frame_id = 2; gpos = v2d_new(6,23); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 2)) {
                /* running */
                frame_id = 1;
                gpos = v2d_new(8,26);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 5)) {
                /* look up */
                frame_id = 3;
                if((int)p->actor->animation_frame == 0)
                    gpos = v2d_new(0,19);
                else
                    gpos = v2d_new(-1,21);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 7)) {
                /* braking */
                frame_id = 1;
                if((int)p->actor->animation_frame < 2)
                    gpos = v2d_new(8,26);
                else
                    gpos = v2d_new(10,28);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 10)) {
                /* almost falling / ledge */
                frame_id = 1;
                switch((int)p->actor->animation_frame) {
                    case 0: gpos = v2d_new(1,22); break;
                    case 1: gpos = v2d_new(-1,23); break;
                    case 2: gpos = v2d_new(1,23); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 11)) {
                /* ringless */
                frame_id = 3;
                gpos = v2d_new(-4,30);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 12)) {
                /* breathing */
                frame_id = 3;
                gpos = v2d_new(1,19);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 13)) {
                /* spring */
                frame_id = 3;
                gpos = v2d_new(4,13);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 14)) {
                /* pushing */
                frame_id = 1;
                gpos = v2d_new(12,31);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 15)) {
                /* waiting */
                frame_id = 0;
                gpos = v2d_new(3,23);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_SONIC), 25)) {
                /* being carried */
                frame_id = 0;
                gpos = v2d_new(3,22);
            }
            else
                visible = FALSE;
            break;



        case PL_TAILS:
            if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 0)) {
                /* stopped */
                gpos = v2d_new(5,34);
                frame_id = 1;
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 1)) {
                /* walking */
                frame_id = 2;
                switch((int)p->actor->animation_frame) {
                    case 0: gpos = v2d_new(2,33); break;
                    case 1: gpos = v2d_new(3,33); break;
                    case 2: gpos = v2d_new(8,33); break;
                    case 3: gpos = v2d_new(3,32); break;
                    case 4: gpos = v2d_new(1,33); break;
                    case 5: gpos = v2d_new(3,33); break;
                    case 6: gpos = v2d_new(7,33); break;
                    case 7: gpos = v2d_new(3,32); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 2)) {
                /* running */
                frame_id = 2;
                if((int)p->actor->animation_frame == 0)
                    gpos = v2d_new(7,35);
                else
                    gpos = v2d_new(6,34);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 4)) {
                /* crouch down */
                frame_id = 1;
                gpos = v2d_new(9,44);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 5)) {
                /* look up */
                frame_id = 1;
                gpos = v2d_new(7,32);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 7)) {
                /* braking */
                frame_id = 1;
                if((int)p->actor->animation_frame == 0)
                    gpos = v2d_new(2,33);
                else
                    gpos = v2d_new(4,33);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 10)) {
                /* almost falling / ledge */
                frame_id = 4;
                switch((int)p->actor->animation_frame) {
                    case 0: gpos = v2d_new(5,33); break;
                    case 1: gpos = v2d_new(6,33); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 11)) {
                /* ringless */
                frame_id = 1;
                gpos = v2d_new(1,33);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 12)) {
                /* breathing */
                frame_id = 1;
                gpos = v2d_new(6,28);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 13)) {
                /* spring */
                frame_id = 3;
                gpos = v2d_new(2,17);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 14)) {
                /* pushing */
                frame_id = 1;
                gpos = v2d_new(9,35);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 15)) {
                /* waiting */
                frame_id = 4;
                switch((int)p->actor->animation_frame) {
                    case 0: case 8: case 9: case 10: gpos = v2d_new(5,34); break;
                    default: gpos = v2d_new(5,33); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 16)) {
                /* carrying */
                frame_id = 1;
                gpos = v2d_new(8,37);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 19)) {
                /* tired of flying */
                frame_id = 1;
                if((int)p->actor->animation_frame == 0)
                    gpos = v2d_new(9,39);
                else
                    gpos = v2d_new(9,40);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 20)) {
                /* flying */
                frame_id = 1;
                gpos = v2d_new(8,39);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_TAILS), 25)) {
                /* being carried */
                frame_id = 1;
                gpos = v2d_new(0,23);
            }
            else
                visible = FALSE;
            break;


        case PL_KNUCKLES:
            if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 0)) {
                /* stopped */
                frame_id = 1;
                gpos = v2d_new(1,24);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 1)) {
                /* walking */
                switch((int)p->actor->animation_frame) {
                    case 0: frame_id = 1; gpos = v2d_new(5,29); break;
                    case 1: frame_id = 2; gpos = v2d_new(5,29); break;
                    case 2: frame_id = 2; gpos = v2d_new(8,29); break;
                    case 3: frame_id = 2; gpos = v2d_new(9,28); break;
                    case 4: frame_id = 1; gpos = v2d_new(6,28); break;
                    case 5: frame_id = 1; gpos = v2d_new(6,29); break;
                    case 6: frame_id = 1; gpos = v2d_new(5,28); break;
                    case 7: frame_id = 1; gpos = v2d_new(4,27); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 2)) {
                /* running */
                frame_id = 1;
                gpos = v2d_new(7,29);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 4)) {
                /* crouch down */
                frame_id = 1;
                if((int)p->actor->animation_frame == 0)
                    gpos = v2d_new(0,31);
                else
                    gpos = v2d_new(0,40);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 5)) {
                /* look up */
                frame_id = 1;
                if((int)p->actor->animation_frame == 0)
                    gpos = v2d_new(0,21);
                else
                    gpos = v2d_new(-1,21);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 7)) {
                /* braking */
                frame_id = 0;
                gpos = v2d_new(-2,27);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 10)) {
                /* almost falling / ledge */
                frame_id = 1;
                switch((int)p->actor->animation_frame) {
                    case 0: gpos = v2d_new(9,30); break;
                    case 1: gpos = v2d_new(8,27); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 11)) {
                /* ringless */
                frame_id = 1;
                gpos = v2d_new(-3,27);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 12)) {
                /* breathing */
                frame_id = 1;
                gpos = v2d_new(5,24);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 13)) {
                /* spring */
                frame_id = 3;
                gpos = v2d_new(-1,16);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 14)) {
                /* pushing */
                switch((int)p->actor->animation_frame) {
                    case 0: frame_id = 1; gpos = v2d_new(5,29); break;
                    case 1: frame_id = 2; gpos = v2d_new(5,29); break;
                    case 2: frame_id = 2; gpos = v2d_new(8,29); break;
                    case 3: frame_id = 2; gpos = v2d_new(9,28); break;
                    case 4: frame_id = 1; gpos = v2d_new(6,28); break;
                    case 5: frame_id = 1; gpos = v2d_new(6,29); break;
                    case 6: frame_id = 1; gpos = v2d_new(5,28); break;
                    case 7: frame_id = 1; gpos = v2d_new(4,27); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 15)) {
                /* waiting */
                frame_id = 0;
                gpos = v2d_new(1,23);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 16)) {
                /* no more climbing */
                frame_id = 1;
                switch((int)p->actor->animation_frame) {
                    case 0: gpos = v2d_new(6,23); break;
                    case 1: gpos = v2d_new(5,20); break;
                    case 2: gpos = v2d_new(0,22); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 17)) {
                /* climbing */
                frame_id = 3;
                switch((int)p->actor->animation_frame) {
                    case 0: gpos = v2d_new(-1,22); break;
                    case 1: gpos = v2d_new(-2,20); break;
                    case 2: gpos = v2d_new(0,21); break;
                    case 3: gpos = v2d_new(-1,24); break;
                    case 4: gpos = v2d_new(0,23); break;
                    case 5: gpos = v2d_new(0,22); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 18)) {
                /* end of flight */
                frame_id = 1;
                if((int)p->actor->animation_frame == 0)
                    gpos = v2d_new(6,23);
                else
                    gpos = v2d_new(5,20);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 19)) {
                /* flying - ground */
                frame_id = 1;
                gpos = v2d_new(8,44);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 20)) {
                /* flying - air */
                frame_id = 1;
                gpos = v2d_new(8,39);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 21)) {
                /* flying - turn */
                frame_id = 4;
                switch((int)p->actor->animation_frame) {
                    case 0: gpos = v2d_new(-8,41); break;
                    case 1: gpos = v2d_new(0,43); break;
                    case 2: gpos = v2d_new(10,41); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 22)) {
                /* climbing - stopped */
                frame_id = 3;
                gpos = v2d_new(0,22);
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 23)) {
                /* climbing - reached the top */
                switch((int)p->actor->animation_frame) {
                    case 0: frame_id = 3; gpos = v2d_new(7,17); break;
                    case 1: frame_id = 3; gpos = v2d_new(11,15); break;
                    case 2: frame_id = 0; gpos = v2d_new(12,13); break;
                }
            }
            else if(anim == sprite_get_animation(get_sprite_id(PL_KNUCKLES), 25)) {
                /* being carried */
                frame_id = 0;
                gpos = v2d_new(0,23);
            }
            else
                visible = FALSE;
            break;
    }

    gpos.x *= hflip ? -1 : 1;
    actor_change_animation(p->glasses, sprite_get_animation("SD_GLASSES", frame_id));
    p->glasses->position = v2d_add(top, v2d_rotate(gpos, -ang));
    p->glasses->angle = ang;
    p->glasses->mirror = p->actor->mirror;
    p->glasses->visible = visible && p->actor->visible;
}



void drop_glasses(player_t *p)
{
    if(p->got_glasses) {
        v2d_t pos = v2d_add(p->actor->position, v2d_new(0,-27));
        item_t *item = level_create_item(IT_FALGLASSES, pos);
        falglasses_set_speed(item, v2d_new(-0.2f * p->actor->speed.x, -490.0f));
        p->got_glasses = FALSE;
    }
}



void update_shield(player_t *p)
{
    actor_t *sh = p->shield, *act = p->actor;
    v2d_t off = v2d_new(0,0);

    switch(p->shield_type) {

        case SH_SHIELD:
            off = v2d_new(0,-22);
            sh->position = v2d_add(act->position, v2d_rotate(off, -old_school_angle(act->angle)));
            actor_change_animation(sh, sprite_get_animation("SD_SHIELD", 0));
            break;

        case SH_FIRESHIELD:
            off = v2d_new(0,-22);
            sh->position = v2d_add(act->position, v2d_rotate(off, -old_school_angle(act->angle)));
            actor_change_animation(sh, sprite_get_animation("SD_FIRESHIELD", 0));
            break;

        case SH_THUNDERSHIELD:
            off = v2d_new(0,-22);
            sh->position = v2d_add(act->position, v2d_rotate(off, -old_school_angle(act->angle)));
            actor_change_animation(sh, sprite_get_animation("SD_THUNDERSHIELD", 0));
            break;

        case SH_WATERSHIELD:
            off = v2d_new(0,-22);
            sh->position = v2d_add(act->position, v2d_rotate(off, -old_school_angle(act->angle)));
            actor_change_animation(sh, sprite_get_animation("SD_WATERSHIELD", 0));
            break;

        case SH_ACIDSHIELD:
            off = v2d_new(0,-22);
            sh->position = v2d_add(act->position, v2d_rotate(off, -old_school_angle(act->angle)));
            actor_change_animation(sh, sprite_get_animation("SD_ACIDSHIELD", 0));
            break;

        case SH_WINDSHIELD:
            off = v2d_new(0,-22);
            sh->position = v2d_add(act->position, v2d_rotate(off, -old_school_angle(act->angle)));
            actor_change_animation(sh, sprite_get_animation("SD_WINDSHIELD", 0));
            break;
    }
}


/* is the player inside a loop? */
int inside_loop(player_t *p)
{
    return (p->disable_wall != PLAYER_WALL_NONE);
}

/* the player won't leave the floor unless necessary */
void stickyphysics_hack(player_t *player, brick_list_t *brick_list, brick_t **brick_downleft, brick_t **brick_down, brick_t **brick_downright)
{
    actor_t *act = player->actor;
    float oldy = act->position.y;

    if(NULL == *brick_down && !act->is_jumping && !player->is_fire_jumping && !player->flying && !player->climbing && !player->landing && !player->spring && !player->getting_hit && !player->dead && !player->dying) {
        int i;
        float sqrsize=2, diff=-2;
        brick_t *downleft, *down, *downright;
        for(i=0; i<8; i++) {
            act->position.y = oldy + (float)(1+i);
            actor_corners(act, sqrsize, diff, brick_list, NULL, NULL, NULL, &downright, &down, &downleft, NULL, NULL);
            if(NULL != down) {
                *brick_downleft = downleft;
                *brick_down = down;
                *brick_downright = downright;
                return;
            }
        }
    }

    act->position.y = oldy;
}

/* aaaargh!! the player is being crushed! */
int got_crushed(player_t *p, brick_t *brick_up, brick_t *brick_right, brick_t *brick_down, brick_t *brick_left)
{
    float sx, sy, t;

    if(p->climbing)
        return FALSE;

    /* y-axis */
    if(brick_up && brick_down && brick_up != brick_down) {
        if(brick_up->brick_ref->behavior == BRB_CIRCULAR && brick_up->brick_ref->property == BRK_OBSTACLE) {
            t = brick_up->value[0];
            sy = brick_up->brick_ref->behavior_arg[3];
            if(sin(sy * t) > 0) return TRUE; /* crushed! */
        }

        if(brick_down->brick_ref->behavior == BRB_CIRCULAR && brick_down->brick_ref->property == BRK_OBSTACLE) {
            t = brick_down->value[0];
            sy = brick_down->brick_ref->behavior_arg[3];
            if(sin(sy * t) < 0) return TRUE; /* crushed! */
        }
    }

    /* x-axis */
    if(brick_left && brick_right && brick_left != brick_right) {
        if(brick_left->brick_ref->behavior == BRB_CIRCULAR && brick_left->brick_ref->property == BRK_OBSTACLE) {
            t = brick_left->value[0];
            sx = brick_left->brick_ref->behavior_arg[2];
            if(cos(sx * t) > 0) return TRUE; /* crushed! */
        }

        if(brick_right->brick_ref->behavior == BRB_CIRCULAR && brick_right->brick_ref->property == BRK_OBSTACLE) {
            t = brick_right->value[0];
            sx = brick_right->brick_ref->behavior_arg[2];
            if(cos(sx * t) < 0) return TRUE; /* crushed! */
        }
    }

    /* I'm not being crushed */
    return FALSE;
}
