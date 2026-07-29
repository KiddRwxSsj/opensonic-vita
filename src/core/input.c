/*
 * input.c - input management
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
 * The Vita has exactly one physical controller and no keyboard, so
 * IT_KEYBOARD, IT_JOYSTICK and IT_USER all read the same sceCtrl state
 * (this mirrors upstream's own IT_USER, which already ORs keyboard and
 * joystick together). The analog stick is ORed into the d-pad directions
 * the same way upstream ORs the joystick axes in -- same idea, one more
 * physical source. IT_MOUSE has no real equivalent; it's only ever used
 * by the (mouse-driven, hence unreachable here) level editor, so it's
 * kept as a registered no-op device for source compatibility, with its
 * x/y reporting the front touch panel position in case that's ever
 * useful, and no buttons.
 *
 * input_joystick_available() always returns TRUE: unlike a desktop
 * build, there's no "no joystick detected" case to handle here.
 */

#include <stdlib.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include "input.h"
#include "util.h"
#include "video.h"
#include "logfile.h"
#include "timer.h"

/* available devices */
typedef enum input_device_t input_device_t;
enum input_device_t {
    IT_KEYBOARD,
    IT_MOUSE,
    IT_COMPUTER,
    IT_JOYSTICK,
    IT_USER
};

/* input structure (private) */
struct input_t {
    input_device_t type;
    int state[IB_MAX], oldstate[IB_MAX];
    int x, y, z;
    int dx, dy, dz;
    int keybmap[IB_MAX]; /* unused on this platform; kept for struct/API parity */
    int enabled;
    float howlong[IB_MAX];
};

typedef struct input_list_t input_list_t;
struct input_list_t {
    input_t *data;
    input_list_t *next;
};

/* private data */
static input_list_t *inlist;
static int ignore_joystick;
static SceCtrlData pad;
static SceTouchData touch;

#define STICK_DEADZONE 64 /* 0..127 distance from center (128) */

/* private methods */
static void input_register(input_t *in);
static void input_unregister(input_t *in);
static void poll_gamepad(int state[IB_MAX]);


/*
 * input_init()
 * Initializes the input module
 */
void input_init()
{
    logfile_message("input_init()");

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    inlist = NULL;
    ignore_joystick = FALSE;
}

/*
 * input_update()
 * Updates all the registered input objects
 */
void input_update()
{
    int i;
    float dt = timer_get_delta();
    int gamepad_state[IB_MAX];
    input_list_t *it;

    sceCtrlPeekBufferPositive(0, &pad, 1);
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    poll_gamepad(gamepad_state);

    for(it = inlist; it; it = it->next) {
        for(i = 0; i < IB_MAX; i++)
            it->data->oldstate[i] = it->data->state[i];

        for(i = 0; i < IB_MAX; i++) {
            if(input_button_down(it->data, i))
                it->data->howlong[i] += dt;
            else
                it->data->howlong[i] = 0.0;
        }

        switch(it->data->type) {
            case IT_KEYBOARD:
            case IT_JOYSTICK:
            case IT_USER: {
                if(!ignore_joystick) {
                    for(i = 0; i < IB_MAX; i++)
                        it->data->state[i] = gamepad_state[i];
                }
                else {
                    for(i = 0; i < IB_MAX; i++)
                        it->data->state[i] = FALSE;
                }
                break;
            }

            case IT_MOUSE: {
                it->data->x = (touch.reportNum > 0) ? (touch.report[0].x * VIDEO_SCREEN_W) / 1920 : it->data->x;
                it->data->y = (touch.reportNum > 0) ? (touch.report[0].y * VIDEO_SCREEN_H) / 1088 : it->data->y;
                for(i = 0; i < IB_MAX; i++)
                    it->data->state[i] = FALSE;
                break;
            }

            case IT_COMPUTER: {
                for(i = 0; i < IB_MAX; i++)
                    it->data->state[i] = FALSE;
                break;
            }
        }
    }
}


/*
 * input_release()
 * Releases the input module
 */
void input_release()
{
    input_list_t *it, *next;

    logfile_message("input_release()");
    for(it = inlist; it; it = next) {
        next = it->next;
        free(it->data);
        free(it);
    }
}


/*
 * input_button_down()
 * Checks if a given button is down
 */
int input_button_down(input_t *in, inputbutton_t button)
{
    return in->enabled ? in->state[(int)button] : FALSE;
}


/*
 * input_button_pressed()
 * Checks if a given button is pressed, not holded
 */
int input_button_pressed(input_t *in, inputbutton_t button)
{
    return in->enabled ? (in->state[(int)button] && !in->oldstate[(int)button]) : FALSE;
}


/*
 * input_button_up()
 * Checks if a given button is up
 */
int input_button_up(input_t *in, inputbutton_t button)
{
    return in->enabled ? (!in->state[(int)button] && in->oldstate[(int)button]) : FALSE;
}


/*
 * input_button_howlong()
 * For how long (in seconds) is [button] being holded?
 */
float input_button_howlong(input_t *in, inputbutton_t button)
{
    return in->enabled ? in->howlong[(int)button] : 0.0;
}


