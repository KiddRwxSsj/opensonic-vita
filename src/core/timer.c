/*
 * timer.c - time handler
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
 * Upstream already had a non-Allegro timing path for non-Windows builds
 * (gettimeofday-based, see the #else branch this file used to have under
 * USE_ALLEGRO_TIMERS). VitaSDK's newlib provides gettimeofday(), so that
 * exact path is kept; only install_timer()/Allegro's interrupt-driven
 * path is removed, since it's unreachable on this platform anyway.
 */

#include <sys/time.h>
#include "global.h"
#include "timer.h"
#include "util.h"
#include "logfile.h"

/* constants */
#define MIN_FRAME_INTERVAL 10 /* (1/10) * 1000 = 100 fps max */
#define MAX_FRAME_INTERVAL 16 /* (1/16) * 1000 ~  62 fps min */

/* internal data */
static int partial_fps, fps_accum, fps;
static uint32 last_time;
static float delta;
static uint32 start_time;
static uint32 get_tick_count();

/*
 * timer_init()
 * Initializes the Time Handler
 */
void timer_init()
{
    logfile_message("timer_init()");

    partial_fps = 0;
    fps_accum = 0;
    fps = 0;
    delta = 0.0;

    start_time = get_tick_count();
    last_time = timer_get_ticks();
}


/*
 * timer_update()
 * Updates the Time Handler. This routine
 * must be called at every cycle of
 * the main loop
 */
void timer_update()
{
    uint32 current_time, delta_time; /* both in milliseconds */

    /* time control */
    for(delta_time = 0; delta_time < MIN_FRAME_INTERVAL; ) {
        current_time = timer_get_ticks();
        delta_time = (current_time > last_time) ? (current_time - last_time) : 0;
        last_time = (current_time >= last_time) ? last_time : current_time;
    }
    delta_time = min(delta_time, MAX_FRAME_INTERVAL);
    delta = (float)delta_time * 0.001;

    /* FPS (frames per second) */
    partial_fps++; /* 1 render per cycle */
    fps_accum += (int)delta_time;
    if(fps_accum >= 1000) {
        fps = partial_fps;
        partial_fps = 0;
        fps_accum = 0;
    }

    /* done! */
    last_time = timer_get_ticks();
}


/*
 * timer_release()
 * Releases the Time Handler
 */
void timer_release()
{
    logfile_message("timer_release()");
}


/*
 * timer_get_delta()
 * Returns the time interval, in seconds,
 * between the last two cycles of the
 * main loop
 */
float timer_get_delta()
{
    return delta;
}


/*
 * timer_get_ticks()
 * Elapsed milliseconds since
 * the application has started
 */
uint32 timer_get_ticks()
{
    uint32 ticks = get_tick_count();
    if(ticks < start_time)
        start_time = ticks;
    return ticks - start_time;
}


/*
 * timer_get_fps()
 * Returns the FPS rate
 */
int timer_get_fps()
{
    return fps;
}


/* internal methods */

uint32 get_tick_count()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec*1000) + (now.tv_usec/1000);
}
