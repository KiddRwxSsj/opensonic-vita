/*
 * actor.c - actor module
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
#include "../core/global.h"
#include "../core/input.h"
#include "../scenes/level.h"
#include "../core/util.h"
#include "../core/logfile.h"
#include "../core/video.h"
#include "../core/timer.h"


/* constants */
#define MAGIC_DIFF              -2  /* platform movement & collision detectors magic */
#define SIDE_CORNERS_HEIGHT     0.5 /* height of the left/right sensors */


/* private data */
static int floor_priority = TRUE; /* default behavior: priority(floor) > priority(wall) */
static int slope_priority = TRUE; /* default behavior: priority(slope) > priority(floor) */
static brick_t* brick_at(brick_list_t *list, float rect[4]);
static int is_leftwall_disabled = FALSE;
static int is_rightwall_disabled = FALSE;
static int is_floor_disabled = FALSE;
static int is_ceiling_disabled = FALSE;

/* private functions */
static void calculate_rotated_boundingbox(const actor_t *act, v2d_t spot[4]);


/* actor functions */



/*
 * actor_create()
 * Creates an actor
 */
actor_t* actor_create()
{
    actor_t *act = mallocx(sizeof *act);

    act->spawn_point = v2d_new(0,0);
    act->position = act->spawn_point;
    act->angle = 0.0f;
    act->speed = v2d_new(0,0);
    act->maxspeed = 0.0f;
    act->acceleration = 0.0f;
    act->jump_strength = 0.0f;
    act->is_jumping = FALSE;
    act->ignore_horizontal = FALSE;
    act->input = NULL;

    act->animation = NULL;
    act->animation_frame = 0.0f;
    act->animation_speed_factor = 1.0f;
    act->mirror = IF_NONE;
    act->visible = TRUE;
    act->alpha = 1.0f;
    act->hot_spot = v2d_new(0,0);

    act->carried_by = NULL;
    act->carry_offset = v2d_new(0,0);
    act->carrying = NULL;

    return act;
}


/*
 * actor_destroy()
 * Destroys an actor
 */
void actor_destroy(actor_t *act)
{
    if(act->input)
        input_destroy(act->input);
    free(act);
}


/*
 * actor_render()
 * Default rendering function
 */
void actor_render(actor_t *act, v2d_t camera_position)
{
    float diff = MAGIC_DIFF;
    image_t *img;
    v2d_t tmp;

    if(act->visible && act->animation) {
        /* update animation */
        act->animation_frame += (act->animation->fps * act->animation_speed_factor) * timer_get_delta();
        if((int)act->animation_frame >= act->animation->frame_count) {
            if(act->animation->repeat)
                act->animation_frame = (int)act->animation_frame % act->animation->frame_count;
            else
                act->animation_frame = act->animation->frame_count-1;
        }

        /* render */
        tmp = act->position;
        img = actor_image(act);
        actor_move(act, v2d_new(0,-diff));
        if(fabs(act->angle) > EPSILON)
           image_draw_rotated(img, video_get_backbuffer(), (int)(act->position.x-(camera_position.x-VIDEO_SCREEN_W/2)), (int)(act->position.y-(camera_position.y-VIDEO_SCREEN_H/2)), (int)act->hot_spot.x, (int)act->hot_spot.y, act->angle, act->mirror);
        else if(fabs(act->alpha - 1.0f) > EPSILON)
           image_draw_trans(img, video_get_backbuffer(), (int)(act->position.x-act->hot_spot.x-(camera_position.x-VIDEO_SCREEN_W/2)), (int)(act->position.y-act->hot_spot.y-(camera_position.y-VIDEO_SCREEN_H/2)), image_rgb(255,255,255), act->alpha, act->mirror);
        else
           image_draw(img, video_get_backbuffer(), (int)(act->position.x-act->hot_spot.x-(camera_position.x-VIDEO_SCREEN_W/2)), (int)(act->position.y-act->hot_spot.y-(camera_position.y-VIDEO_SCREEN_H/2)), act->mirror);
        act->position = tmp;
    }
}



/*
 * actor_render_repeat_xy()
 * Rendering / repeat xy
 */
void actor_render_repeat_xy(actor_t *act, v2d_t camera_position, int repeat_x, int repeat_y)
{
    int i, j, w, h;
    image_t *img = actor_image(act);
    v2d_t final_pos;
    int mod_x, mod_y;

    /* C's % truncates toward zero, so for negative act->position (common
     * on parallax layers with negative scroll_speed, once the camera has
     * moved far enough) this does NOT wrap into [0, img->w) / [0, img->h)
     * like the tiling loop below assumes -- it leaves a tile-sized gap
     * that shows through as a black square (level_render() already
     * cleared the backbuffer to black before this runs). Normalize the
     * remainder into [0, m) explicitly instead. */
    mod_x = repeat_x ? (((int)act->position.x % img->w) + img->w) % img->w : 0;
    mod_y = repeat_y ? (((int)act->position.y % img->h) + img->h) % img->h : 0;

    final_pos.x = (repeat_x ? mod_x : (int)act->position.x) - act->hot_spot.x-(camera_position.x-VIDEO_SCREEN_W/2) - (repeat_x?img->w:0);
    final_pos.y = (repeat_y ? mod_y : (int)act->position.y) - act->hot_spot.y-(camera_position.y-VIDEO_SCREEN_H/2) - (repeat_y?img->h:0);

    if(act->visible && act->animation) {
        /* update animation */
        act->animation_frame += (act->animation->fps * act->animation_speed_factor) * timer_get_delta();
        if((int)act->animation_frame >= act->animation->frame_count) {
            if(act->animation->repeat)
                act->animation_frame = (int)act->animation_frame % act->animation->frame_count;
            else
                act->animation_frame = act->animation->frame_count-1;
        }

        /* render */
        /* minimum number of tiles to cover the screen + 1 tile of margin
           (the margin covers the sub-tile offset applied by final_pos).
           NOTE: the previous formula (VIDEO_SCREEN_W/img->w + 3) assumed
           small tiles; with tiles wider than the screen (common in
           parallax layers, e.g. 888px vs a 320px screen) the integer
           division gave 0 and stayed fixed at 3 full tiles regardless
           of how much was left off-screen -> 8x or more overdraw. */
        w = repeat_x ? ((VIDEO_SCREEN_W + img->w - 1) / img->w + 1) : 1;
        h = repeat_y ? ((VIDEO_SCREEN_H + img->h - 1) / img->h + 1) : 1;
        for(i=0; i<w; i++) {
            int tx = (int)final_pos.x + i*img->w;
            if(tx + img->w <= 0 || tx >= VIDEO_SCREEN_W)
                continue; /* column of tiles completely off-screen */
            for(j=0; j<h; j++) {
                int ty = (int)final_pos.y + j*img->h;
                if(ty + img->h <= 0 || ty >= VIDEO_SCREEN_H)
                    continue; /* tile completely off-screen */
                image_draw(img, video_get_backbuffer(), tx, ty, act->mirror);
            }
        }
    }
}



