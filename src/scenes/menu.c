/*
 * menu.c - menu scene
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <psp2/io/stat.h>
#include "menu.h"
#include "quest.h"
#include "../core/quest.h"
#include "../core/scene.h"
#include "../core/storyboard.h"
#include "../core/global.h"
#include "../core/osspec.h"
#include "../core/v2d.h"
#include "../core/logfile.h"
#include "../core/video.h"
#include "../core/audio.h"
#include "../core/timer.h"
#include "../core/lang.h"
#include "../core/soundfactory.h"
#include "../entities/actor.h"
#include "../entities/font.h"
#include "../entities/background.h"



/* private data */
#define MENU_MUSICFILE  "musics/title.ogg"
#define MENU_BGFILE     "themes/menu.bg"

/* menu screens */
#define MENU_MAIN 0 /* main menu */
#define MENU_QUEST 1 /* custom quests */
static int menu_screen; /* SCREEN_* */
static input_t *input;
static scene_t *jump_to;
static bgtheme_t *bgtheme;


/* main menu */
#define MENU_MAXOPTIONS 5
static float start_time;
static int control_restored;
static char menu[MENU_MAXOPTIONS][32];
static int menuopt; /* current option */
static font_t *menufnt[MENU_MAXOPTIONS][2]; /* 0. white ; 1. yellow */
static actor_t *menufoot;
static int surge_entering;
static actor_t *surge, *surgebg, *gametitle;
static font_t *credit, *version;
static int quit;


/* quest menu */
#define MENU_QUESTSPERPAGE 14
static font_t *qstselect[2], *qstdetail;
static int qstmenuopt;
static int qstcount;
static font_t **qstfnt; /* vector of font_t* */
static quest_t **qstdata; /* vector of quest_t* */



/* private functions */
static void select_option(int opt);
static void load_quest_list();
static void release_quest_list();
static int dirfill(const char *filename, int attrib, void *param);
static int dircount(const char *filename, int attrib, void *param);
static int sort_cmp(const void *a, const void *b);
static int qstmenuopt_getpage(int val);
static int qstmenuopt_getmaxpages();
static void game_start(quest_t *q);






/* public functions */

/*
 * menu_init()
 * Initializes the menu
 */
void menu_init()
{
    int i, j;

    /* initializing... */
    quit = FALSE;
    menu_screen = MENU_MAIN;
    start_time = timer_get_ticks()*0.001;
    control_restored = FALSE;
    jump_to = NULL;
    input = input_create_user();
    input_ignore(input);
    load_quest_list();
    music_play( music_load(MENU_MUSICFILE) , INFINITY);


    /* background init */
    bgtheme = background_load(MENU_BGFILE);


    /* main actors */
    surge_entering = TRUE;

    surge = actor_create();
    actor_change_animation(surge, sprite_get_animation("SD_TITLESURGE", 0));
    surge->position.x = (VIDEO_SCREEN_W-actor_image(surge)->w)/2 + 5;
    surge->position.y = -15;

    surgebg = actor_create();
    actor_change_animation(surgebg, sprite_get_animation("SD_TITLEBG", 0));
    surgebg->position.x = (VIDEO_SCREEN_W-actor_image(surgebg)->w)/2;
    surgebg->position.y = surge->position.y+25;

    gametitle = actor_create();
    actor_change_animation(gametitle, sprite_get_animation("SD_TITLEGAMENAME", 0));
    gametitle->position.x = (VIDEO_SCREEN_W-actor_image(gametitle)->w)/2;
    gametitle->position.y = surge->position.y+actor_image(surge)->h-9;

    credit = font_create(8);
    credit->position = v2d_new(3, VIDEO_SCREEN_H-12);
    font_set_text(credit, "%s   2008-2010", GAME_WEBSITE+7);
    version = font_create(0);
    version->position = v2d_new(VIDEO_SCREEN_W-75, 3);
    font_set_text(version, "FREEWARE\n  V%d.%d.%d", GAME_VERSION, GAME_SUB_VERSION, GAME_WIP_VERSION);

    /* main menu */
    menuopt = 0;
    menufoot = actor_create();
    actor_change_animation(menufoot, sprite_get_animation("SD_TITLEFOOT", 0));

    lang_getstring("MENU_1PGAME", menu[0], sizeof(menu[0]));
    lang_getstring("MENU_TUTORIAL", menu[1], sizeof(menu[1]));
    lang_getstring("MENU_CUSTOMQUESTS", menu[2], sizeof(menu[2]));
    lang_getstring("MENU_OPTIONS", menu[3], sizeof(menu[3]));
    lang_getstring("MENU_EXIT", menu[4], sizeof(menu[4]));

    for(i=0; i<2; i++) {
        for(j=0; j<MENU_MAXOPTIONS; j++) {
            menufnt[j][i] = font_create(i);
            menufnt[j][i]->position = v2d_new(112, gametitle->position.y+65+10*j);
            font_set_text(menufnt[j][i], menu[j]);
        }
    }

    /* quest menu */
    qstselect[0] = font_create(8);
    qstselect[0]->position = v2d_new(5, 3);

    qstselect[1] = font_create(8);
    qstselect[1]->position = v2d_new(5, VIDEO_SCREEN_H-13);

    qstdetail = font_create(8);
    qstdetail->position = v2d_new(5, 170);


    /* fade in */
    fadefx_in(image_rgb(0,0,0), 1.5);
}


