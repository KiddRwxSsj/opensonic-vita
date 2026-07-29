/*
 * video.h - video manager
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

#ifndef _VIDEO_H
#define _VIDEO_H

#include "image.h"
#include "v2d.h"



/* video modes -- kept for source compatibility with menu/options code.
 * On Vita there is exactly one physical display, so these no longer pick
 * a window size: they only pick how the fixed 320x240 backbuffer is
 * scaled onto the 960x544 screen (see draw_to_screen() in video.c).
 * Presentation mapping (remapped per user request):
 *   1X   ("Tiny")   -> integer 2x scale, centered (small bars)
 *   2X   ("Normal") -> uniform 4:3 aspect-fit scale, centered (may letterbox)
 *   MAX  ("Max")    -> true fullscreen, independent x/y scale, fills 960x544, no bars */
#define VIDEORESOLUTION_1X        0 /* "Tiny" in the options menu -- see mapping above */
#define VIDEORESOLUTION_2X        1 /* "Normal" in the options menu -- see mapping above */
#define VIDEORESOLUTION_MAX       2 /* "Max" in the options menu -- true fullscreen, no bars */
#define VIDEORESOLUTION_EDT       3 /* level editor -- unreachable, needs a mouse */


/* video manager */
void video_init(const char *window_title, int resolution, int smooth, int fullscreen, int bpp);
void video_release();
void video_render();
void video_showmessage(const char *fmt, ...);
int video_get_color_depth();
int video_is_window_active();
uint32 video_get_maskcolor();
void video_changemode(int resolution, int smooth, int fullscreen);
int video_get_resolution();
int video_is_smooth();
int video_is_fullscreen();
v2d_t video_get_window_size();


/* backbuffer */
#define VIDEO_SCREEN_W            320
#define VIDEO_SCREEN_H            240
image_t *video_get_backbuffer();



/* fps counter */
void video_show_fps(int show);
int video_is_fps_visible();



/* Fade-in & fade-out */
void fadefx_in(uint32 color, float seconds); /* fade in */
void fadefx_out(uint32 color, float seconds); /* fade out */
int fadefx_over(); /* end of fade effect? (only one action when this event loops) */
int fadefx_is_fading(); /* is the fade effect ocurring? */

#endif