/*
 * actor_render_corners()
 * Renders the corners (sensors) of the actor.
 *
 * Let X be the original collision detector spot. So:
 * diff = distance between the real collision detector spot and X
 * sqrsize = size of the collision detector
 */
void actor_render_corners(const actor_t *act, float sqrsize, float diff, v2d_t camera_position)
{
    uint32 c[2];
    v2d_t offset = v2d_subtract(camera_position, v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2));
    int frame_width = actor_image(act)->w;
    int frame_height = actor_image(act)->h;

    v2d_t feet      = v2d_subtract(act->position, offset);
    v2d_t vup        = v2d_add ( feet , v2d_rotate( v2d_new(0, -frame_height+diff), -act->angle) );
    v2d_t vdown      = v2d_add ( feet , v2d_rotate( v2d_new(0, -diff), -act->angle) ); 
    v2d_t vleft      = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width/2+diff, -frame_height*SIDE_CORNERS_HEIGHT), -act->angle) );
    v2d_t vright     = v2d_add ( feet , v2d_rotate( v2d_new(frame_width/2-diff, -frame_height*SIDE_CORNERS_HEIGHT), -act->angle) );
    v2d_t vupleft    = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width/2+diff, -frame_height+diff), -act->angle) );
    v2d_t vupright   = v2d_add ( feet , v2d_rotate( v2d_new(frame_width/2-diff, -frame_height+diff), -act->angle) );
    v2d_t vdownleft  = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width/2+diff, -diff), -act->angle) );
    v2d_t vdownright = v2d_add ( feet , v2d_rotate( v2d_new(frame_width/2-diff, -diff), -act->angle) );

    float cd_up[4] = { vup.x-sqrsize , vup.y-sqrsize , vup.x+sqrsize , vup.y+sqrsize };
    float cd_down[4] = { vdown.x-sqrsize , vdown.y-sqrsize , vdown.x+sqrsize , vdown.y+sqrsize };
    float cd_left[4] = { vleft.x-sqrsize , vleft.y-sqrsize , vleft.x+sqrsize , vleft.y+sqrsize };
    float cd_right[4] = { vright.x-sqrsize , vright.y-sqrsize , vright.x+sqrsize , vright.y+sqrsize };
    float cd_upleft[4] = { vupleft.x-sqrsize , vupleft.y-sqrsize , vupleft.x+sqrsize , vupleft.y+sqrsize };
    float cd_upright[4] = { vupright.x-sqrsize , vupright.y-sqrsize , vupright.x+sqrsize , vupright.y+sqrsize };
    float cd_downleft[4] = { vdownleft.x-sqrsize , vdownleft.y-sqrsize , vdownleft.x+sqrsize , vdownleft.y+sqrsize };
    float cd_downright[4] = { vdownright.x-sqrsize , vdownright.y-sqrsize , vdownright.x+sqrsize , vdownright.y+sqrsize };

    c[0] = image_rgb(255,255,255);
    c[1] = image_rgb(0,128,255);

    image_rectfill(video_get_backbuffer(), cd_up[0], cd_up[1], cd_up[2], cd_up[3], c[0]);
    image_rectfill(video_get_backbuffer(), cd_down[0], cd_down[1], cd_down[2], cd_down[3], c[0]);
    image_rectfill(video_get_backbuffer(), cd_left[0], cd_left[1], cd_left[2], cd_left[3], c[0]);
    image_rectfill(video_get_backbuffer(), cd_right[0], cd_right[1], cd_right[2], cd_right[3], c[0]);
    image_rectfill(video_get_backbuffer(), cd_downleft[0], cd_downleft[1], cd_downleft[2], cd_downleft[3], c[1]);
    image_rectfill(video_get_backbuffer(), cd_downright[0], cd_downright[1], cd_downright[2], cd_downright[3], c[1]);
    image_rectfill(video_get_backbuffer(), cd_upright[0], cd_upright[1], cd_upright[2], cd_upright[3], c[1]);
    image_rectfill(video_get_backbuffer(), cd_upleft[0], cd_upleft[1], cd_upleft[2], cd_upleft[3], c[1]);
}


/*
 * actor_collision()
 * Check collisions
 */
int actor_collision(const actor_t *a, const actor_t *b)
{
    int j, right = 0;
    v2d_t corner[2][4];
    corner[0][0] = v2d_subtract(a->position, v2d_rotate(a->hot_spot, -a->angle)); /* a's topleft */
    corner[0][1] = v2d_add( corner[0][0] , v2d_rotate(v2d_new(actor_image(a)->w, 0), -a->angle) ); /* a's topright */
    corner[0][2] = v2d_add( corner[0][0] , v2d_rotate(v2d_new(actor_image(a)->w, actor_image(a)->h), -a->angle) ); /* a's bottomright */
    corner[0][3] = v2d_add( corner[0][0] , v2d_rotate(v2d_new(0, actor_image(a)->h), -a->angle) ); /* a's bottomleft */
    corner[1][0] = v2d_subtract(b->position, v2d_rotate(b->hot_spot, -b->angle)); /* b's topleft */
    corner[1][1] = v2d_add( corner[1][0] , v2d_rotate(v2d_new(actor_image(b)->w, 0), -b->angle) ); /* b's topright */
    corner[1][2] = v2d_add( corner[1][0] , v2d_rotate(v2d_new(actor_image(b)->w, actor_image(b)->h), -b->angle) ); /* b's bottomright */
    corner[1][3] = v2d_add( corner[1][0] , v2d_rotate(v2d_new(0, actor_image(b)->h), -b->angle) ); /* b's bottomleft */
    right += fabs(a->angle)<EPSILON||fabs(a->angle-PI/2)<EPSILON||fabs(a->angle-PI)<EPSILON||fabs(a->angle-3*PI/2)<EPSILON;
    right += fabs(b->angle)<EPSILON||fabs(b->angle-PI/2)<EPSILON||fabs(b->angle-PI)<EPSILON||fabs(b->angle-3*PI/2)<EPSILON;

    if(right) {
        float r[2][4];
        for(j=0; j<2; j++) {
            r[j][0] = min(corner[j][0].x, corner[j][1].x);
            r[j][1] = min(corner[j][0].y, corner[j][1].y);
            r[j][2] = max(corner[j][2].x, corner[j][3].x);
            r[j][3] = max(corner[j][2].y, corner[j][3].y);
            if(r[j][0] > r[j][2]) swap(r[j][0], r[j][2]);
            if(r[j][1] > r[j][3]) swap(r[j][1], r[j][3]);
        }
        return bounding_box(r[0],r[1]);
    }
    else {
        v2d_t center[2];
        float radius[2] = { max(actor_image(a)->w,actor_image(a)->h) , max(actor_image(b)->w,actor_image(b)->h) };
        for(j=0; j<2; j++)
            center[j] = v2d_multiply(v2d_add(corner[j][0], corner[j][2]), 0.5);
        return circular_collision(center[0], radius[0], center[1], radius[1]);
    }
}


