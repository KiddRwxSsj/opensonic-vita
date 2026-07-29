/*
 * intro.c - introduction scene
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

#include "intro.h"
#include "../core/v2d.h"
#include "../core/timer.h"
#include "../core/scene.h"
#include "../core/storyboard.h"
#include "../core/video.h"
#include "../entities/background.h"


/* private data */
#define INTRO_BGFILE        "themes/intro.bg"
#define INTRO_TIMEOUT       4.0f
static float elapsed_time;
static bgtheme_t *bgtheme;


/* public functions */

/*
 * intro_init()
 * Initializes the introduction scene
 */
void intro_init()
{
    elapsed_time = 0.0f;
    bgtheme = background_load(INTRO_BGFILE);
    fadefx_in(image_rgb(0,0,0), 1.0f);
}


/*
 * intro_release()
 * Releases the introduction scene
 */
void intro_release()
{
    bgtheme = background_unload(bgtheme);
}


/*
 * intro_update()
 * Updates the introduction scene
 */
void intro_update()
{
    elapsed_time += timer_get_delta();
    background_update(bgtheme);

    if(elapsed_time >= INTRO_TIMEOUT) {
        if(fadefx_over()) {
            scenestack_pop();
            scenestack_push(storyboard_get_scene(SCENE_MENU));
            return;
        }
        fadefx_out(image_rgb(0,0,0), 1.0f);
    }
}



/*
 * intro_render()
 * Renders the introduction scene
 */
void intro_render()
{
    v2d_t camera = v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2);

    background_render_bg(bgtheme, camera);
    background_render_fg(bgtheme, camera);
}
