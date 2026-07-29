/*
 * font.c - font module
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
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include "font.h"
#include "../core/sprite.h"
#include "../core/video.h"
#include "../core/lang.h"
#include "../core/input.h"
#include "../core/stringutil.h"
#include "../core/logfile.h"

/* helper macro for detecting variable names */
#define IS_IDENTIFIER_CHAR(c)      ((c) != '\0' && ((isalnum((unsigned char)(c))) || ((c) == '_')))

/* font internal data */
#define FONT_MAX            10 /* how many fonts do we have? */
#define FONT_STACKCAPACITY  32
#define FONT_TEXTMAXLENGTH  20480
typedef struct {
    image_t *ch[256];
} fontdata_t;


/* private data */
static fontdata_t fontdata[FONT_MAX];
static void get_font_size(font_t *f, int *w, int *h);
static const char* get_variable(const char *key);
static int has_variables_to_expand(const char *str);
static void expand_variables(char *str);
static void render_char(image_t *dest, image_t *ch, int x, int y, uint32 color);
static uint8 hex2dec(char digit);


/*
 * font_init()
 * Initializes the font module
 */
void font_init()
{
    int i, j;
    char sheet[32];
    char *p, alphabet[FONT_MAX][1024] = {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789*.:!?",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789*.:!?",
        "0123456789:",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_´abcdefghijklmnopqrstuvwxyz{|}~\x7f\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff",
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_´abcdefghijklmnopqrstuvwxyz{|}~\200\201\202"
    };

    logfile_message("font_init()");
    for(i=0; i<FONT_MAX; i++) {
        for(j=0; j<256; j++)
            fontdata[i].ch[j] = NULL;

        sprintf(sheet, "FT_FONT%d", i);
        for(p=alphabet[i],j=0; *p; p++,j++)
            fontdata[i].ch[(int)*p] = sprite_get_image(sprite_get_animation(sheet, 0), j);
    }
    logfile_message("font_init() ok");
}



/*
 * font_create()
 * Creates a new font object
 */
font_t *font_create(int type)
{
    int i;
    font_t *f = mallocx(sizeof *f);

    f->type = clip(type, 0, FONT_MAX-1);
    f->text = NULL;
    f->width = 0;
    f->visible = TRUE;
    f->hspace = f->vspace = 1;
    for(i=0; i<FONT_MAXVALUES; i++)
        f->value[i] = 0;    

    return f;
}




/*
 * font_destroy()
 * Destroys an existing font object
 */
void font_destroy(font_t *f)
{
    if(f->text)
        free(f->text);

    free(f);
}




/*
 * font_set_text()
 * Sets the text...
 */
void font_set_text(font_t *f, const char *fmt, ...)
{
    static char buf[FONT_TEXTMAXLENGTH];
    va_list args;
    char *p, *q;

    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);

    while(has_variables_to_expand(buf))
        expand_variables(buf);

    if(f->text) free(f->text);
    f->text = mallocx(sizeof(char) * (strlen(buf) + 1));

    for(p=buf,q=f->text; *p; p++,q++) {
        if(*p == '\\') {
            switch( *(p+1) ) {
                case 'n':
                    *q = '\n';
                    p++;
                    break;

                case '\\':
                    *q = '\\';
                    p++;
                    break;

                default:
                    *q = *p;
                    break;
            }
        }
        else
            *q = *p;
    }

    *q = 0;
}



/*
 * font_get_text()
 * Returns the text
 */
char *font_get_text(font_t *f)
{
    return f->text ? f->text : "";
}



/*
 * font_set_width()
 * Sets the width of the font (useful if
 * you want wordwrap). If w == 0, then
 * there's no wordwrap.
 */
void font_set_width(font_t *f, int w)
{
    f->width = max(0, w);
}


/*
 * font_render()
 * Render
 */