/*
 * actor_orientedbox_collision()
 * Is a colliding with b? (oriented bounding box detection)
 */
int actor_orientedbox_collision(const actor_t *a, const actor_t *b)
{
    v2d_t a_pos, b_pos;
    v2d_t a_size, b_size;
    v2d_t a_spot[4], b_spot[4]; /* rotated spots */

    calculate_rotated_boundingbox(a, a_spot);
    calculate_rotated_boundingbox(b, b_spot);

    a_pos.x = min(a_spot[0].x, min(a_spot[1].x, min(a_spot[2].x, a_spot[3].x)));
    a_pos.y = min(a_spot[0].y, min(a_spot[1].y, min(a_spot[2].y, a_spot[3].y)));
    b_pos.x = min(b_spot[0].x, min(b_spot[1].x, min(b_spot[2].x, b_spot[3].x)));
    b_pos.y = min(b_spot[0].y, min(b_spot[1].y, min(b_spot[2].y, b_spot[3].y)));

    a_size.x = max(a_spot[0].x, max(a_spot[1].x, max(a_spot[2].x, a_spot[3].x))) - a_pos.x;
    a_size.y = max(a_spot[0].y, max(a_spot[1].y, max(a_spot[2].y, a_spot[3].y))) - a_pos.y;
    b_size.x = max(b_spot[0].x, max(b_spot[1].x, max(b_spot[2].x, b_spot[3].x))) - b_pos.x;
    b_size.y = max(b_spot[0].y, max(b_spot[1].y, max(b_spot[2].y, b_spot[3].y))) - b_pos.y;

    if(a_pos.x + a_size.x >= b_pos.x && a_pos.x <= b_pos.x + b_size.x) {
        if(a_pos.y + a_size.y >= b_pos.y && a_pos.y <= b_pos.y + b_size.y)
            return TRUE;
    }

    return FALSE;
}


/*
 * actor_pixelperfect_collision()
 * Is a colliding with b? (pixel perfect detection)
 */
int actor_pixelperfect_collision(const actor_t *a, const actor_t *b)
{

    if(fabs(a->angle) < EPSILON && fabs(b->angle) < EPSILON) {
        if(actor_collision(a, b)) {
            int x1, y1, x2, y2;

            x1 = (int)(a->position.x - a->hot_spot.x);
            y1 = (int)(a->position.y - a->hot_spot.y);
            x2 = (int)(b->position.x - b->hot_spot.x);
            y2 = (int)(b->position.y - b->hot_spot.y);

            return image_pixelperfect_collision(actor_image(a), actor_image(b), x1, y1, x2, y2);
        }
        else
            return FALSE;
    }
    else {
        if(actor_orientedbox_collision(a, b)) {
            image_t *image_a, *image_b;
            v2d_t size_a, size_b, pos_a, pos_b;
            v2d_t a_spot[4], b_spot[4]; /* rotated spots */
            v2d_t ac, bc; /* rotation spot */
            int collided;

            calculate_rotated_boundingbox(a, a_spot);
            calculate_rotated_boundingbox(b, b_spot);

            pos_a.x = min(a_spot[0].x, min(a_spot[1].x, min(a_spot[2].x, a_spot[3].x)));
            pos_a.y = min(a_spot[0].y, min(a_spot[1].y, min(a_spot[2].y, a_spot[3].y)));
            pos_b.x = min(b_spot[0].x, min(b_spot[1].x, min(b_spot[2].x, b_spot[3].x)));
            pos_b.y = min(b_spot[0].y, min(b_spot[1].y, min(b_spot[2].y, b_spot[3].y)));

            size_a.x = max(a_spot[0].x, max(a_spot[1].x, max(a_spot[2].x, a_spot[3].x))) - pos_a.x;
            size_a.y = max(a_spot[0].y, max(a_spot[1].y, max(a_spot[2].y, a_spot[3].y))) - pos_a.y;
            size_b.x = max(b_spot[0].x, max(b_spot[1].x, max(b_spot[2].x, b_spot[3].x))) - pos_b.x;
            size_b.y = max(b_spot[0].y, max(b_spot[1].y, max(b_spot[2].y, b_spot[3].y))) - pos_b.y;

            ac = v2d_add(v2d_subtract(a_spot[0], pos_a), v2d_rotate(a->hot_spot, -a->angle));
            bc = v2d_add(v2d_subtract(b_spot[0], pos_b), v2d_rotate(b->hot_spot, -b->angle));

            image_a = image_create(size_a.x, size_a.y);
            image_b = image_create(size_b.x, size_b.y);
            image_clear(image_a, video_get_maskcolor());
            image_clear(image_b, video_get_maskcolor());

            image_draw_rotated(actor_image(a), image_a, ac.x, ac.y, (int)a->hot_spot.x, (int)a->hot_spot.y, a->angle, a->mirror);
            image_draw_rotated(actor_image(b), image_b, bc.x, bc.y, (int)b->hot_spot.x, (int)b->hot_spot.y, b->angle, b->mirror);

            collided = image_pixelperfect_collision(image_a, image_b, pos_a.x, pos_a.y, pos_b.x, pos_b.y);

            image_destroy(image_a);
            image_destroy(image_b);
            return collided;
        }
        else
            return FALSE;
    }
}


/*
 * actor_brick_collision()
 * Actor collided with a brick?
 */
int actor_brick_collision(actor_t *act, brick_t *brk)
{
    v2d_t topleft = v2d_subtract(act->position, v2d_rotate(act->hot_spot, act->angle));
    v2d_t bottomright = v2d_add( topleft , v2d_rotate(v2d_new(actor_image(act)->w, actor_image(act)->h), act->angle) );
    float a[4] = { topleft.x , topleft.y , bottomright.x , bottomright.y };
    float b[4] = { (float)brk->x , (float)brk->y , (float)(brk->x+brk->brick_ref->image->w) , (float)(brk->y+brk->brick_ref->image->h) };

    return bounding_box(a,b);
}



/*
 * actor_move()
 * Uses the orientation angle to move an actor
 */
void actor_move(actor_t *act, v2d_t delta_space)
{
    if(fabs(delta_space.x) < EPSILON) delta_space.x = 0;
    act->position.x += delta_space.x * cos(act->angle) + delta_space.y * sin(act->angle);
    act->position.y += delta_space.y * cos(act->angle) - delta_space.x * sin(act->angle);
}


/*
 * actor_change_animation()
 * Changes the animation of an actor
 */
void actor_change_animation(actor_t *act, animation_t *anim)
{
    if(act->animation != anim) {
        act->animation = anim;
        act->hot_spot = anim->hot_spot;
        act->animation_frame = 0;
        act->animation_speed_factor = 1.0;
    }
}



