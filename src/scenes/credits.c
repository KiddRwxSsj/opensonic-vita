/*
 * credits.c - credits scene
 * Copyright (C) 2009-2010  Alexandre Martins <alemartf(at)gmail(dot)com>
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
#include <string.h>
#include "credits.h"
#include "options.h"
#include "../core/util.h"
#include "../core/video.h"
#include "../core/audio.h"
#include "../core/lang.h"
#include "../core/input.h"
#include "../core/timer.h"
#include "../core/scene.h"
#include "../core/soundfactory.h"
#include "../entities/font.h"
#include "../entities/actor.h"
#include "../entities/background.h"


/* credits text */
static char credits_text[] =
    "\n<color=ffff00>$CREDITS_ENGINE</color>\n\n"

    "\n<color=ffff00>$CREDITS_ACTIVE</color>\n\n"
    "Alexandre Martins:\n$CREDITS_ALEXANDRE\n\n"
    "Di Rodrigues:\n$CREDITS_DI\n\n"
    "Colin:\n$CREDITS_COLIN\n\n"
    "Mateus Reis:\n$CREDITS_MATEUSREIS\n\n"
    "Christopher Martinus:\n$CREDITS_CHRISTOPHER\n\n"
    "Celdecea:\n$CREDITS_CELDECEA\n\n"
    "Christian Zigotzky:\n$CREDITS_XENO\n\n"
    "Joepotato28:\n$CREDITS_JOE\n\n"
    "Arthur Blot:\n$CREDITS_ARTHURBLOT\n\n"
    "Reimund Renner:\n$CREDITS_REIMUND\n\n"
    "Szymon Weihs:\n$CREDITS_SZYMON\n\n"
    "Tomires:\n$CREDITS_TOMIRES\n\n"
    "Sascha de waal:\n$CREDITS_SSDW\n\n"
    "Francesco Sciusco:\n$CREDITS_FRANCESCO\n\n"

    "\n<color=ffff00>$CREDITS_THANKS</color>\n\n"
    "SourceForge.net\n"
    "allegro.cc\n"
    "OpenGameArt.org\n"
    "GagaGames.com.br\n"
    "Rsonist88\n"
    "PlayDeb.net\n\n"

    "\n<color=ffff00>$CREDITS_RETIRED</color>\n\n"
    "Neoblast:\n$CREDITS_NEOBLAST\n\n"
    "Bastian von Halem:\n$CREDITS_BASTIAN\n\n"
    "Lainz:\n$CREDITS_LAINZ\n\n"
    "Jogait:\n$CREDITS_JOGAIT\n\n"
;



/* private data */
#define CREDITS_BGFILE             "themes/credits.bg"
static image_t *box;
static int quit;
static font_t *title, *text, *back;
static input_t *input;
static int line_count;
static bgtheme_t *bgtheme;




/* public functions */

/*
 * credits_init()
 * Initializes the scene
 */
void credits_init()
{
    char *p;

    /* initializing stuff... */
    quit = FALSE;
    input = input_create_user();

    title = font_create(4);
    font_set_text(title, lang_get("CREDITS_TITLE"));
    title->position.x = (VIDEO_SCREEN_W - strlen(font_get_text(title))*font_get_charsize(title).x)/2;
    title->position.y = 5;

    back = font_create(8);
    font_set_text(back, lang_get("CREDITS_KEY"));
    back->position.x = 10;
    back->position.y = VIDEO_SCREEN_H - font_get_charsize(back).y - 5;

    text = font_create(8);
    font_set_text(text, "%s", credits_text);
    font_set_width(text, 300);
    text->position.x = 10;
    text->position.y = VIDEO_SCREEN_H;
    for(line_count=1,p=font_get_text(text); *p; p++)
        line_count += (*p == '\n') ? 1 : 0;

    box = image_create(VIDEO_SCREEN_W, 30);
    image_clear(box, image_rgb(0,0,0));

    bgtheme = background_load(CREDITS_BGFILE);

    fadefx_in(image_rgb(0,0,0), 1.0);
}


/*
 * credits_release()
 * Releases the scene
 */
void credits_release()
{
    bgtheme = background_unload(bgtheme);
    image_destroy(box);

    font_destroy(title);
    font_destroy(text);
    font_destroy(back);

    input_destroy(input);
}


/*
 * credits_update()
 * Updates the scene
 */
void credits_update()
{
    float dt = timer_get_delta();

    /* background movement */
    background_update(bgtheme);

    /* text movement */
    text->position.y -= (3*font_get_charsize(text).y) * dt;
    if(text->position.y < -(line_count * (font_get_charsize(text).y + font_get_charspacing(text).y)))
        text->position.y = VIDEO_SCREEN_H;

    /* quit */
    if(!quit && !fadefx_is_fading()) {
        if(input_button_pressed(input, IB_FIRE3)) {
            sound_play( soundfactory_get("select") );
            quit = TRUE;
        }
        else if(input_button_pressed(input, IB_FIRE4)) {
            sound_play( soundfactory_get("return") );
            quit = TRUE;
        }
    }

    /* music */
    if(!music_is_playing()) {
        music_t *m = music_load(OPTIONS_MUSICFILE);
        music_play(m, INFINITY);
    }

    /* fade-out */
    if(quit) {
        if(fadefx_over()) {
            scenestack_pop();
            return;
        }
        fadefx_out(image_rgb(0,0,0), 1.0);
    }
}



/*
 * credits_render()
 * Renders the scene
 */
void credits_render()
{
    v2d_t cam = v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2);

    background_render_bg(bgtheme, cam);
    background_render_fg(bgtheme, cam);

    font_render(text, cam);
    image_blit(box, video_get_backbuffer(), 0, 0, 0, 0, box->w, box->h);
    image_blit(box, video_get_backbuffer(), 0, 0, 0, VIDEO_SCREEN_H-20, box->w, box->h);
    font_render(title, cam);
    font_render(back, cam);
}

