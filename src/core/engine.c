/*
 * engine.c - game engine facade
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

#include <string.h>
#include <psp2/kernel/processmgr.h>
#include "engine.h"
#include "global.h"
#include "scene.h"
#include "storyboard.h"
#include "util.h"
#include "osspec.h"
#include "resourcemanager.h"
#include "stringutil.h"
#include "logfile.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "timer.h"
#include "sprite.h"
#include "soundfactory.h"
#include "lang.h"
#include "screenshot.h"
#include "preferences.h"
#include "commandline.h"
#include "nanoparser/nanoparser.h"
#include "../scenes/quest.h"
#include "../scenes/level.h"
#include "../entities/actor.h"
#include "../entities/brick.h"
#include "../entities/player.h"
#include "../entities/item.h"
#include "../entities/enemy.h"
#include "../entities/font.h"
#include "frame_diag.h"

/* private stuff ;) */
static void clean_garbage();
static void init_basic_stuff();
static void init_managers(commandline_t cmd);
static void init_accessories(commandline_t cmd);
static void init_game_data();
static void push_initial_scene(commandline_t cmd);
static void release_accessories();
static void release_managers();
static void release_basic_stuff();
static const char* get_window_title();
static void parser_error(const char *msg);
static void parser_warning(const char *msg);


/* public functions */

/*
 * engine_init()
 * Initializes all the subsystems of
 * the game engine
 */
void engine_init(int argc, char **argv)
{
    commandline_t cmd;

    init_basic_stuff();
    cmd = commandline_parse(argc, argv);

    init_managers(cmd);
    init_accessories(cmd);
    init_game_data();

    push_initial_scene(cmd);
}


/*
 * engine_mainloop()
 * A classic main loop
 */
void engine_mainloop()
{
    scene_t *scn;

    frame_diag_init();

    while(!game_is_over() && !scenestack_empty()) {
        /* updating the managers */
        timer_update();
        input_update();

        frame_diag_sample();
        frame_diag_update_toggle();

        audio_update();

        /* updating the current scene */
        scn = scenestack_top();
        scn->update();

        /* rendering the current scene */
        if(scn == scenestack_top()) /* scn may have been 'popped' out */
            scn->render();

        frame_diag_render();

        screenshot_update();
        video_render();

        /* calling the garbage collector */
        clean_garbage();
    }

    frame_diag_release();
}


/*
 * engine_release()
 * Releases the game engine and its
 * subsystems
 */
void engine_release()
{
    release_accessories();
    release_managers();
    release_basic_stuff();
}




/* private functions */

/*
 * clean_garbage()
 * Runs the garbage collector.
 */
void clean_garbage()
{
    static uint32 last = 0;
    uint32 t = timer_get_ticks();

    if(t >= last + 2000) { /* every 2 seconds */
        last = t;
        resourcemanager_release_unused_resources();
    }
}


/*
 * init_basic_stuff()
 * Initializes the basic stuff, such as Allegro.
 * Call this before anything else.
 */
void init_basic_stuff()
{
    randomize();
    osspec_init();
    logfile_init();
    nanoparser_set_error_function(parser_error);
    nanoparser_set_warning_function(parser_warning);
    preferences_init();
}


/*
 * init_managers()
 * Initializes the managers
 */
void init_managers(commandline_t cmd)
{
    timer_init();
    video_init(get_window_title(), cmd.video_resolution, cmd.smooth_graphics, cmd.fullscreen, cmd.color_depth);
    video_show_fps(cmd.show_fps);
    audio_init();
    input_init();
    resourcemanager_init();
}


/*
 * init_accessories()
 * Initializes the accessories
 */
void init_accessories(commandline_t cmd)
{
    sprite_init();
    font_init();
    soundfactory_init();
    objects_init();
    storyboard_init();
    screenshot_init();
    lang_init();
    if(strcmp(cmd.language_filepath, "") != 0)
        lang_loadfile(cmd.language_filepath);
    scenestack_init();
}


/*
 * init_game_data()
 * Initializes the game data
 */
void init_game_data()
{
    player_set_lives(PLAYER_INITIAL_LIVES);
    player_set_score(0);
}


/*
 * push_initial_scene()
 * Decides which scene should be pushed into the scene stack
 */
void push_initial_scene(commandline_t cmd)
{
    if(cmd.custom_level) {
        scenestack_push(storyboard_get_scene(SCENE_MENU));
        level_setfile(cmd.custom_level_path);
        scenestack_push(storyboard_get_scene(SCENE_LEVEL));
    }
    else if(cmd.custom_quest) {
        quest_t *q = load_quest(cmd.custom_quest_path);
        quest_run(q, FALSE);
        scenestack_push(storyboard_get_scene(SCENE_QUEST));
    }
    else
        scenestack_push(storyboard_get_scene(SCENE_INTRO));
}


/*
 * release_accessories()
 * Releases the previously loaded accessories
 */
void release_accessories()
{
    scenestack_release();
    storyboard_release();
    lang_release();
    screenshot_release();
    objects_release();
    soundfactory_release();
    sprite_release();
}

/*
 * release_managers()
 * Releases the previously loaded managers
 */
void release_managers()
{
    input_release();
    video_release();
    resourcemanager_release();
    audio_release();
    timer_release();
}


/*
 * release_basic_stuff()
 * Releases the basic stuff, such as Allegro.
 * Call this after everything else.
 */
void release_basic_stuff()
{
    logfile_release();
    osspec_release();
}


/*
 * get_window_title()
 * Returns the title of the window
 */
const char* get_window_title()
{
    static char window_title[128];

#ifndef GAME_STABLE_RELEASE
    sprintf(window_title, "%s %d.%d.%d - work-in-progress version", GAME_TITLE, GAME_VERSION, GAME_SUB_VERSION, GAME_WIP_VERSION);
#else
    sprintf(window_title, "%s %d.%d.%d", GAME_TITLE, GAME_VERSION, GAME_SUB_VERSION, GAME_WIP_VERSION);
#endif

    return window_title;
}

/*
 * parser_error()
 * This is called by nanoparser when an error is raised
 */
void parser_error(const char *msg)
{
    fatal_error("%s", msg);
}

/*
 * parser_warning()
 * This is called by nanoparser when a warning is raised
 */
void parser_warning(const char *msg)
{
    logfile_message("WARNING: %s", msg);
}