/*
 * actor_change_animation_frame()
 * Changes the animation frame
 */
void actor_change_animation_frame(actor_t *act, int frame)
{
    act->animation_frame = clip(frame, 0, act->animation->frame_count);
}



/*
 * actor_change_animation_speed_factor()
 * Changes the speed factor of the current animation
 * The default factor is 1.0 (i.e., 100% of the original
 * animation speed)
 */
void actor_change_animation_speed_factor(actor_t *act, float factor)
{
    act->animation_speed_factor = max(0.0, factor);
}



/*
 * actor_animation_finished()
 * Returns true if the current animation has finished
 */
int actor_animation_finished(actor_t *act)
{
    float frame = act->animation_frame + (act->animation->fps * act->animation_speed_factor) * timer_get_delta();
    return (!act->animation->repeat && (int)frame >= act->animation->frame_count);
}



/*
 * actor_image()
 * Returns the current image of the
 * animation of this actor
 */
image_t* actor_image(const actor_t *act)
{
    return sprite_get_image(act->animation, (int)act->animation_frame);
}






/*
 * actor_corners()
 * Get actor's corners
 *
 * Let X be the original collision detector spot. So:
 * diff = distance between the real collision detector spot and X
 * sqrsize = size of the collision detector
 */
void actor_corners(actor_t *act, float sqrsize, float diff, brick_list_t *brick_list, brick_t **up, brick_t **upright, brick_t **right, brick_t **downright, brick_t **down, brick_t **downleft, brick_t **left, brick_t **upleft)
{
    int frame_width = actor_image(act)->w;
    int frame_height = actor_image(act)->h;

    v2d_t feet      = act->position;
    v2d_t vup        = v2d_add ( feet , v2d_rotate( v2d_new(0, -frame_height+diff), -act->angle) );
    v2d_t vdown      = v2d_add ( feet , v2d_rotate( v2d_new(0, -diff), -act->angle) ); 
    v2d_t vleft      = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width/2+diff, -frame_height*SIDE_CORNERS_HEIGHT), -act->angle) );
    v2d_t vright     = v2d_add ( feet , v2d_rotate( v2d_new(frame_width/2-diff, -frame_height*SIDE_CORNERS_HEIGHT), -act->angle) );
    v2d_t vupleft    = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width/2+diff, -frame_height+diff), -act->angle) );
    v2d_t vupright   = v2d_add ( feet , v2d_rotate( v2d_new(frame_width/2-diff, -frame_height+diff), -act->angle) );
    v2d_t vdownleft  = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width/2+diff, -diff), -act->angle) );
    v2d_t vdownright = v2d_add ( feet , v2d_rotate( v2d_new(frame_width/2-diff, -diff), -act->angle) );

    actor_corners_ex(act, sqrsize, vup, vupright, vright, vdownright, vdown, vdownleft, vleft, vupleft, brick_list, up, upright, right, downright, down, downleft, left, upleft);
}




/*
 * actor_corners_ex()
 * Like actor_corners(), but this procedure allows to specify the
 * collision detectors positions'
 */
void actor_corners_ex(actor_t *act, float sqrsize, v2d_t vup, v2d_t vupright, v2d_t vright, v2d_t vdownright, v2d_t vdown, v2d_t vdownleft, v2d_t vleft, v2d_t vupleft, brick_list_t *brick_list, brick_t **up, brick_t **upright, brick_t **right, brick_t **downright, brick_t **down, brick_t **downleft, brick_t **left, brick_t **upleft)
{
    float cd_up[4] = { vup.x-sqrsize , vup.y-sqrsize , vup.x+sqrsize , vup.y+sqrsize };
    float cd_down[4] = { vdown.x-sqrsize , vdown.y-sqrsize , vdown.x+sqrsize , vdown.y+sqrsize };
    float cd_left[4] = { vleft.x-sqrsize , vleft.y-sqrsize , vleft.x+sqrsize , vleft.y+sqrsize };
    float cd_right[4] = { vright.x-sqrsize , vright.y-sqrsize , vright.x+sqrsize , vright.y+sqrsize };
    float cd_upleft[4] = { vupleft.x-sqrsize , vupleft.y-sqrsize , vupleft.x+sqrsize , vupleft.y+sqrsize };
    float cd_upright[4] = { vupright.x-sqrsize , vupright.y-sqrsize , vupright.x+sqrsize , vupright.y+sqrsize };
    float cd_downleft[4] = { vdownleft.x-sqrsize , vdownleft.y-sqrsize , vdownleft.x+sqrsize , vdownleft.y+sqrsize };
    float cd_downright[4] = { vdownright.x-sqrsize , vdownright.y-sqrsize , vdownright.x+sqrsize , vdownright.y+sqrsize };

    if(up) *up = brick_at(brick_list, cd_up);
    if(down) *down = brick_at(brick_list, cd_down);
    if(left) *left = brick_at(brick_list, cd_left);
    if(right) *right = brick_at(brick_list, cd_right);
    if(upleft) *upleft = brick_at(brick_list, cd_upleft);
    if(upright) *upright = brick_at(brick_list, cd_upright);
    if(downleft) *downleft = brick_at(brick_list, cd_downleft);
    if(downright) *downright = brick_at(brick_list, cd_downright);
}


/*
 * actor_corners_set_floor_priority()
 * Which one has the greatest priority: floor (TRUE) or wall (FALSE) ?
 * Collision-detection routine. See also: brick_at()
 */
void actor_corners_set_floor_priority(int floor)
{
    floor_priority = floor;
}


/*
 * actor_corners_restore_floor_priority()
 * Shortcut to actor_corners_set_floor_priority(TRUE);
 * TRUE is the default value.
 */
void actor_corners_restore_floor_priority()
{
    actor_corners_set_floor_priority(TRUE);
}

/*
 * actor_corners_set_slope_priority()
 * Which one has the greatest priority: slope (TRUE) or floor (FALSE) ?
 * Collision-detection routine. See also: brick_at()
 */
void actor_corners_set_slope_priority(int slope)
{
    slope_priority = slope;
}


/*
 * actor_corners_restore_slope_priority()
 * Shortcut to actor_corners_set_slope_priority(TRUE);
 * TRUE is the default value.
 */
void actor_corners_restore_slope_priority()
{
    actor_corners_set_slope_priority(TRUE);
}


/*
 * actor_corners_disable_detection()
 * Disables the collision detection of a few bricks
 */
void actor_corners_disable_detection(int disable_leftwall, int disable_rightwall, int disable_floor, int disable_ceiling)
{
    is_leftwall_disabled = disable_leftwall;
    is_rightwall_disabled = disable_rightwall;
    is_floor_disabled = disable_floor;
    is_ceiling_disabled = disable_ceiling;
}


/*
 * actor_platform_movement()
 * Basic platform movement. Returns
 * a delta_space vector.
 *
 * Note: the actor's hot spot must
 * be defined on its feet.
 */