/*
 * menu_update()
 * Updates the menu
 */
void menu_update()
{
    int i;
    float t = timer_get_ticks() * 0.001;

    /* game start */
    if(jump_to != NULL && fadefx_over()) {
        scenestack_pop();
        scenestack_push(jump_to);
        return;
    }

    /* quit game */
    if(quit && fadefx_over()) {
        game_quit();
        return;
    }

    /* ignore/restore control */
    if(t <= start_time + 2.0)
        input_ignore(input);
    else if(!control_restored) {
        input_restore(input);
        control_restored = TRUE;
    }

    /* background movement */
    background_update(bgtheme);


    /* menu programming */
    if(jump_to || quit)
        return;

    switch(menu_screen) {

        /* ------ main menu ------ */
        case MENU_MAIN:
        {
            /* surge & stuff */
            if(surge_entering && actor_animation_finished(surge)) {
                surge_entering = FALSE;
                actor_change_animation(surge, sprite_get_animation("SD_TITLESURGE", 1));
                input_restore(input);
            }
            gametitle->visible = !surge_entering;

            /* current option */
            menufoot->position.x = menufnt[menuopt][0]->position.x - 20 + 3*cos(2*PI * t);
            menufoot->position.y = menufnt[menuopt][0]->position.y;

            if(input_button_pressed(input, IB_UP)) {
                sound_play( soundfactory_get("choose") );
                menuopt--;
            }
            if(input_button_pressed(input, IB_DOWN)) {
                sound_play( soundfactory_get("choose") );
                menuopt++;
            }
            menuopt = (menuopt%MENU_MAXOPTIONS + MENU_MAXOPTIONS) % MENU_MAXOPTIONS;

            if(input_button_pressed(input, IB_FIRE1) || input_button_pressed(input, IB_FIRE3)) {
                sound_play( soundfactory_get("select") );
                select_option(menuopt);
                return;
            }

            break;
        }





        /* ------ quest menu ----- */
        case MENU_QUEST:
        {
            /* go back to the main menu */
            if(input_button_pressed(input, IB_FIRE4)) {
                sound_play( soundfactory_get("return") );
                menu_screen = MENU_MAIN;
            }

            /* font position */
            for(i=0; i<qstcount; i++) {
                qstfnt[i]->position = v2d_new(30, 20 + 10*(i%MENU_QUESTSPERPAGE));
                qstfnt[i]->visible = (qstmenuopt_getpage(i) == qstmenuopt_getpage(qstmenuopt));
            }

            /* selected option? */
            menufoot->position.x = 10;
            menufoot->position.y = qstfnt[qstmenuopt]->position.y;

            if(input_button_pressed(input, IB_UP)) {
                sound_play( soundfactory_get("choose") );
                qstmenuopt--;
            }
            if(input_button_pressed(input, IB_DOWN)) {
                sound_play( soundfactory_get("choose") );
                qstmenuopt++;
            }
            qstmenuopt = (qstmenuopt%qstcount + qstcount) % qstcount;

            /* quest details */
            font_set_text(qstselect[0], lang_get("MENU_CQ_SELECT"), qstmenuopt_getpage(qstmenuopt), qstmenuopt_getmaxpages());
            font_set_text(qstselect[1], lang_get("MENU_CQ_BACK"));
            font_set_text(qstdetail, lang_get("MENU_CQ_INFO"), qstdata[qstmenuopt]->version, qstdata[qstmenuopt]->name, qstdata[qstmenuopt]->author, qstdata[qstmenuopt]->description);

            /* game start! */
            if(input_button_pressed(input, IB_FIRE1) || input_button_pressed(input, IB_FIRE3)) {
                quest_t *clone = load_quest(qstdata[qstmenuopt]->file);
                sound_play( soundfactory_get("select") );
                game_start(clone);
                return;
            }

            break;
        }
    }
}