void font_render(font_t *f, v2d_t camera_position)
{
    int offx = 0, offy = 0, w, h;
    char *p;
    image_t *ch;
    uint32 color[FONT_STACKCAPACITY];
    int i, top = 0;
    int wordwrap;

    color[top++] = image_rgb(255,255,255);
    get_font_size(f, &w, &h);
    if(f->visible && f->text) {
        for(p=f->text; *p; p++) {
            /* wordwrap */
            wordwrap = FALSE;
            if(p == f->text || (p != f->text && isspace((unsigned char)*(p-1)))) {
                char *q;
                int tag = FALSE;
                int wordlen = 0;

                for(q=p; !(*q=='\0' || isspace((unsigned char)*q)); q++) {
                    if(*q == '<') tag = TRUE;
                    if(!tag) wordlen++;
                    if(*q == '>') tag = FALSE;
                }

                wordwrap = ((f->width > 0) && ((offx + (w + f->hspace)*wordlen - f->hspace) > f->width));
            }

            /* tags */
            if(*p == '<') {

                if(strncmp(p+1, "color=", 6) == 0) {
                    char *orig = p;
                    uint8 r, g, b;
                    char tc;
                    int valid = TRUE;

                    p += 7;
                    for(i=0; i<6 && valid; i++) {
                        tc = tolower( *(p+i) );
                        valid = ((tc >= '0' && tc <= '9') || (tc >= 'a' && tc <= 'f'));
                    }
                    valid = valid && (*(p+6) == '>');

                    if(valid) {
                        r = (hex2dec(*(p+0)) << 4) | hex2dec(*(p+1));
                        g = (hex2dec(*(p+2)) << 4) | hex2dec(*(p+3));
                        b = (hex2dec(*(p+4)) << 4) | hex2dec(*(p+5));
                        p += 7;
                        if(top < FONT_STACKCAPACITY)
                            color[top++] = image_rgb(r,g,b);
                    }
                    else
                        p = orig;
                }

                if(strncmp(p+1, "/color>", 7) == 0) {
                    p += 8;
                    if(top >= 2) /* we must not clear the color stack */
                        top--;
                }

                if(!*p)
                    break;
            }

            /* printing text */
            if(wordwrap) { offx = 0; offy += h + f->vspace; }
            if(*p != '\n') {
                ch = fontdata[f->type].ch[(int)*p];
                if(ch)
                    render_char(video_get_backbuffer(), ch, (int)(f->position.x+offx-(camera_position.x-VIDEO_SCREEN_W/2)), (int)(f->position.y+offy-(camera_position.y-VIDEO_SCREEN_H/2)), color[top-1]);
                offx += w + f->hspace;
            }
            else {
                offx = 0;
                offy += h + f->vspace;
            }
        }
    }
}


/*
 * font_get_charsize()
 * Returns the size of any character of a given font
 */
v2d_t font_get_charsize(font_t *fnt)
{
    int w, h;
    get_font_size(fnt, &w, &h);
    return v2d_new(w,h);
}


/*
 * font_get_charspacing()
 * Returns the spacing between the characters of a given font
 */
v2d_t font_get_charspacing(font_t *f)
{
    return v2d_new(f->hspace, f->vspace);
}




/* private functions */

/* if you want to know the size of the given font... */
void get_font_size(font_t *f, int *w, int *h)
{
    int i;
    image_t *ch;

    *w = *h = 0;
    for(i=0; i<256; i++) {
        if(NULL != (ch=fontdata[f->type].ch[i])) {
            *w = ch->w;
            *h = ch->h;
            return;
        }
    }
}