v2d_t actor_platform_movement(actor_t *act, brick_list_t *brick_list, float gravity)
{
    v2d_t ds = v2d_new(0,0); /* return value */
    float dt = timer_get_delta(); /* delta_time */
    float natural_angle = 0; /* default angle (gravity) */
    float max_y_speed = 480, friction = 0, gravity_factor = 1.0; /* generic modifiers */
    float diff = MAGIC_DIFF; /* magic hack */
    v2d_t feet = act->position; /* actor's feet */
    v2d_t up, upright, right, downright, down, downleft, left, upleft; /* collision detectors (CDs) */
    brick_t *brick_up, *brick_upright, *brick_right, *brick_downright, *brick_down, *brick_downleft, *brick_left, *brick_upleft; /* bricks detected by the CDs */

    /* actor's collision detectors */
    actor_get_collision_detectors(act, diff, &up, &upright, &right, &downright, &down, &downleft, &left, &upleft);
    actor_handle_collision_detectors(act, brick_list, up, upright, right, downright, down, downleft, left, upleft, &brick_up, &brick_upright, &brick_right, &brick_downright, &brick_down, &brick_downleft, &brick_left, &brick_upleft);

    /* clouds */
    actor_handle_clouds(act, diff, &brick_up, &brick_upright, &brick_right, &brick_downright, &brick_down, &brick_downleft, &brick_left, &brick_upleft);

    /* carrying characters */
    actor_handle_carrying(act, brick_down);
    if(act->carried_by != NULL)
        return v2d_new(0,0);

    /* wall collisions */
    actor_handle_wall_collision(act, feet, left, right, brick_left, brick_right);

    /* y-axis logic */
    if(brick_down) {
        act->is_jumping = FALSE;
        act->ignore_horizontal = FALSE;
        actor_handle_jumping(act, diff, natural_angle, brick_down, brick_up);
        actor_handle_slopes(act, brick_down);
    }
    else
        act->angle = natural_angle;

    /* y-axis movement */
    ds.y = (fabs(act->speed.y) > EPSILON) ? act->speed.y*dt + 0.5*(gravity*gravity_factor)*(dt*dt) : 0;
    act->speed.y = min(act->speed.y + (gravity*gravity_factor)*dt, max_y_speed);

    /* ceiling collision */
    actor_handle_ceil_collision(act, feet, up, brick_up);

    /* floor collision */
    actor_handle_floor_collision(act, diff, natural_angle, &ds, &feet, &friction, brick_downleft, brick_down, brick_downright);

    /* x-axis movement */
    ds.x = (fabs(act->speed.x) > EPSILON) ? act->speed.x*dt + 0.5*((1.0-friction)*act->acceleration)*(dt*dt) : 0;
    actor_handle_acceleration(act, friction, brick_down);

    /* final adjustments... */
    if(fabs(act->speed.x) < EPSILON) act->speed.x = ds.x = 0;
    ds.x += level_brick_move_actor(brick_down,act).x*dt;
    return ds;
}



/*
 * actor_particle_movement()
 *
 * Basic particle movement. Returns a
 * delta_space vector.
 */
v2d_t actor_particle_movement(actor_t *act, float gravity)
{
    float dt = timer_get_delta();
    v2d_t ds = v2d_new(0,0);

    /* x-axis */
    ds.x = act->speed.x*dt;

    /* y-axis */
    ds.y = act->speed.y*dt + 0.5*gravity*dt*dt;
    act->speed.y += gravity*dt;

    /* done! */
    return ds;
}




/*
 * actor_eightdirections_movement()
 *
 * Basic eight directions movement with acceleration.
 * Returns a delta_space vector.
 */
v2d_t actor_eightdirections_movement(actor_t *act)
{
    float dt = timer_get_delta();
    v2d_t ds = v2d_new(0,0);

    /* input device */
    if(act->input) {
        /* x-speed */
        if(input_button_down(act->input, IB_RIGHT) && !input_button_down(act->input, IB_LEFT))
            act->speed.x = min(act->speed.x + act->acceleration*dt, act->maxspeed);
        if(input_button_down(act->input, IB_LEFT) && !input_button_down(act->input, IB_RIGHT))
            act->speed.x = max(act->speed.x - act->acceleration*dt, -act->maxspeed);
        if(!input_button_down(act->input, IB_LEFT) && !input_button_down(act->input, IB_RIGHT) && fabs(act->speed.x) > EPSILON) {
            if(act->speed.x > 0)
                act->speed.x = max(act->speed.x - act->acceleration*dt, 0);
            else
                act->speed.x = min(act->speed.x + act->acceleration*dt, 0);
        }

        /* y-speed */
        if(input_button_down(act->input, IB_DOWN) && !input_button_down(act->input, IB_UP))
            act->speed.y = min(act->speed.y + act->acceleration*dt, act->maxspeed);
        if(input_button_down(act->input, IB_UP) && !input_button_down(act->input, IB_DOWN))
            act->speed.y = max(act->speed.y - act->acceleration*dt, -act->maxspeed);
        if(!input_button_down(act->input, IB_UP) && !input_button_down(act->input, IB_DOWN) && fabs(act->speed.y) > EPSILON) {
            if(act->speed.y > 0)
                act->speed.y = max(act->speed.y - act->acceleration*dt, 0);
            else
                act->speed.y = min(act->speed.y + act->acceleration*dt, 0);
        }
    }
    else
        act->speed = v2d_new(0,0);

    /* done! */
    ds.x = fabs(act->speed.x) > EPSILON ? act->speed.x*dt + 0.5*act->acceleration*dt*dt : 0;
    ds.y = fabs(act->speed.y) > EPSILON ? act->speed.y*dt + 0.5*act->acceleration*dt*dt : 0;
    return ds;
}


/*
 * actor_bullet_movement()
 *
 * Basic bullet movement. Returns a
 * delta_space vector.
 *
 * Bullet movement is a horizontal movement without any gravity effect.  If
 * a procedure uses this movement it is responsible for destroying the bullet
 * upon collision.
 */
v2d_t actor_bullet_movement(actor_t *act)
{
    float dt = timer_get_delta();
    v2d_t ds = v2d_new(0,0);

    /* x-axis */
    ds.x = act->speed.x*dt;

    /* y-axis */
    ds.y = 0.0;
    
    /* done! */
    return ds;
}








/* platform movement: auxiliary routines */


/*
 * actor_handle_clouds()
 * Cloud programming (called inside platform
 * movement methods). Clouds are also known
 * as 'jump-through' platforms.
 */