/*
 * menu_render()
 * Renders the menu
 */
void menu_render()
{
    int i;
    v2d_t camera = v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2);

    /* don't draw anything. We're leaving... */
    if(quit && fadefx_over())
        return;

    /* background */
    background_render_bg(bgtheme, camera);
    background_render_fg(bgtheme, camera);

    /* rendering menus... :) */
    switch(menu_screen) {
        /* ----- main menu ----- */
        case MENU_MAIN:
        {
            /* menu */
            for(i=0; i<MENU_MAXOPTIONS; i++)
                font_render(menufnt[i][i == menuopt ? 1 : 0], camera);
            actor_render(menufoot, camera);

            /* surge & stuff */
            font_render(credit, camera);
            font_render(version, camera);
            actor_render(surgebg, camera);
            if(surge_entering)
                image_clear(video_get_backbuffer(), image_rgb(0,0,0));
            actor_render(surge, camera);
            actor_render(gametitle, camera);
            break;
        }

        /* ----- quest menu ----- */
        case MENU_QUEST:
        {
            /* quest details */
            image_t *thumb = qstdata[qstmenuopt]->image;
            font_render(qstdetail, camera);
            image_blit(thumb, video_get_backbuffer(), 0, 0, VIDEO_SCREEN_W - thumb->w - 5, (int)qstfnt[0]->position.y, thumb->w, thumb->h);

            /* texts */
            font_render(qstselect[0], camera);
            font_render(qstselect[1], camera);
            for(i=0; i<qstcount; i++)
                font_render(qstfnt[i], camera);
            actor_render(menufoot, camera);
            break;
        }
    }
}



/*
 * menu_release()
 * Releases the menu
 */
void menu_release()
{
    int i, j;

    /* no more music... */
    music_stop();
    music_unref(MENU_MUSICFILE);

    /* main menu stuff */
    font_destroy(credit);
    font_destroy(version);
    for(i=0; i<2; i++) {
        for(j=0; j<MENU_MAXOPTIONS; j++)
            font_destroy(menufnt[j][i]);
    }
    actor_destroy(surgebg);
    actor_destroy(gametitle);
    actor_destroy(surge);

    /* quest menu */
    font_destroy(qstselect[0]);
    font_destroy(qstselect[1]);
    font_destroy(qstdetail);

    /* background */
    bgtheme = background_unload(bgtheme);

    /* misc */
    actor_destroy(menufoot);
    input_destroy(input);
    release_quest_list();
}








/* private functions */


/* the player selected some option at the main menu */
void select_option(int opt)
{
    char abs_path[1024];

    switch(opt) {
        /* 1P GAME */
        case 0:
            resource_filepath(abs_path, "quests/default.qst", sizeof(abs_path), RESFP_READ);
            game_start( load_quest(abs_path) );
            return;

        /* TUTORIAL */
        case 1:
            resource_filepath(abs_path, "quests/tutorial.qst", sizeof(abs_path), RESFP_READ);
            game_start( load_quest(abs_path) );
            return;

        /* CUSTOM QUESTS */
        case 2:
            menu_screen = MENU_QUEST;
            qstmenuopt = 0;
            break;

        /* OPTIONS */
        case 3:
            jump_to = storyboard_get_scene(SCENE_OPTIONS);
            fadefx_out(image_rgb(0,0,0), 0.5);
            return;

        /* EXIT */
        case 4:
            quit = TRUE;
            fadefx_out(image_rgb(0,0,0), 0.5);
            return;
    }
}



