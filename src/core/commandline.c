/*
 * commandline.c - command line parser
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
 *
 * --- PS Vita port ---
 * A VPK-launched app gets no meaningful argv (see main.c), so all of
 * the desktop --flag parsing is unreachable here and is dropped along
 * with its Allegro dependency (allegro_message(), desktop_color_depth()).
 * What's kept is exactly what engine_init() actually needs: a
 * commandline_t seeded from the user's saved preferences, same as the
 * desktop build's own defaults before it looks at argv.
 */

#include <stdlib.h>
#include "commandline.h"
#include "global.h"
#include "stringutil.h"
#include "util.h"
#include "video.h"
#include "preferences.h"

/*
 * commandline_parse()
 * On Vita, argc/argv are unused (see main.c); this just returns the
 * defaults the desktop build would also start from before parsing argv.
 */
commandline_t commandline_parse(int argc, char **argv)
{
    commandline_t cmd;
    (void)argc;
    (void)argv;

    cmd.video_resolution = preferences_get_videoresolution();
    cmd.smooth_graphics = preferences_get_smooth();
    cmd.fullscreen = preferences_get_fullscreen();
    cmd.show_fps = preferences_get_showfps();
    str_cpy(cmd.language_filepath, preferences_get_languagepath(), sizeof(cmd.language_filepath));
    cmd.color_depth = 32;
    cmd.custom_level = FALSE;
    cmd.custom_quest = FALSE;
    cmd.custom_level_path[0] = '\0';
    cmd.custom_quest_path[0] = '\0';

    return cmd;
}