void actor_handle_clouds(actor_t *act, float diff, brick_t **up, brick_t **upright, brick_t **right, brick_t **downright, brick_t **down, brick_t **downleft, brick_t **left, brick_t **upleft)
{
    int i;
    brick_t **cloud_off[5] = { up, upright, right, left, upleft };

    /* bricks: laterals and top */
    for(i=0; i<5; i++) {
        /* forget bricks */
        brick_t **brk = cloud_off[i];
        if(brk && *brk && (*brk)->brick_ref && (*brk)->brick_ref->property == BRK_CLOUD)
            *brk = NULL;
    }

    /* bricks: down, downleft, downright */
    if(down && *down && (*down)->brick_ref->property == BRK_CLOUD) {
        float offset = min(15, (*down)->brick_ref->image->h/3);
        if(!(act->speed.y >= 0 && act->position.y < ((*down)->y+diff+1)+offset)) {
            /* forget bricks */
            if(downleft && *downleft == *down)
                *downleft = NULL;
            if(downright && *downright == *down)
                *downright = NULL;
            *down = NULL;
        }
    }
}

/*
 * actor_get_collision_detectors()
 * Gets the collision detectors of this actor
 */
void actor_get_collision_detectors(actor_t *act, float diff, v2d_t *up, v2d_t *upright, v2d_t *right, v2d_t *downright, v2d_t *down, v2d_t *downleft, v2d_t *left, v2d_t *upleft)
{
    const int frame_width = actor_image(act)->w, frame_height = actor_image(act)->h;
    const int slope = !((fabs(act->angle)<EPSILON)||(fabs(act->angle-PI/2)<EPSILON)||(fabs(act->angle-PI)<EPSILON)||(fabs(act->angle-3*PI/2)<EPSILON));
    const v2d_t feet = act->position;
    float top, middle, lateral;

    /* slope hack */
    if(!slope) { top = 0.7; middle = 0.5; lateral = 0.25; }
    else       { top = 1.0; middle = 0.7; lateral = 0.25; }

    /* calculating the collision detectors */
    *up        = v2d_add ( feet , v2d_rotate( v2d_new(0, -frame_height*top+diff), -act->angle) );
    *down      = v2d_add ( feet , v2d_rotate( v2d_new(0, -diff), -act->angle) ); 
    *left      = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -frame_height*middle), -act->angle) );
    *right     = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -frame_height*middle), -act->angle) );
    *upleft    = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -frame_height*top+diff), -act->angle) );
    *upright   = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -frame_height*top+diff), -act->angle) );
    *downleft  = v2d_add ( feet , v2d_rotate( v2d_new(-frame_width*lateral+diff, -diff), -act->angle) );
    *downright = v2d_add ( feet , v2d_rotate( v2d_new(frame_width*lateral-diff, -diff), -act->angle) );
}


/*
 * actor_handle_collision_detectors()
 * Uses the collision detectors to retrieve *brick_up, ..., *brick_upleft
 */
void actor_handle_collision_detectors(actor_t *act, brick_list_t *brick_list, v2d_t up, v2d_t upright, v2d_t right, v2d_t downright, v2d_t down, v2d_t downleft, v2d_t left, v2d_t upleft, brick_t **brick_up, brick_t **brick_upright, brick_t **brick_right, brick_t **brick_downright, brick_t **brick_down, brick_t **brick_downleft, brick_t **brick_left, brick_t **brick_upleft)
{
    const float sqrsize = 2;

    actor_corners_ex(act, sqrsize, up, upright, right, downright, down, downleft, left, upleft, brick_list, brick_up, brick_upright, brick_right, brick_downright, brick_down, brick_downleft, brick_left, brick_upleft);
}

/*
 * actor_handle_carrying()
 * If act is being carried, runs the corresponding logic
 */
void actor_handle_carrying(actor_t *act, brick_t *brick_down)
{
    float dt = timer_get_delta();

    /* I'm being carried */
    if(act->carried_by != NULL) {
        actor_t *car = act->carried_by;

        /* what should I do? */
        if((brick_down && brick_down->brick_ref->angle == 0 && (int)car->speed.y >= 5)) {
            /* put me down! */
            act->position = act->carried_by->position;
            act->carried_by->carrying = NULL;
            act->carried_by = NULL;
        }
        else {
            /* carry me! */
            act->speed = v2d_new(0,0);
            act->mirror = car->mirror;
            act->angle = 0;
            act->position = v2d_subtract(v2d_add(car->position, v2d_multiply(car->speed,dt)), act->carry_offset);
        }
    }
}

/*
 * actor_handle_wall_collision()
 * Handles wall collision
 */
void actor_handle_wall_collision(actor_t *act, v2d_t feet, v2d_t left, v2d_t right, brick_t *brick_left, brick_t *brick_right)
{
    /* right wall */
    if(brick_right) {
        if(brick_right->brick_ref->angle % 90 == 0 && (act->speed.x > EPSILON || right.x > brick_right->x)) {
            act->speed.x = 0;
            act->position.x = brick_right->x + (feet.x-right.x);
        }
    }

    /* left wall */
    if(brick_left) {
        if(brick_left->brick_ref->angle % 90 == 0 && (act->speed.x < -EPSILON || left.x < brick_left->x+brick_left->brick_ref->image->w)) {
            act->speed.x = 0;
            act->position.x = (brick_left->x+brick_left->brick_ref->image->w) + (feet.x-left.x);
        }
    }
}


/*
 * actor_handle_ceil_collision()
 * Handles ceiling collision
 */
void actor_handle_ceil_collision(actor_t *act, v2d_t feet, v2d_t up, brick_t *brick_up)
{
    if(brick_up && brick_up->brick_ref->angle % 90 == 0 && act->speed.y < -EPSILON) {
        act->position.y = (brick_up->y+brick_up->brick_ref->image->h) + (feet.y-up.y);
        act->speed.y = 10;
    }
}



/*
 * actor_handle_jumping()
 * Handles the jumping logic
 */
void actor_handle_jumping(actor_t *act, float diff, float natural_angle, brick_t *brick_down, brick_t *brick_up)
{
    int ang = brick_down->brick_ref->angle;

    if(input_button_down(act->input, IB_FIRE1) && !input_button_down(act->input, IB_DOWN) && !brick_up) {
        act->angle = natural_angle;
        act->is_jumping = TRUE;
        if(ang == 0) {
            act->speed.y = -act->jump_strength;
        }
        else if(ang > 0 && ang < 90) {
            act->speed.x = min(act->speed.x, -0.7*act->jump_strength);
            act->speed.y = -0.7*act->jump_strength;
        }
        else if(ang == 90) {
            actor_move(act, v2d_new(20*diff, 0));
            act->speed.x = min(act->speed.x, -act->jump_strength);
            act->speed.y = -act->jump_strength/2;
        }
        else if(ang > 90 && ang < 180) {
            actor_move(act, v2d_new(0, -20*diff));
            act->speed.x = min(act->speed.x, -0.7*act->jump_strength);
            act->speed.y = act->jump_strength;
        }
        else if(ang == 180) {
            actor_move(act, v2d_new(0, -20*diff));
            act->speed.x *= -1;
            act->speed.y = act->jump_strength;
        }
        else if(ang > 180 && ang < 270) {
            actor_move(act, v2d_new(0, -20*diff));
            act->speed.x = max(act->speed.x, 0.7*act->jump_strength);
            act->speed.y = act->jump_strength;
        }
        else if(ang == 270) {
            actor_move(act, v2d_new(-20*diff, 0));
            act->speed.x = max(act->speed.x, act->jump_strength);
            act->speed.y = -act->jump_strength/2;
        }
        else if(ang > 270 && ang < 360) {
            act->speed.x = max(act->speed.x, 0.7*act->jump_strength);
            act->speed.y = -0.7*act->jump_strength;
        }
    }
}