/* reads the quest list from the quest/ folder */
void load_quest_list()
{
    int i, j, c = 0;
    int max_paths;
    char dir[] = "quests"; /* no wildcard: scan_directory() (osspec.h) filters by extension below */
    const char *extension = ".qst";
    char abs_path[2][1024];

    logfile_message("load_quest_list()");

    /* official and $HOME quests */
    absolute_filepath(abs_path[0], dir, sizeof(abs_path[0]));
    home_filepath(abs_path[1], dir, sizeof(abs_path[1]));
    max_paths = (strcmp(abs_path[0], abs_path[1]) == 0) ? 1 : 2;

    /* loading quest data */
    qstcount = 0;
    for(j=0; j<max_paths; j++)
        scan_directory(abs_path[j], extension, dircount, NULL);

    qstdata = mallocx(qstcount * sizeof(quest_t*));
    for(j=0; j<max_paths; j++)
        scan_directory(abs_path[j], extension, dirfill, (void*)&c);
    qsort(qstdata, qstcount, sizeof(quest_t*), sort_cmp);

    /* fatal error */
    if(qstcount == 0)
        fatal_error("FATAL ERROR: no quests found! Please reinstall the game.");
    else
        logfile_message("%d quests found.", qstcount);

    /* other stuff */
    qstfnt = mallocx(qstcount * sizeof(font_t*));
    for(i=0; i<qstcount; i++) {
        qstfnt[i] = font_create(8);
        font_set_text(qstfnt[i], "%2d %s", i+1, qstdata[i]->name);
    }
}

int dirfill(const char *filename, int attrib, void *param)
{
    int *c = (int*)param;
    qstdata[ (*c)++ ] = load_quest((char*)filename);
    return 0;
}

int dircount(const char *filename, int attrib, void *param)
{
    qstcount++;
    return 0;
}

/* releases the quest list */
void release_quest_list()
{
    int i;

    logfile_message("release_quest_list()");

    for(i=0; i<qstcount; i++) {
        unload_quest(qstdata[i]);
        font_destroy(qstfnt[i]);
    }

    free(qstdata);
    free(qstfnt);
    qstcount = 0;
}


/*
 * quest_file_mtime()
 * Vita replacement for Allegro's file_time(): file_time() returned a
 * scalar last-modification timestamp, directly comparable by subtraction
 * (newer file = greater value), for a single file. sceIoGetstat() gives
 * the equivalent information as a SceDateTime (separate calendar
 * fields, not a scalar), so those fields are folded here into one
 * monotonically increasing value with the same ordering: comparing two
 * files' mtimes with normal subtraction still says which one is newer.
 * That ordering is the only thing sort_cmp() below relies on, so this
 * preserves its behavior exactly, even though the packed value itself
 * isn't a real Unix timestamp.
 */
long quest_file_mtime(const char *path)
{
    SceIoStat stat;

    if(sceIoGetstat(path, &stat) < 0)
        return 0;

    return (((((long)stat.st_mtime.year * 12 + stat.st_mtime.month) * 31 + stat.st_mtime.day)
             * 24 + stat.st_mtime.hour) * 60 + stat.st_mtime.minute) * 60 + stat.st_mtime.second;
}

/* comparator */
int sort_cmp(const void *a, const void *b)
{
    quest_t *q[2] = { *((quest_t**)a), *((quest_t**)b) };
    return (int)( quest_file_mtime((const char*)(q[1]->file)) - quest_file_mtime((const char*)(q[0]->file)) );
}


/* returns a page number... */
int qstmenuopt_getpage(int val)
{
    return val/MENU_QUESTSPERPAGE + 1;
}

/* how many pages? */
int qstmenuopt_getmaxpages()
{
    return qstcount/MENU_QUESTSPERPAGE + ((qstcount%MENU_QUESTSPERPAGE == 0) ? 0 : 1);
}


/* closes the menu and starts the game. Call return after this. */
void game_start(quest_t *q)
{
    quest_run(q, FALSE);
    jump_to = storyboard_get_scene(SCENE_QUEST);
    input_ignore(input);
    fadefx_out(image_rgb(0,0,0), 0.5);
}
