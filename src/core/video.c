/*
 * video.c - video manager
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
 *
 * --- PS Vita port ---
 * The game still renders everything (sprites, HUD, level tiles) into the
 * same 320x240 software backbuffer as upstream -- pixel-perfect collision
 * in image.c depends on that. What changes is the final step: instead of
 * a CPU-side 2xSaI/SuperEagle upscale into a second Allegro window
 * surface, the finished backbuffer is uploaded once per frame into a
 * vita2d texture and the GPU scales it onto the 960x544 screen. This
 * drops fast2x_blit()/filter_blit() and the 2xSaI/SuperEagle filters
 * entirely: they existed to fake smooth upscaling on hardware that had
 * no guaranteed GPU scaling path, which does not describe the Vita.
 *
 * The debug video message / FPS counter used Allegro's built-in system
 * font (font, textout_ex). This was first replaced with vita2d's PGF
 * wrapper around the system font (drawn straight onto the display after
 * the game texture, so it never touched the software backbuffer). That
 * approach is dropped: it depends on SceFont_stub, which this VitaSDK
 * install doesn't provide. Debug text now goes through the engine's own
 * bitmap font module (entities/font.c/font.h) instead -- the same
 * font_t the HUD and dialog boxes already use (see e.g. level.c's
 * lifefnt/mainfnt[] or dlgbox_title for the same create-once,
 * font_set_text()-per-frame, font_render() pattern followed below).
 *
 * This does change one thing: font_render() always draws onto
 * video_get_backbuffer() (see font.c's render_char() call), so unlike
 * the vita2d_pgf text, debug text is now part of the 320x240 software
 * image and gets GPU-upscaled onto the 960x544 screen along with
 * everything else, instead of being drawn separately at native screen
 * resolution after the upscale. It's still presentation-layer debug
 * text, not game content, and still never affects gameplay pixels --
 * it's just no longer pixel-crisp at the physical screen resolution.
 * Positions/margins below are given in backbuffer (320x240) space, not
 * the previous 960x544 screen space.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <vita2d.h>
#include "video.h"
#include "timer.h"
#include "logfile.h"
#include "util.h"
/* Layering note: this pulls in the entities-level font module from core/
 * code, which upstream's architecture wouldn't do. It's specifically for
 * the debug overlay text below (see file header comment) -- font_t is
 * otherwise an entities-layer concept. */
#include "../entities/font.h"

#define SCREEN_W  960
#define SCREEN_H  544

/* private data */
static image_t *video_buffer;
static vita2d_texture *screen_texture;
static font_t *debug_font_msg; /* video_showmessage() text; type 8 = full ASCII, like dlgbox_title/message */
static font_t *debug_font_fps; /* FPS counter; type 0 = uppercase+digits+':', enough for "FPS: %d" */
static int video_smooth;
static int video_resolution;
static int video_fullscreen;
static int video_showfps;
static void draw_to_screen(image_t *img);
static void blend_fill(image_t *img, uint32 color, float alpha);

/* Fade-in & fade-out */
#define FADEFX_NONE            0
#define FADEFX_IN              1
#define FADEFX_OUT             2
static int fadefx_type;
static int fadefx_end;
static uint32 fadefx_color;
static float fadefx_elapsed_time;
static float fadefx_total_time;

/* Video Message */
#define VIDEOMSG_TIMEOUT    5000
static uint32 videomsg_endtime;
static char videomsg_data[512];



/* video manager */

/*
 * video_init()
 * Initializes the video manager
 */
void video_init(const char *window_title, int resolution, int smooth, int fullscreen, int bpp)
{
    (void)window_title; /* no window system on Vita */
    (void)bpp; /* always 32bpp RGBA truecolor on Vita */

    logfile_message("video_init()");

    vita2d_init();
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
    vita2d_set_vblank_wait(1);

    debug_font_msg = font_create(8);
    debug_font_fps = font_create(0);
    screen_texture = vita2d_create_empty_texture_format(VIDEO_SCREEN_W, VIDEO_SCREEN_H, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);

    video_buffer = NULL;
    video_changemode(resolution, smooth, fullscreen);

    videomsg_endtime = 0;

    logfile_message("video_init() ok");
}

/*
 * video_changemode()
 * On Vita there's no real display mode switch -- this only picks how
 * the fixed 320x240 backbuffer gets scaled/filtered onto the screen.
 */
void video_changemode(int resolution, int smooth, int fullscreen)
{
    logfile_message("video_changemode(%d,%d,%d)", resolution, smooth, fullscreen);

    video_resolution = resolution;
    video_fullscreen = fullscreen; /* always effectively fullscreen; kept for API compatibility */
    video_smooth = smooth;

    vita2d_texture_set_filters(screen_texture,
        video_smooth ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT,
        video_smooth ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);

    if(video_buffer != NULL)
        image_destroy(video_buffer);
    video_buffer = image_create(VIDEO_SCREEN_W, VIDEO_SCREEN_H);
    image_clear(video_buffer, image_rgb(0, 0, 0));

    logfile_message("video_changemode() ok");
}