/*
 * actor_handle_slopes()
 * slopes / speed issues
 */
void actor_handle_slopes(actor_t *act, brick_t *brick_down)
{
    int ang = brick_down->brick_ref->angle;
    float dt = timer_get_delta();
    float mytan, factor;

    if(!act->is_jumping) {
        if(ang > 0 && ang < 90) {
            mytan = min(1, tan( ang*PI/180.0 ));
            if(fabs(act->speed.y) > EPSILON)
                act->speed.x = -3*mytan*act->speed.y;
            else {
                factor = (!(act->mirror & IF_HFLIP) ? 0.8 : 2.0) * mytan;
                act->speed.x = max(act->speed.x - factor*act->acceleration*dt, -act->maxspeed);
            }
        }
        else if(ang > 270 && ang < 360) {
            mytan = min(1, -tan( ang*PI/180.0 ));
            if(fabs(act->speed.y) > EPSILON)
                act->speed.x = 3*mytan*act->speed.y;
            else {
                factor = ((act->mirror & IF_HFLIP) ? 0.8 : 2.0) * mytan;
                act->speed.x = min(act->speed.x + factor*act->acceleration*dt, act->maxspeed);
            }
        }
    }
}


/*
 * actor_handle_acceleration()
 * Handles x-axis acceleration
 */
