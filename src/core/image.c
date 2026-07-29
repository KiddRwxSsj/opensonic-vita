/*
 * image.c - image implementation
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
 * All of the game's shipped artwork is PNG (see images/*.png), so the
 * BMP/PCX/TGA/JPG loaders that Allegro's load_bitmap() pulled in for
 * desktop builds are not needed here and are not ported. Everything is
 * decoded straight to a 32-bit RGBA surface with libpng.
 *
 * Sprites use color-key transparency (magenta, matching Allegro's
 * default MASK_COLOR_32), not real alpha -- this mirrors the original
 * engine exactly, including the maskcolor_bugfix() workaround for PNGs
 * whose transparent pixels weren't stored as exact magenta.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <png.h>
#include "image.h"
#include "video.h"
#include "stringutil.h"
#include "logfile.h"
#include "osspec.h"
#include "resourcemanager.h"
#include "util.h"

/* useful macros */
#define IS_PNG(path) (str_icmp((path)+strlen(path)-4, ".png") == 0)

/* color key used for sprite transparency (opaque magenta) */
#define MASKCOLOR image_rgb(255, 0, 255)

/* private stuff */
static image_t *png_load(const char *path);
static void png_save(const image_t *img, const char *path);
static inline int in_bounds(const image_t *img, int x, int y);

/*
 * image_load()
 * Loads a image from a file.
 * Supported types: PNG
 */
image_t *image_load(const char *path)
{
    char abs_path[1024];
    image_t *img;

    if(NULL == (img = resourcemanager_find_image(path))) {
        resource_filepath(abs_path, path, sizeof(abs_path), RESFP_READ);
        logfile_message("image_load(%s)", abs_path);

        img = png_load(abs_path);
        if(img == NULL) {
            logfile_message("image_load() error: couldn't load %s", abs_path);
            return NULL;
        }

        resourcemanager_add_image(path, img);
        resourcemanager_ref_image(path);

        logfile_message("image_load() ok");
    }
    else
        resourcemanager_ref_image(path);

    return img;
}

/*
 * image_unref()
 * Will try to release the resource from
 * the memory. You will call this if, and
 * only if, you are sure you don't need the
 * resource anymore (i.e., you're not holding
 * any pointers to it)
 */
int image_unref(const char *path)
{
    return resourcemanager_unref_image(path);
}

/*
 * image_save()
 * Saves a image to a file
 */
void image_save(const image_t *img, const char *path)
{
    char abs_path[1024];

    resource_filepath(abs_path, path, sizeof(abs_path), RESFP_WRITE);
    logfile_message("image_save(%p,%s)", img, abs_path);

    png_save(img, abs_path);
}

/*
 * image_create()
 * Creates a new image of a given size
 */
image_t *image_create(int width, int height)
{
    image_t *img = mallocx(sizeof *img);

    width = max(width, 1);
    height = max(height, 1);

    img->w = width;
    img->h = height;
    img->pixels = mallocx(sizeof(uint32) * width * height);
    image_clear(img, image_rgb(0, 0, 0));

    return img;
}

/*
 * image_destroy()
 * Destroys an image. This is called automatically
 * while unloading the resource manager.
 */
void image_destroy(image_t *img)
{
    if(img->pixels != NULL) {
        free(img->pixels);
        img->pixels = NULL;
    }

    free(img);
}

/*
 * image_getpixel()
 * Returns the pixel at the given position on the image
 */
uint32 image_getpixel(const image_t *img, int x, int y)
{
    if(!in_bounds(img, x, y))
        return 0;

    return img->pixels[y * img->w + x];
}

/*
 * image_putpixel()
 * Plots a pixel into the given image
 */
void image_putpixel(image_t *img, int x, int y, uint32 color)
{
    if(in_bounds(img, x, y))
        img->pixels[y * img->w + x] = color;
}

/*
 * image_line()
 * Draws a line from (x1,y1) to (x2,y2) using the specified color
 */
