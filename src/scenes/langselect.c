/*
 * langselect.c - language selection screen
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
#include "langselect.h"
#include "options.h"
#include "../core/scene.h"
#include "../core/preferences.h"
#include "../core/osspec.h"
#include "../core/stringutil.h"
#include "../core/logfile.h"
#include "../core/video.h"
#include "../core/audio.h"
#include "../core/lang.h"
#include "../core/input.h"
#include "../core/timer.h"
#include "../core/soundfactory.h"
#include "../entities/font.h"
#include "../entities/actor.h"
#include "../entities/background.h"


/* private data */
#define LANG_BGFILE             "themes/langselect.bg"
#define LANG_MAXPERPAGE         8
typedef struct {
    char title[128];
    char filepath[1024];
} lngdata_t;

static int quit;
static int lngcount;
static font_t *title[2], **lngfnt[2], *page_label;
static lngdata_t *lngdata;
static int option; /* current option: 0..n-1 */
static actor_t *icon;
static input_t *input;
static float scene_time;
static bgtheme_t *bgtheme;

/* private functions */
static void save_preferences(const char *filepath);
static void load_lang_list();
static void unload_lang_list();
static int dirfill(const char *filename, int attrib, void *param);
static int dircount(const char *filename, int attrib, void *param);
static int sort_cmp(const void *a, const void *b);






/* public functions */

/*
 * langselect_init()
 * Initializes the scene
 */
void langselect_init()
{
    option = 0;
    quit = FALSE;
    scene_time = 0;
    input = input_create_user();

    page_label = font_create(8);

    title[0] = font_create(4);
    title[1] = font_create(4);
    font_set_text(title[0], "SELECT YOUR");
    font_set_text(title[1], "LANGUAGE");
    title[0]->position.x = (VIDEO_SCREEN_W - strlen(font_get_text(title[0]))*font_get_charsize(title[0]).x)/2;
    title[0]->position.y = 10;
    title[1]->position.x = (VIDEO_SCREEN_W - strlen(font_get_text(title[1]))*font_get_charsize(title[1]).x)/2;
    title[1]->position.y = title[0]->position.y + font_get_charsize(title[1]).y + 10;

    bgtheme = background_load(LANG_BGFILE);

    icon = actor_create();
    actor_change_animation(icon, sprite_get_animation("SD_TITLEFOOT", 0));

    load_lang_list();
    fadefx_in(image_rgb(0,0,0), 1.0);
}


/*
 * langselect_release()
 * Releases the scene
 */
void langselect_release()
{
    unload_lang_list();
    bgtheme = background_unload(bgtheme);

    actor_destroy(icon);
    font_destroy(title[0]);
    font_destroy(title[1]);
    font_destroy(page_label);
    input_destroy(input);
}


/*
 * langselect_update()
 * Updates the scene
 */
void langselect_update()
{
    float dt = timer_get_delta();
    scene_time += dt;

    /* background movement */
    background_update(bgtheme);

    /* menu option */
    icon->position = lngfnt[0][option]->position;
    icon->position.x -= 15;
    icon->position.x += 5*cos(2*PI * scene_time);
    if(!quit && !fadefx_is_fading()) {
        if(input_button_pressed(input, IB_DOWN)) {
            option = (option+1)%lngcount;
            sound_play( soundfactory_get("choose") );
        }
        if(input_button_pressed(input, IB_UP)) {
            option = (((option-1)%lngcount)+lngcount)%lngcount;
            sound_play( soundfactory_get("choose") );
        }
        if(input_button_pressed(input, IB_FIRE1) || input_button_pressed(input, IB_FIRE3)) {
            char *filepath = lngdata[option].filepath;
            logfile_message("Loading language \"%s\", \"%s\"", lngdata[option].title, filepath);
            lang_loadfile(DEFAULT_LANGUAGE_FILEPATH); /* just in case of missing strings... */
            lang_loadfile(filepath);
            save_preferences(filepath);
            sound_play( soundfactory_get("select") );
            quit = TRUE;
        }
        if(input_button_pressed(input, IB_FIRE4)) {
            sound_play( soundfactory_get("return") );
            quit = TRUE;
        }
    }

    /* page label */
    font_set_text(page_label, "page %d/%d", 1+option/LANG_MAXPERPAGE, 1+max(0,lngcount-1)/LANG_MAXPERPAGE);
    page_label->position.x = VIDEO_SCREEN_W - strlen(font_get_text(page_label))*font_get_charsize(page_label).x - 10;
    page_label->position.y = VIDEO_SCREEN_H - font_get_charsize(page_label).y - 3;

    /* music */
    if(!music_is_playing()) {
        music_t *m = music_load(OPTIONS_MUSICFILE);
        music_play(m, INFINITY);
    }

    /* quit */
    if(quit) {
        if(fadefx_over()) {
            scenestack_pop();
            return;
        }
        fadefx_out(image_rgb(0,0,0), 1.0);
    }
}



