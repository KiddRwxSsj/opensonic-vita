/*
 * gameover.c - "game over" scene
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

#include "gameover.h"
#include "quest.h"
#include "../core/scene.h"
#include "../core/util.h"
#include "../core/video.h"
#include "../core/timer.h"
#include "../entities/font.h"


/* private data */
static font_t *gameover_fnt[2];
static image_t *gameover_buf;
static float gameover_timer;


/*
 * gameover_init()
 * Initializes the game over screen
 */
void gameover_init()
{
    gameover_timer = 0;

    gameover_fnt[0] = font_create(7);
    gameover_fnt[0]->position = v2d_new(-50, 112);
    font_set_text(gameover_fnt[0], "GAME");

    gameover_fnt[1] = font_create(7);
    gameover_fnt[1]->position = v2d_new(298, 112);
    font_set_text(gameover_fnt[1], "OVER");

    gameover_buf = image_create(video_get_backbuffer()->w, video_get_backbuffer()->h);
    image_blit(video_get_backbuffer(), gameover_buf, 0, 0, 0, 0, gameover_buf->w, gameover_buf->h);
}



/*
 * gameover_update()
 * Updates the game over screen
 */
void gameover_update()
{
    float dt = timer_get_delta();

    /* timer */
    gameover_timer += dt;
    if(gameover_timer > 5) {
        if(fadefx_over()) {
            quest_abort();
            scenestack_pop();
            return;
        }
        fadefx_out(image_rgb(0,0,0), 2);
    }

    /* "game over" text */
    gameover_fnt[0]->position.x += 200*dt;
    if(gameover_fnt[0]->position.x > 80)
        gameover_fnt[0]->position.x = 80;

    gameover_fnt[1]->position.x -= 200*dt;
    if(gameover_fnt[1]->position.x < 168)
        gameover_fnt[1]->position.x = 168;
}



/*
 * gameover_render()
 * Renders the game over screen
 */
void gameover_render()
{
    v2d_t v = v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2);

    image_blit(gameover_buf, video_get_backbuffer(), 0, 0, 0, 0, gameover_buf->w, gameover_buf->h);
    font_render(gameover_fnt[0], v);
    font_render(gameover_fnt[1], v);
}



/*
 * gameover_release()
 * Releases the game over screen
 */
void gameover_release()
{
    image_destroy(gameover_buf);
    font_destroy(gameover_fnt[1]);
    font_destroy(gameover_fnt[0]);
    quest_abort();
}