/*
 * video_get_resolution()
 * Returns the current resolution value,
 * i.e., VIDEORESOLUTION_*
 */
int video_get_resolution()
{
    return video_resolution;
}


/*
 * video_is_smooth()
 * Smooth graphics?
 */
int video_is_smooth()
{
    return video_smooth;
}


/*
 * video_is_fullscreen()
 * Fullscreen mode?
 */
int video_is_fullscreen()
{
    return video_fullscreen;
}


/*
 * video_get_window_size()
 * There's no window on Vita; this reports the physical screen size,
 * which is what VIDEORESOLUTION_MAX scales the backbuffer up to.
 */
v2d_t video_get_window_size()
{
    return v2d_new(SCREEN_W, SCREEN_H);
}


/*
 * video_get_backbuffer()
 * Returns a pointer to the backbuffer
 */
image_t* video_get_backbuffer()
{
    if(video_buffer == NULL)
        fatal_error("FATAL ERROR: video_get_backbuffer() returned NULL!");

    return video_buffer;
}

/*
 * video_render()
 * Updates the video manager and the screen
 */
void video_render()
{
    /* fade effect */
    fadefx_end = FALSE;
    if(fadefx_type != FADEFX_NONE) {
        fadefx_elapsed_time += timer_get_delta();
        if(fadefx_elapsed_time < fadefx_total_time) {
            int n = (int)((float)255 * (fadefx_elapsed_time * 1.25f / fadefx_total_time));
            n = clip(n, 0, 255);
            n = (fadefx_type == FADEFX_IN) ? 255 - n : n;
            blend_fill(video_get_backbuffer(), fadefx_color, n / 255.0f);
        }
        else {
            if(fadefx_type == FADEFX_OUT)
                image_rectfill(video_get_backbuffer(), 0, 0, VIDEO_SCREEN_W - 1, VIDEO_SCREEN_H - 1, fadefx_color);
            fadefx_type = FADEFX_NONE;
            fadefx_total_time = fadefx_elapsed_time = 0;
            fadefx_color = 0;
            fadefx_end = TRUE;
        }
    }

    /* render */
    draw_to_screen(video_get_backbuffer());
}


/*
 * video_release()
 * Releases the video manager
 */
void video_release()
{
    logfile_message("video_release()");

    if(video_buffer != NULL)
        image_destroy(video_buffer);

    if(screen_texture != NULL)
        vita2d_free_texture(screen_texture);

    if(debug_font_msg != NULL)
        font_destroy(debug_font_msg);

    if(debug_font_fps != NULL)
        font_destroy(debug_font_fps);

    vita2d_fini();

    logfile_message("video_release() ok");
}


/*
 * video_showmessage()
 * Shows a text message to the user
 */
void video_showmessage(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(videomsg_data, sizeof(videomsg_data), fmt, args);
    va_end(args);

    videomsg_endtime = timer_get_ticks() + VIDEOMSG_TIMEOUT;
}


/*
 * video_get_color_depth()
 * Vita is always 32bpp RGBA truecolor
 */
int video_get_color_depth()
{
    return 32;
}


/*
 * video_is_window_active()
 * The Vita app is either running in the foreground or suspended (in
 * which case we're not rendering at all), so this is always true.
 */
int video_is_window_active()
{
    return TRUE;
}



/*
 * video_get_maskcolor()
 * Returns the mask color
 */
uint32 video_get_maskcolor()
{
    return image_rgb(255, 0, 255);
}


/*
 * video_show_fps()
 * Shows/hides the FPS counter
 */
void video_show_fps(int show)
{
    video_showfps = show;
}


/*
 * video_is_fps_visible()
 * Is the FPS counter visible?
 */
int video_is_fps_visible()
{
    return video_showfps;
}





/* private stuff */

/* fills img with color, alpha-blended over the existing content
 * (replaces the truecolor branch of Allegro's DRAW_MODE_TRANS + rectfill) */
static void blend_fill(image_t *img, uint32 color, float alpha)
{
    uint8 cr, cg, cb;
    int i, count = img->w * img->h;

    image_color2rgb(color, &cr, &cg, &cb);
    alpha = clip(alpha, 0.0f, 1.0f);

    for(i = 0; i < count; i++) {
        uint8 dr, dg, db;
        image_color2rgb(img->pixels[i], &dr, &dg, &db);
        img->pixels[i] = image_rgb(
            (uint8)(dr + (cr - dr) * alpha),
            (uint8)(dg + (cg - dg) * alpha),
            (uint8)(db + (cb - db) * alpha)
        );
    }
}

/* uploads the backbuffer and presents it, scaled to fit the screen.
 * Tiny/Normal preserve the original 4:3 aspect ratio (may letterbox);
 * Max stretches independently on x/y to fill the screen with no bars. */