/*
 * langselect_render()
 * Renders the scene
 */
void langselect_render()
{
    int i;
    v2d_t cam = v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2);

    background_render_bg(bgtheme, cam);
    background_render_fg(bgtheme, cam);

    font_render(title[0], cam);
    font_render(title[1], cam);
    font_render(page_label, cam);

    for(i=0; i<lngcount; i++) {
        if(i/LANG_MAXPERPAGE == option/LANG_MAXPERPAGE)
            font_render(lngfnt[option==i ? 1 : 0][i], cam);
    }

    actor_render(icon, cam);
}






/* private methods */

/* saves the user preferences */
void save_preferences(const char *filepath)
{
    preferences_set_languagepath(filepath);
}

/* reads the language list from the languages/ folder */
void load_lang_list()
{
    int i, j, c = 0;
    int max_paths;
    char dir[] = "languages"; /* no wildcard: scan_directory() (osspec.h) filters by extension below */
    const char *extension = ".lng";
    char abs_path[2][1024];

    logfile_message("load_lang_list()");

    /* official and $HOME files */
    absolute_filepath(abs_path[0], dir, sizeof(abs_path[0]));
    home_filepath(abs_path[1], dir, sizeof(abs_path[1]));
    max_paths = (strcmp(abs_path[0], abs_path[1]) == 0) ? 1 : 2;

    /* loading language data */
    lngcount = 0;
    for(j=0; j<max_paths; j++)
        scan_directory(abs_path[j], extension, dircount, NULL);

    lngdata = mallocx(lngcount * sizeof(lngdata_t));
    for(j=0; j<max_paths; j++)
        scan_directory(abs_path[j], extension, dirfill, (void*)&c);
    qsort(lngdata, lngcount, sizeof(lngdata_t), sort_cmp);

    /* fatal error */
    if(lngcount == 0)
        fatal_error("FATAL ERROR: no language files were found! Please reinstall the game.");
    else
        logfile_message("%d languages found.", lngcount);

    /* other stuff */
    lngfnt[0] = mallocx(lngcount * sizeof(font_t*));
    lngfnt[1] = mallocx(lngcount * sizeof(font_t*));
    for(i=0; i<lngcount; i++) {
        lngfnt[0][i] = font_create(8);
        lngfnt[1][i] = font_create(8);
        font_set_text(lngfnt[0][i], "%2d. %s", i+1, lngdata[i].title);
        font_set_text(lngfnt[1][i], "<color=ffff00>% 2d. %s</color>", i+1, lngdata[i].title);
        lngfnt[0][i]->position = v2d_new(25, 75 + 20*(i%LANG_MAXPERPAGE));
        lngfnt[1][i]->position = v2d_new(25, 75 + 20*(i%LANG_MAXPERPAGE));
    }
}

int dirfill(const char *filename, int attrib, void *param)
{
    int *c = (int*)param;
    int ver, subver, wipver;

    lang_readcompatibility((char*)filename, &ver, &subver, &wipver);
    if(ver == GAME_VERSION && subver == GAME_SUB_VERSION && wipver == GAME_WIP_VERSION) {
        sprintf(lngdata[*c].filepath, "languages/%s", basename(filename));
        lang_readstring((char*)filename, "LANG_LANGUAGE", lngdata[*c].title, sizeof( lngdata[*c].title ));
        (*c)++;
    }
    else
        logfile_message("Warning: language file \"%s\" (compatibility: %d.%d.%d) isn't compatible with this version of the game (%d.%d.%d)", filename, ver, subver, wipver, GAME_VERSION, GAME_SUB_VERSION, GAME_WIP_VERSION);

    return 0;
}

int dircount(const char *filename, int attrib, void *param)
{
    int ver, subver, wipver;
    lang_readcompatibility((char*)filename, &ver, &subver, &wipver);
    if(ver == GAME_VERSION && subver == GAME_SUB_VERSION && wipver == GAME_WIP_VERSION)
        lngcount++;

    return 0;
}

/* unloads the language list */
void unload_lang_list()
{
    int i;

    logfile_message("unload_lang_list()");

    for(i=0; i<lngcount; i++) {
        font_destroy(lngfnt[0][i]);
        font_destroy(lngfnt[1][i]);
    }

    free(lngfnt[0]);
    free(lngfnt[1]);
    free(lngdata);
    lngcount = 0;
}


/* comparator */
int sort_cmp(const void *a, const void *b)
{
    lngdata_t *q[2] = { (lngdata_t*)a, (lngdata_t*)b };
    if(str_icmp(q[0]->title, "English") == 0) return -1;
    if(str_icmp(q[1]->title, "English") == 0) return 1;
    return str_icmp(q[0]->title, q[1]->title);
}