/* returns a static char* (case insensitive search) */
const char* get_variable(const char *key)
{
    /* since our table is very small,
     * we can perform a linear search */
    static char tmp[1024];

    /* '$' symbol */
    if(str_icmp(key, "$") == 0 || str_icmp(key, "$$") == 0)
        return "$";

    /* special characters */
    if(str_icmp(key, "$LT") == 0)
        return "<";
    if(str_icmp(key, "$GT") == 0)
        return ">";

    /* input variables */
    if(str_icmp(key, "$INPUT_DIRECTIONAL") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_DIRECTIONAL" : "INPUT_KEYB_DIRECTIONAL", tmp, sizeof(tmp));
        return tmp;
    }
    if(str_icmp(key, "$INPUT_LEFT") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_LEFT" : "INPUT_KEYB_LEFT", tmp, sizeof(tmp));
        return tmp;
    }
    if(str_icmp(key, "$INPUT_RIGHT") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_RIGHT" : "INPUT_KEYB_RIGHT", tmp, sizeof(tmp));
        return tmp;
    }
    if(str_icmp(key, "$INPUT_UP") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_UP" : "INPUT_KEYB_UP", tmp, sizeof(tmp));
        return tmp;
    }
    if(str_icmp(key, "$INPUT_DOWN") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_DOWN" : "INPUT_KEYB_DOWN", tmp, sizeof(tmp));
        return tmp;
    }
    if(str_icmp(key, "$INPUT_FIRE1") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_FIRE1" : "INPUT_KEYB_FIRE1", tmp, sizeof(tmp));
        return tmp;
    }
    if(str_icmp(key, "$INPUT_FIRE2") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_FIRE2" : "INPUT_KEYB_FIRE2", tmp, sizeof(tmp));
        return tmp;
    }
    if(str_icmp(key, "$INPUT_FIRE3") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_FIRE3" : "INPUT_KEYB_FIRE3", tmp, sizeof(tmp));
        return tmp;
    }
    if(str_icmp(key, "$INPUT_FIRE4") == 0) {
        lang_getstring(input_joystick_available() ? "INPUT_JOY_FIRE4" : "INPUT_KEYB_FIRE4", tmp, sizeof(tmp));
        return tmp;
    }

    /* retrieving data from the language module */
    lang_getstring(key+1, tmp, sizeof(tmp));
    return tmp;
}


/* expands the variables, e.g.,
 * 1) Please press the $INPUT_LEFT to go left
 * 2) Please press the LEFT CTRL KEY to go left */
void expand_variables(char *str)
{
    static char buf[FONT_TEXTMAXLENGTH];
    static char varname[FONT_TEXTMAXLENGTH];
    char *p=str, *q=buf, *r;
    const char *u;

    while(*p) {
        /* looking for variables... */
        while(*p && *p != '$')
            *(q++) = *(p++);

        /* I found a variable! */
        if(*p == '$') {
            /* detect the name of this variable */
            r = varname;
            do {
                *(r++) = *(p++);
            } while(IS_IDENTIFIER_CHAR(*p));
            *r = 0;

            /* put it into buf */
            for(u=get_variable(varname); *u; u++,q++)
                *q = *u;
        }
    }
    *q = 0;

    strcpy(str, buf);
}


/* has_variables_to_expand() */
int has_variables_to_expand(const char *str)
{
    const char *p;

    for(p=str; *p; p++) {
        if(*p == '$' && IS_IDENTIFIER_CHAR(*(p+1)))
            return TRUE;
    }

    return FALSE;
}



/* render_char() */
void render_char(image_t *dest, image_t *ch, int x, int y, uint32 color)
{
    if(color == image_rgb(255,255,255))
        image_draw(ch, dest, x, y, IF_NONE);
    else {
        int l, c;
        uint8 r, g, b;
        uint8 cr, cg, cb;
        uint32 px, mask = video_get_maskcolor();

        image_color2rgb(color, &cr, &cg, &cb);
        for(l=0; l<ch->h; l++) {
            for(c=0; c<ch->w; c++) {
                px = image_getpixel(ch, c, l);
                if(px != mask) {
                    image_color2rgb(px, &r, &g, &b);
                    r &= cr; g &= cg; b &= cb;
                    image_putpixel(dest, x+c, y+l, image_rgb(r,g,b));
                }
            }
        }
    }
}

/* hex2dec() */
uint8 hex2dec(char digit)
{
    digit = tolower(digit);
    if(digit >= '0' && digit <= '9')
        return digit-'0';
    else if(digit >= 'a' && digit <= 'f')
        return (digit-'a')+10;
    else
        return 255; /* error */
}