static void draw_to_screen(image_t *img)
{
    float scale_x, scale_y, scale, draw_w, draw_h, off_x, off_y;
    void *texdata;

    /* debug overlay text: font_render() always draws onto
     * video_get_backbuffer() (== img here), so it has to happen before
     * the backbuffer is copied into the screen texture below -- see the
     * file header comment for why this moved here from after
     * vita2d_draw_texture_scale(). Position is fixed on screen
     * regardless of the in-game camera, same as level.c's HUD/dialog
     * box text: camera_position = (VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2)
     * makes font_t's position field line up directly with backbuffer
     * pixel coordinates (see font_render() in font.c). */
    if(timer_get_ticks() < videomsg_endtime) {
        debug_font_msg->position = v2d_new(4, VIDEO_SCREEN_H - 12);
        font_set_text(debug_font_msg, videomsg_data);
        font_render(debug_font_msg, v2d_new(VIDEO_SCREEN_W / 2, VIDEO_SCREEN_H / 2));
    }

    if(video_is_fps_visible()) {
        debug_font_fps->position = v2d_new(VIDEO_SCREEN_W - 50, 4);
        font_set_text(debug_font_fps, "FPS: %d", timer_get_fps());
        font_render(debug_font_fps, v2d_new(VIDEO_SCREEN_W / 2, VIDEO_SCREEN_H / 2));
    }

    texdata = vita2d_texture_get_datap(screen_texture);
    memcpy(texdata, img->pixels, sizeof(uint32) * img->w * img->h);

    scale_x = (float)SCREEN_W / (float)VIDEO_SCREEN_W;
    scale_y = (float)SCREEN_H / (float)VIDEO_SCREEN_H;

    /* Resolution remap (per user request):
     *   Tiny   (VIDEORESOLUTION_1X) -> old "Normal" look: integer 2x, centered
     *   Normal (VIDEORESOLUTION_2X) -> old "Max" look: uniform aspect-fit scale (4:3, may letterbox)
     *   Max    (VIDEORESOLUTION_MAX) -> true fullscreen: independent x/y scale, fills the
     *                                    whole 960x544 screen, no bars (aspect ratio not preserved)
     * VIDEORESOLUTION_EDT (level editor) keeps the old aspect-fit behavior; it's unreachable
     * on this port anyway (needs a mouse), so it's not part of the user-facing remap. */
    switch(video_get_resolution()) {
        case VIDEORESOLUTION_1X:
            scale = 2.0f;
            draw_w = VIDEO_SCREEN_W * scale;
            draw_h = VIDEO_SCREEN_H * scale;
            off_x = (SCREEN_W - draw_w) / 2.0f;
            off_y = (SCREEN_H - draw_h) / 2.0f;
            break;

        case VIDEORESOLUTION_2X:
            scale = min(scale_x, scale_y);
            draw_w = VIDEO_SCREEN_W * scale;
            draw_h = VIDEO_SCREEN_H * scale;
            off_x = (SCREEN_W - draw_w) / 2.0f;
            off_y = (SCREEN_H - draw_h) / 2.0f;
            break;

        case VIDEORESOLUTION_MAX:
            /* fullscreen: stretch to fill exactly, independent x/y scale, no letterboxing */
            draw_w = SCREEN_W;
            draw_h = SCREEN_H;
            off_x = 0.0f;
            off_y = 0.0f;
            break;

        case VIDEORESOLUTION_EDT:
        default:
            scale = min(scale_x, scale_y);
            draw_w = VIDEO_SCREEN_W * scale;
            draw_h = VIDEO_SCREEN_H * scale;
            off_x = (SCREEN_W - draw_w) / 2.0f;
            off_y = (SCREEN_H - draw_h) / 2.0f;
            break;
    }

    vita2d_start_drawing();
    vita2d_clear_screen();

    if(video_get_resolution() == VIDEORESOLUTION_MAX) {
        /* independent x/y scale to fill the screen exactly */
        vita2d_draw_texture_scale(screen_texture, off_x, off_y, scale_x, scale_y);
    }
    else {
        vita2d_draw_texture_scale(screen_texture, off_x, off_y, scale, scale);
    }

    vita2d_end_drawing();
    vita2d_swap_buffers();
}



/* fade effects */

/*
 * fadefx_in()
 * Fade-in effect
 */
void fadefx_in(uint32 color, float seconds)
{
    if(fadefx_type == FADEFX_NONE) {
        fadefx_type = FADEFX_IN;
        fadefx_end = FALSE;
        fadefx_color = color;
        fadefx_elapsed_time = 0;
        fadefx_total_time = seconds;
    }
}


/*
 * fadefx_out()
 * Fade-out effect
 */
void fadefx_out(uint32 color, float seconds)
{
    if(fadefx_type == FADEFX_NONE) {
        fadefx_type = FADEFX_OUT;
        fadefx_end = FALSE;
        fadefx_color = color;
        fadefx_elapsed_time = 0;
        fadefx_total_time = seconds;
    }
}


/*
 * fadefx_over()
 * Asks if the fade effect has ended
 * (only one action when this event loops)
 */
int fadefx_over()
{
    return fadefx_end;
}


/*
 * fadefx_is_fading()
 * Is the fade effect ocurring?
 */
int fadefx_is_fading()
{
    return (fadefx_type != FADEFX_NONE);
}