void actor_handle_acceleration(actor_t *act, float friction, brick_t *brick_down)
{
    float dt = timer_get_delta();

    if(input_button_down(act->input, IB_LEFT) && !input_button_down(act->input, IB_RIGHT)) {
        if(!act->ignore_horizontal && !input_button_down(act->input, IB_DOWN)) {
            act->speed.x = max(act->speed.x - (1.0-friction)*act->acceleration*dt, -act->maxspeed);
            act->mirror = IF_HFLIP;
        }
    }
    else if(input_button_down(act->input, IB_RIGHT) && !input_button_down(act->input, IB_LEFT)) {
        if(!act->ignore_horizontal && !input_button_down(act->input, IB_DOWN)) {
            act->speed.x = min(act->speed.x + (1.0-friction)*act->acceleration*dt, act->maxspeed);
            act->mirror = IF_NONE;
        }
    }
    else if(brick_down){
        int signal = 0;
        int ang = brick_down->brick_ref->angle;
        float factor;
        
        /* deceleration factor */
        factor = 1.0;

        /* deceleration */
        if(ang % 90 == 0) {
            if( (ang==90 && (act->mirror&IF_HFLIP) && act->speed.x < 0) || ((ang==270) && !(act->mirror&IF_HFLIP) && act->speed.x > 0) ) {
                if(act->speed.x > EPSILON) signal = 1;
                else if(-act->speed.x > EPSILON) signal = -1;
                else signal = 0;
            }
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


/*
 * actor_handle_floor_collision()
 * Floor collision
 */
void actor_handle_floor_collision(actor_t *act, float diff, float natural_angle, v2d_t *ds, v2d_t *feet, float *friction, brick_t *brick_downleft, brick_t *brick_down, brick_t *brick_downright)
{
    float dt = timer_get_delta();
    int ang;

    if(brick_down && !act->is_jumping) {
        ang = brick_down->brick_ref->angle;
        act->speed.y = ds->y = 0;
        act->angle = ang * PI / 180.0;

        /* 0 floor */
        if(ang == 0) {
            v2d_t mov = level_brick_move_actor(brick_down, act); /* moveable platforms I */
            feet->y = brick_down->y;
            *friction = 0;
            if(mov.y > EPSILON) /* if the moveable brick is going down... */
                ds->y += mov.y*dt;
            else
                act->position.y = feet->y+diff;
        }

        /* (0-90) slope */
        else if(ang > 0 && ang < 90) {
            feet->y = brick_down->y + brick_down->brick_ref->image->h - (act->position.x-brick_down->x)*tan(act->angle);
            act->position.y = feet->y+diff;
            if(!(act->mirror & IF_HFLIP))
                *friction = 0.2;
        }

        /* 90 wall */
        else if(ang == 90) {
            if(fabs(act->speed.x) > 5) {
                int myang = brick_downright ? brick_downright->brick_ref->angle : -1;
                if(brick_downright && (myang >= ang && myang < ang+90)) {
                    feet->y = brick_down->x;
                    act->position.x = feet->y+diff;
                }
                else {
                    act->angle = natural_angle;
                    actor_move(act, v2d_new(6.5*diff, 0));
                    act->is_jumping = TRUE;
                    act->speed = v2d_new(0, -0.7*fabs(act->speed.x));
                }
            }
            else {
                act->angle = natural_angle;
                actor_move(act, v2d_new(5*diff, 0));
                act->is_jumping = TRUE;
                act->ignore_horizontal = FALSE;
            }
            if(!(act->mirror & IF_HFLIP))
                *friction = 1.5;
        }

        /* (90-180) slope */
        else if(ang > 90 && ang < 180) {
            if(fabs(act->speed.x) > 5) {
                feet->y = brick_down->y - (act->position.x-brick_down->x)*tan(act->angle);
                act->position.y = feet->y-diff;
            }
            else {
                act->angle = natural_angle;
                actor_move(act, v2d_new(0, -15*diff));
                act->is_jumping = TRUE;
            }
            *friction = 1.5;
        }

        /* 180 ceil */
        else if(ang == 180) {
            if( (act->speed.x > 5 && !(act->mirror & IF_HFLIP)) || (act->speed.x < -5 && act->mirror & IF_HFLIP) ) {
                feet->y = brick_down->y + brick_down->brick_ref->image->h;
                act->position.y = feet->y-diff;
            }
            else {
                act->angle = natural_angle;
                actor_move(act, v2d_new(0, -20*diff));
                act->is_jumping = TRUE;
                act->speed.x = 0;
            }
            *friction = 1.2;
        }

        /* (180-270) slope */
        else if(ang > 180 && ang < 270) {
            if(fabs(act->speed.x) > 5) {
                feet->y = brick_down->y + brick_down->brick_ref->image->h - (act->position.x-brick_down->x)*tan(act->angle);
                act->position.y = feet->y-diff;
            }
            else {
                act->angle = natural_angle;
                actor_move(act, v2d_new(0, -15*diff));
                act->is_jumping = TRUE;
            }
            *friction = 1.5;
        }

        /* 270 wall */
        else if(ang == 270) {
            if(fabs(act->speed.x) > 5) {
                int myang = brick_downleft ? brick_downleft->brick_ref->angle : -1;
                if(brick_downleft && (myang > ang-90 && myang <= ang)) {
                    feet->y = brick_down->x + brick_down->brick_ref->image->w;
                    act->position.x = feet->y-diff;
                }
                else {
                    act->angle = natural_angle;
                    actor_move(act, v2d_new(-6.5*diff, 0));
                    act->is_jumping = TRUE;
                    act->speed = v2d_new(0, -0.7*fabs(act->speed.x));            
                }

            }
            else {
                act->angle = natural_angle;
                actor_move(act, v2d_new(-5*diff, 0));
                act->is_jumping = TRUE;
                act->ignore_horizontal = FALSE;
            }
            if(act->mirror & IF_HFLIP)
                *friction = 1.5;
        }

        /* (270-360) slope */
        else if(ang > 270 && ang < 360) {
            feet->y = brick_down->y - (act->position.x-brick_down->x)*tan(act->angle);
            act->position.y = feet->y+diff;
            if(act->mirror & IF_HFLIP)
                *friction = 0.2;
        }
    }
}


/* private stuff */

/* brick_at(): given a list of bricks, returns
 * one that collides with the rectangle 'rect'
 * PS: this code ignores the bricks that are
 * not obstacles */
static brick_t* brick_at(brick_list_t *list, float rect[4])
{
    brick_t *ret = NULL;
    brick_list_t *p;
    int deg, inside_region, end = FALSE;
    float x, y, mytan, line;
    float br[4];

    /* main algorithm */
    for(p=list; p && !end; p=p->next) {

        /* I don't care about passable/disabled bricks. */
        if(p->data->brick_ref->property == BRK_NONE || !p->data->enabled)
            continue;

        /* I don't like clouds. */
        if(p->data->brick_ref->property == BRK_CLOUD && (ret && ret->brick_ref->property == BRK_OBSTACLE))
            continue;

        /* I don't like moving platforms */
        if(p->data->brick_ref->behavior == BRB_CIRCULAR && (ret && ret->brick_ref->behavior != BRB_CIRCULAR) && p->data->y >= ret->y)
            continue;

        /* I don't want a floor! */
        if(is_floor_disabled && p->data->brick_ref->angle == 0)
            continue;

        /* I don't want a ceiling! */
        if(is_ceiling_disabled && p->data->brick_ref->angle == 180)
            continue;

        /* I don't want a right wall */
        if(is_rightwall_disabled && p->data->brick_ref->angle > 0 && p->data->brick_ref->angle < 180)
            continue;

        /* I don't want a left wall */
        if(is_leftwall_disabled && p->data->brick_ref->angle > 180 && p->data->brick_ref->angle < 360)
            continue;

        /* here's something I like... */
        br[0] = (float)p->data->x;
        br[1] = (float)p->data->y;
        br[2] = (float)(p->data->x + p->data->brick_ref->image->w);
        br[3] = (float)(p->data->y + p->data->brick_ref->image->h);

        if(bounding_box(rect, br)) {
            if(p->data->brick_ref->behavior != BRB_CIRCULAR && (ret && ret->brick_ref->behavior == BRB_CIRCULAR) && p->data->y <= ret->y) {
                ret = p->data; /* I don't like moving platforms. Let's grab a regular platform instead. */
            }
            else if(p->data->brick_ref->property == BRK_OBSTACLE && (ret && ret->brick_ref->property == BRK_CLOUD)) {
                ret = p->data; /* I don't like clouds. Let's grab an obstacle instead. */
            }
            else if(p->data->brick_ref->property == BRK_CLOUD && (ret && ret->brick_ref->property == BRK_CLOUD)) {
                /* oh no, two conflicting clouds! */
                if(p->data->y > ret->y)
                    ret = p->data;
            }
            else if(p->data->brick_ref->angle % 90 == 0) { /* if not slope */


                if(slope_priority) {
                    if(!ret) /* this code priorizes the slopes */
                        ret = p->data;
                    else {
                        if(floor_priority) {
                            if(ret->brick_ref->angle % 180 != 0) /* priorizes the floor/ceil */
                                ret = p->data;
                        }
                        else {
                            if(ret->brick_ref->angle % 180 == 0) /* priorizes the walls (not floor/ceil) */
                                ret = p->data;
                        }
                    }
                }
                else
                    ret = p->data; /* priorizes the floors & walls */


            }
            else if(slope_priority) { /* if slope */
                deg = p->data->brick_ref->angle;
                mytan = tan(deg * PI/180.0);
                for(x=rect[0]; x<=rect[2] && !end; x++) {
                    for(y=rect[1]; y<=rect[3] && !end; y++) {
                        inside_region = FALSE;

                        switch( (int)(deg/90) % 4 ) {
                            case 0: /* 1st quadrant */
                                line = br[3] + mytan*(br[0]-x);
                                inside_region = (br[0] <= x && x <= br[2] && line <= y && y <= br[3]);
                                break;
                            case 1: /* 2nd quadrant */
                                line = br[3] - mytan*(br[2]-x);
                                inside_region = (br[0] <= x && x <= br[2] && br[1] <= y && y <= line);
                                break;
                            case 2: /* 3rd quadrant */
                                line = br[3] - mytan*(br[0]-x);
                                inside_region = (br[0] <= x && x <= br[2] && br[1] <= y && y <= line);
                                break;
                            case 3: /* 4th quadrant */
                                line = br[3] + mytan*(br[2]-x);
                                inside_region = (br[0] <= x && x <= br[2] && line <= y && y <= br[3]);
                                break;
                        }

                        if(inside_region) {
                            ret = p->data;
                            end = TRUE;
                        }
                    }
                }
            }
        }
    }

    return ret;
}


/*
 * calculate_rotated_boundingbox()
 * Calculates the rotated bounding box of a given actor
 */
void calculate_rotated_boundingbox(const actor_t *act, v2d_t spot[4])
{
    float w, h, angle;
    v2d_t a, b, c, d, hs;
    v2d_t pos;

    angle = -act->angle;
    w = actor_image(act)->w;
    h = actor_image(act)->h;
    hs = act->hot_spot;
    pos = act->position;

    a = v2d_subtract(v2d_new(0, 0), hs);
    b = v2d_subtract(v2d_new(w, 0), hs);
    c = v2d_subtract(v2d_new(w, h), hs);
    d = v2d_subtract(v2d_new(0, h), hs);

    spot[0] = v2d_add(pos, v2d_rotate(a, angle));
    spot[1] = v2d_add(pos, v2d_rotate(b, angle));
    spot[2] = v2d_add(pos, v2d_rotate(c, angle));
    spot[3] = v2d_add(pos, v2d_rotate(d, angle));
}