static input_t *new_input_device(input_device_t type)
{
    input_t *in = mallocx(sizeof *in);
    int i;

    in->type = type;
    in->enabled = TRUE;
    in->dx = in->dy = in->x = in->y = in->z = in->dz = 0;
    for(i = 0; i < IB_MAX; i++) {
        in->state[i] = in->oldstate[i] = FALSE;
        in->howlong[i] = 0.0;
        in->keybmap[i] = 0;
    }

    input_register(in);
    return in;
}

/*
 * input_create_keyboard()
 * There's no physical keyboard on this platform; this reads the same
 * gamepad state as input_create_user(). keybmap is accepted for API
 * compatibility and ignored.
 */
input_t *input_create_keyboard(int keybmap[])
{
    (void)keybmap;
    return new_input_device(IT_KEYBOARD);
}


/*
 * input_create_mouse()
 * Front touch panel stand-in; see the file header comment.
 */
input_t *input_create_mouse()
{
    return new_input_device(IT_MOUSE);
}


/*
 * input_create_computer()
 * Creates an object that receives "input" from
 * the computer
 */
input_t *input_create_computer()
{
    return new_input_device(IT_COMPUTER);
}


/*
 * input_create_joystick()
 * The Vita's gamepad is always present.
 */
input_t *input_create_joystick()
{
    return new_input_device(IT_JOYSTICK);
}


/*
 * input_create_user()
 * Creates an user's custom input device
 */
input_t *input_create_user()
{
    return new_input_device(IT_USER);
}


/*
 * input_destroy()
 * Destroys an input object
 */
void input_destroy(input_t *in)
{
    input_unregister(in);
    free(in);
}


/*
 * input_ignore()
 * Ignore Control
 */
void input_ignore(input_t *in)
{
    in->enabled = FALSE;
}


/*
 * input_restore()
 * Restore Control
 */
void input_restore(input_t *in)
{
    in->enabled = TRUE;
}


/*
 * input_is_ignored()
 * Returns TRUE if the input is ignored,
 * or FALSE otherwise
 */
int input_is_ignored(input_t *in)
{
    return !in->enabled;
}


/*
 * input_clear()
 * Clears all the input buttons
 */
void input_clear(input_t *in)
{
    int i;
    for(i = 0; i < IB_MAX; i++)
        in->state[i] = in->oldstate[i] = FALSE;
}


/*
 * input_simulate_button_down()
 * Useful for computer-controlled input objects
 */
void input_simulate_button_down(input_t *in, inputbutton_t button)
{
    in->state[(int)button] = TRUE;
}


/*
 * input_joystick_available()
 * The Vita's gamepad is always available.
 */
int input_joystick_available()
{
    return !ignore_joystick;
}


/*
 * input_ignore_joystick()
 * Ignores the input received from the gamepad
 */
void input_ignore_joystick(int ignore)
{
    ignore_joystick = ignore;
}


/*
 * input_is_joystick_ignored()
 * Is the joystick input ignored?
 */
int input_is_joystick_ignored()
{
    return ignore_joystick;
}


/*
 * input_get_xy()
 * Gets the xy coordinates (mouse/touch-related routine)
 */
v2d_t input_get_xy(input_t *in)
{
    return v2d_new(in->x, in->y);
}



/* private methods */

/* registers an input device */
static void input_register(input_t *in)
{
    input_list_t *node = mallocx(sizeof *node);

    node->data = in;
    node->next = inlist;
    inlist = node;
}

/* unregisters the given input device */
static void input_unregister(input_t *in)
{
    input_list_t *node, *next;

    if(inlist->data == in) {
        next = inlist->next;
        free(inlist);
        inlist = next;
    }
    else {
        node = inlist;
        while(node->next && node->next->data != in)
            node = node->next;
        if(node->next) {
            next = node->next->next;
            free(node->next);
            node->next = next;
        }
    }
}

/* reads the physical gamepad into an IB_MAX-sized button state array */
static void poll_gamepad(int state[IB_MAX])
{
    int lx = pad.lx - 128, ly = pad.ly - 128;

    state[IB_UP]    = (pad.buttons & SCE_CTRL_UP)    || (ly < -STICK_DEADZONE);
    state[IB_DOWN]  = (pad.buttons & SCE_CTRL_DOWN)  || (ly >  STICK_DEADZONE);
    state[IB_LEFT]  = (pad.buttons & SCE_CTRL_LEFT)  || (lx < -STICK_DEADZONE);
    state[IB_RIGHT] = (pad.buttons & SCE_CTRL_RIGHT) || (lx >  STICK_DEADZONE);
    state[IB_FIRE1] = (pad.buttons & SCE_CTRL_CROSS)    ? TRUE : FALSE;
    state[IB_FIRE2] = (pad.buttons & SCE_CTRL_CIRCLE)   ? TRUE : FALSE;
    state[IB_FIRE3] = (pad.buttons & SCE_CTRL_START)    ? TRUE : FALSE;
    state[IB_FIRE4] = (pad.buttons & SCE_CTRL_SELECT)   ? TRUE : FALSE;
}