void image_line(image_t *img, int x1, int y1, int x2, int y2, uint32 color)
{
    int dx = abs(x2 - x1), dy = -abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy, e2;

    for(;;) {
        image_putpixel(img, x1, y1, color);
        if(x1 == x2 && y1 == y2)
            break;
        e2 = 2 * err;
        if(e2 >= dy) { err += dy; x1 += sx; }
        if(e2 <= dx) { err += dx; y1 += sy; }
    }
}

/*
 * image_ellipse()
 * Draws an ellipse with the specified centre, radius and color
 */
void image_ellipse(image_t *img, int cx, int cy, int radius_x, int radius_y, uint32 color)
{
    int steps = 4 * (radius_x + radius_y);
    int i;
    float prevx = 0, prevy = 0;

    if(steps <= 0)
        return;

    for(i = 0; i <= steps; i++) {
        float t = (2.0f * PI * i) / steps;
        float x = cx + radius_x * cosf(t);
        float y = cy + radius_y * sinf(t);
        if(i > 0)
            image_line(img, (int)prevx, (int)prevy, (int)x, (int)y, color);
        prevx = x;
        prevy = y;
    }
}

/*
 * image_rectfill()
 * Draws a filled rectangle
 */
void image_rectfill(image_t *img, int x1, int y1, int x2, int y2, uint32 color)
{
    int x, y, tmp;

    if(x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
    if(y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }

    x1 = max(x1, 0); y1 = max(y1, 0);
    x2 = min(x2, img->w - 1); y2 = min(y2, img->h - 1);

    for(y = y1; y <= y2; y++) {
        uint32 *row = img->pixels + y * img->w;
        for(x = x1; x <= x2; x++)
            row[x] = color;
    }
}

/*
 * image_rgb()
 * Generates an uint32 color
 */
uint32 image_rgb(uint8 r, uint8 g, uint8 b)
{
    return (uint32)r | ((uint32)g << 8) | ((uint32)b << 16) | ((uint32)0xFF << 24);
}

/*
 * image_color2rgb()
 * Converts an uint32 to a (r,g,b) triple
 */
void image_color2rgb(uint32 color, uint8 *r, uint8 *g, uint8 *b)
{
    if(r) *r = (uint8)(color & 0xFF);
    if(g) *g = (uint8)((color >> 8) & 0xFF);
    if(b) *b = (uint8)((color >> 16) & 0xFF);
}

/*
 * image_clear()
 * Clears an given image with some color
 */
void image_clear(image_t *img, uint32 color)
{
    int i, count = img->w * img->h;

    for(i = 0; i < count; i++)
        img->pixels[i] = color;
}

/*
 * image_blit()
 * Blits a surface onto another (opaque copy, no masking; matches Allegro's blit())
 */
void image_blit(const image_t *src, image_t *dest, int source_x, int source_y, int dest_x, int dest_y, int width, int height)
{
    int x, y;

    if(width <= 0 || height <= 0)
        return;

    for(y = 0; y < height; y++) {
        int sy = source_y + y, dy = dest_y + y;
        if(sy < 0 || sy >= src->h || dy < 0 || dy >= dest->h)
            continue;
        for(x = 0; x < width; x++) {
            int sx = source_x + x, dx = dest_x + x;
            if(sx < 0 || sx >= src->w || dx < 0 || dx >= dest->w)
                continue;
            dest->pixels[dy * dest->w + dx] = src->pixels[sy * src->w + sx];
        }
    }
}

/*
 * image_draw()
 * Draws an image onto the destination surface
 * at the specified position (color-key transparency, like draw_sprite())
 *
 * flags: refer to the IF_* defines (Image Flags)
 */
void image_draw(const image_t *src, image_t *dest, int x, int y, uint32 flags)
{
    int i, j;

    /* Reject degenerate/out-of-range placements *before* doing any
     * arithmetic on x/y below. This is not just an optimization: if x or y
     * is close to INT_MIN, "-y"/"-x" a few lines down overflow (UB, and on
     * this target it wraps back to INT_MIN instead of a large positive
     * value), which defeats the "j1<=j0 || i1<=i0" early-out and lets the
     * draw loop run with wild, out-of-bounds indices -> memory corruption
     * / SIGSEGV. Bailing out here whenever the sprite cannot possibly
     * overlap dest keeps the numbers small and makes the overflow
     * impossible, regardless of how x/y got corrupted upstream. */
    if(x <= -src->w || x >= dest->w || y <= -src->h || y >= dest->h)
        return;

    /* clip range (once), instead of discarding pixel by pixel inside the loop */
    int j0 = (y < 0) ? -y : 0;
    int j1 = (y + src->h > dest->h) ? (dest->h - y) : src->h;
    int i0 = (x < 0) ? -x : 0;
    int i1 = (x + src->w > dest->w) ? (dest->w - x) : src->w;

    /* tile completely off-screen: bail out immediately, O(1) */
    if(j1 <= j0 || i1 <= i0)
        return;

if(src->pixels == NULL || x < -10000 || x > 10000 || y < -10000 || y > 10000) {
    logfile_message("image_draw(): src=%p pixels=%p w=%d h=%d | dest=%p w=%d h=%d | x=%d y=%d flags=%u",
        (void*)src, (void*)src->pixels, src->w, src->h,
        (void*)dest, dest->w, dest->h, x, y, flags);
}

    for(j = j0; j < j1; j++) {
        int dy = y + j;
        int sy = (flags & IF_VFLIP) ? (src->h - 1 - j) : j;
        const uint32 *srow = src->pixels + sy * src->w;
        uint32 *drow = dest->pixels + dy * dest->w;

        if(!(flags & IF_HFLIP)) {
            for(i = i0; i < i1; i++) {
                uint32 pixel = srow[i];
                if(pixel != MASKCOLOR)
                    drow[x + i] = pixel;
            }
        }
        else {
            for(i = i0; i < i1; i++) {
                uint32 pixel = srow[src->w - 1 - i];
                if(pixel != MASKCOLOR)
                    drow[x + i] = pixel;
            }
        }
    }
}

/*
 * image_draw_scaled()
 * Draws a scaled image onto the destination surface
 * at the specified position
 *
 * scale: (1.0, 1.0) is the original size
 *        (2.0, 2.0) stands for a double-sized image
 *        (0.5, 0.5) stands for a smaller image
 */
void image_draw_scaled(const image_t *src, image_t *dest, int x, int y, v2d_t scale, uint32 flags)
{
    int w = max((int)(scale.x * src->w), 1);
    int h = max((int)(scale.y * src->h), 1);
    int i, j;

    for(j = 0; j < h; j++) {
        int dy = y + j;
        int sy = (int)((j * src->h) / h);

        if(dy < 0 || dy >= dest->h)
            continue;
        if(flags & IF_VFLIP)
            sy = src->h - 1 - sy;

        for(i = 0; i < w; i++) {
            int dx = x + i;
            int sx = (int)((i * src->w) / w);
            uint32 pixel;

            if(dx < 0 || dx >= dest->w)
                continue;
            if(flags & IF_HFLIP)
                sx = src->w - 1 - sx;

            pixel = src->pixels[sy * src->w + sx];
            if(pixel != MASKCOLOR)
                dest->pixels[dy * dest->w + dx] = pixel;
        }
    }
}

/*
 * image_draw_rotated()
 * Draws a rotated image onto the destination bitmap at the specified position
 *
 * ang: angle given in radians
 * cx, cy: pivot positions (in src's coordinate space)
 *
 * Implemented as an inverse-mapped rotation around the (x,y) placement of
 * the pivot: for every destination pixel that could be covered, we map it
 * back into src space and sample it. This produces the same visual result
 * as Allegro's pivot_sprite() without needing its fixed-point angle units.
 *
 * The sign of s below is deliberately the negative of sinf(ang): the rest
 * of the engine (actor_handle_floor_collision(), the foot sensors, the
 * rotated bounding boxes, etc.) rotates offsets with v2d_rotate(v, -angle),
 * which expands to (x*cos+y*sin, y*cos-x*sin). To make the drawn sprite
 * turn the same way as those hitboxes -- instead of the opposite way,
 * which is what caused the sprite to look upside-down/sideways on loops
 * and springs -- the forward mapping implemented here must match that same
 * -angle rotation. Flipping the sign of s is what makes that hold; see the
 * derivation notes in the port log if this ever needs to be re-checked.
 */
void image_draw_rotated(const image_t *src, image_t *dest, int x, int y, int cx, int cy, float ang, uint32 flags)
{
    float c = cosf(ang), s = -sinf(ang);
    int radius = (int)(sqrtf((float)(src->w * src->w + src->h * src->h)) + 1);
    int minx = max(x - radius, 0), maxx = min(x + radius, dest->w - 1);
    int miny = max(y - radius, 0), maxy = min(y + radius, dest->h - 1);

    /* the pivot must live in the same (possibly mirrored) space that we
       sample back into below -- otherwise off-center hot spots (cx != w/2)
       rotate around the wrong point whenever IF_HFLIP/IF_VFLIP is set */
    int pivot_cx = (flags & IF_HFLIP) ? (src->w - 1 - cx) : cx;
    int pivot_cy = (flags & IF_VFLIP) ? (src->h - 1 - cy) : cy;
    int px, py;

    for(py = miny; py <= maxy; py++) {
        for(px = minx; px <= maxx; px++) {
            /* undo the rotation to find where this dest pixel came from in src space */
            float rx = (float)(px - x);
            float ry = (float)(py - y);
            float sxf =  c * rx + s * ry + pivot_cx;
            float syf = -s * rx + c * ry + pivot_cy;
            int sx = (int)floorf(sxf + 0.5f);
            int sy = (int)floorf(syf + 0.5f);
            uint32 pixel;

            if(sx < 0 || sx >= src->w || sy < 0 || sy >= src->h)
                continue;

            if(flags & IF_HFLIP)
                sx = src->w - 1 - sx;
            if(flags & IF_VFLIP)
                sy = src->h - 1 - sy;

            pixel = src->pixels[sy * src->w + sx];
            if(pixel != MASKCOLOR)
                dest->pixels[py * dest->w + px] = pixel;
        }
    }
}

/*
 * image_draw_trans()
 * Draws a translucent image
 *
 * color: unused for the RGB channels here, same as upstream -- Allegro's
 * set_trans_blender() only honors the alpha channel for truecolor
 * bitmaps, the r/g/b arguments only affect 8-bit palette blending, which
 * this engine never uses (see setup_color_depth() in the desktop build).
 *
 * alpha: 0.0 (invisible) <= alpha <= 1.0 (opaque)
 */
void image_draw_trans(const image_t *src, image_t *dest, int x, int y, uint32 color, float alpha, uint32 flags)
{
    int i, j;
    (void)color;

    alpha = clip(alpha, 0.0f, 1.0f);

    for(j = 0; j < src->h; j++) {
        int dy = y + j;
        if(dy < 0 || dy >= dest->h)
            continue;

        for(i = 0; i < src->w; i++) {
            int dx = x + i;
            int sx = (flags & IF_HFLIP) ? (src->w - 1 - i) : i;
            int sy = (flags & IF_VFLIP) ? (src->h - 1 - j) : j;
            uint32 spixel, dpixel, out;
            uint8 sr, sg, sb, dr, dg, db;

            if(dx < 0 || dx >= dest->w)
                continue;

            spixel = src->pixels[sy * src->w + sx];
            if(spixel == MASKCOLOR)
                continue;

            dpixel = dest->pixels[dy * dest->w + dx];
            image_color2rgb(spixel, &sr, &sg, &sb);
            image_color2rgb(dpixel, &dr, &dg, &db);

            out = image_rgb(
                (uint8)(dr + (sr - dr) * alpha),
                (uint8)(dg + (sg - dg) * alpha),
                (uint8)(db + (sb - db) * alpha)
            );
            dest->pixels[dy * dest->w + dx] = out;
        }
    }
}

/*
 * image_pixelperfect_collision()
 * Pixel perfect collision detection
 */
int image_pixelperfect_collision(const image_t *img1, const image_t *img2, int x1, int y1, int x2, int y2)
{
    int i, j;

    /* optimizing */
    if(img1->w * img1->h > img2->w * img2->h)
        return image_pixelperfect_collision(img2, img1, x2, y2, x1, y1);

    for(i = 0; i < img1->h; i++) {
        for(j = 0; j < img1->w; j++) {
            if(img1->pixels[i * img1->w + j] != MASKCOLOR) {
                int qx = x1 + j, qy = y1 + i;
                if(qx >= x2 && qx < x2 + img2->w && qy >= y2 && qy < y2 + img2->h) {
                    if(img2->pixels[(qy - y2) * img2->w + (qx - x2)] != MASKCOLOR)
                        return TRUE;
                }
            }
        }
    }

    return FALSE;
}

/* private methods */

static inline int in_bounds(const image_t *img, int x, int y)
{
    return x >= 0 && x < img->w && y >= 0 && y < img->h;
}

/*
 * png_load()
 * Decodes a PNG file straight into an RGBA8888 image_t.
 * Fully transparent pixels (alpha == 0) are forced to MASKCOLOR so that
 * the rest of the engine's color-key based drawing works exactly as it
 * did against Allegro's masked truecolor bitmaps.
 */
static image_t *png_load(const char *path)
{
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_uint_32 width, height;
    int bit_depth, color_type;
    png_bytep *row_pointers;
    image_t *img;
    int x, y;

    fp = fopen(path, "rb");
    if(fp == NULL)
        return NULL;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(png_ptr == NULL) {
        fclose(fp);
        return NULL;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if(info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    if(setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

    /* normalize everything to 8-bit RGBA */
    if(color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);
    if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);
    if(bit_depth == 16)
        png_set_strip_16(png_ptr);
    if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);
    if(color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);

    img = mallocx(sizeof *img);
    img->w = (int)width;
    img->h = (int)height;
    img->pixels = mallocx(sizeof(uint32) * width * height);

    row_pointers = mallocx(sizeof(png_bytep) * height);
    for(y = 0; y < (int)height; y++)
        row_pointers[y] = (png_bytep)(img->pixels + y * width);

    png_read_image(png_ptr, row_pointers);
    png_read_end(png_ptr, NULL);

    free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    /* force fully-transparent pixels to the exact color key */
    for(y = 0; y < img->h; y++) {
        for(x = 0; x < img->w; x++) {
            uint32 *p = &img->pixels[y * img->w + x];
            uint8 a = (uint8)((*p >> 24) & 0xFF);
            if(a == 0)
                *p = MASKCOLOR;
            else
                *p |= (uint32)0xFF << 24; /* the engine has no true alpha blending outside image_draw_trans() */
        }
    }

    return img;
}

/*
 * png_save()
 * Encodes an RGBA8888 image_t to a PNG file. Used by the screenshot
 * feature (see screenshot.c).
 */
static void png_save(const image_t *img, const char *path)
{
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;
    int y;

    if(!IS_PNG(path)) {
        logfile_message("png_save(): only PNG output is supported on this platform (%s)", path);
        return;
    }

    fp = fopen(path, "wb");
    if(fp == NULL) {
        logfile_message("png_save(): couldn't open %s for writing", path);
        return;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(png_ptr == NULL) {
        fclose(fp);
        return;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if(info_ptr == NULL) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return;
    }

    if(setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, img->w, img->h, 8, PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    row_pointers = mallocx(sizeof(png_bytep) * img->h);
    for(y = 0; y < img->h; y++) {
        int x;
        png_bytep row = mallocx(3 * img->w);
        for(x = 0; x < img->w; x++) {
            uint32 pixel = img->pixels[y * img->w + x];
            row[x*3+0] = (uint8)(pixel & 0xFF);
            row[x*3+1] = (uint8)((pixel >> 8) & 0xFF);
            row[x*3+2] = (uint8)((pixel >> 16) & 0xFF);
        }
        row_pointers[y] = row;
    }

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    for(y = 0; y < img->h; y++)
        free(row_pointers[y]);
    free(row_pointers);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}


