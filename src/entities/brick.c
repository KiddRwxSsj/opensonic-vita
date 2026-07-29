/*
 * brick.c - brick module
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
#include "brick.h"
#include "../core/global.h"
#include "../core/video.h"
#include "../core/stringutil.h"
#include "../core/logfile.h"
#include "../core/osspec.h"
#include "../core/util.h"
#include "../core/timer.h"
#include "../core/nanoparser/nanoparser.h"


/* private data */
#define BRKDATA_MAX                 10000 /* this engine supports up to BRKDATA_MAX bricks */
static int brickdata_count; /* size of brickdata and spritedata */
static brickdata_t* brickdata[BRKDATA_MAX]; /* brick data */

/* private functions */
static brickdata_t* brickdata_new();
static brickdata_t* brickdata_delete(brickdata_t *obj);
static void validate_brickdata(const brickdata_t *obj);
static int traverse(const parsetree_statement_t *stmt);
static int traverse_brick_attributes(const parsetree_statement_t *stmt, void *brickdata);


/* public functions */

/*
 * brickdata_load()
 * Loads all the brick data from a file
 */
void brickdata_load(const char *filename)
{
    int i;
    char abs_path[1024];
    parsetree_program_t *tree;

    logfile_message("brickdata_load('%s')", filename);
    resource_filepath(abs_path, filename, sizeof(abs_path), RESFP_READ);

    brickdata_count = 0;
    for(i=0; i<BRKDATA_MAX; i++) 
        brickdata[i] = NULL;

    tree = nanoparser_construct_tree(abs_path);
    nanoparser_traverse_program(tree, traverse);
    tree = nanoparser_deconstruct_tree(tree);

    if(brickdata_count == 0)
        fatal_error("FATAL ERROR: no bricks have been defined in \"%s\"", filename);

    logfile_message("brickdata_load('%s') ok!", filename);
}



/*
 * brickdata_unload()
 * Unloads brick data
 */
void brickdata_unload()
{
    int i;

    logfile_message("brickdata_unload()");

    for(i=0; i<brickdata_count; i++)
        brickdata[i] = brickdata_delete(brickdata[i]);
    brickdata_count = 0;

    logfile_message("brickdata_unload() ok");
}


/*
 * brickdata_get()
 * Gets a brickdata_t* object
 */
brickdata_t *brickdata_get(int id)
{
    id = clip(id, 0, brickdata_count-1);
    return brickdata[id];
}


/*
 * brickdata_size()
 * How many bricks are loaded?
 */
int brickdata_size()
{
    return brickdata_count;
}


/*
 * brick_image()
 * Returns the image of an (animated?) brick
 */
image_t *brick_image(brick_t *brk)
{
    return brk->brick_ref->image;
}


/*
 * brick_animate()
 * Animates a brick
 */
void brick_animate(brick_t *brk)
{
    spriteinfo_t *sprite = brk->brick_ref->data;

    if(sprite != NULL) { /* if brk is not a fake brick */
        int loop = sprite->animation_data[0]->repeat;
        int f, c = sprite->animation_data[0]->frame_count;

        /*brk->animation_frame += sprite->animation_data[0]->fps * timer_get_delta();
        if((int)brk->animation_frame >= c)
            brk->animation_frame = loop ? ((int)brk->animation_frame % c) : (c-1);*/

        if(!loop)
            brk->animation_frame = min(c-1, brk->animation_frame + sprite->animation_data[0]->fps * timer_get_delta());
        else
            brk->animation_frame = (int)(sprite->animation_data[0]->fps * (timer_get_ticks() * 0.001f)) % c;

        f = clip((int)brk->animation_frame, 0, c-1);
        brk->brick_ref->image = sprite->frame_data[ sprite->animation_data[0]->data[f] ];
    }
}





/* private functions */



/*
 * brick_get_property_name()
 * Returns the name of a given brick property
 */
const char* brick_get_property_name(int property)
{
    switch(property) {
        case BRK_NONE:
            return "PASSABLE";

        case BRK_OBSTACLE:
            return "OBSTACLE";

        case BRK_CLOUD:
            return "CLOUD";

        default:
            return "Unknown";
    }
}


/*
 * brick_get_behavior_name()
 * Returns the name of a given brick behavior
 */
const char* brick_get_behavior_name(int behavior)
{
    switch(behavior) {
        case BRB_DEFAULT:
            return "DEFAULT";

        case BRB_CIRCULAR:
            return "CIRCULAR";

        case BRB_BREAKABLE:
            return "BREAKABLE";

        case BRB_FALL:
            return "FALL";

        default:
            return "Unknown";
    }
}




/* === private stuff === */

brickdata_t* brickdata_new()
{
    int i;
    brickdata_t *obj = mallocx(sizeof *obj);

    obj->data = NULL;
    obj->image = NULL;
    obj->property = BRK_NONE;
    obj->angle = 0;
    obj->behavior = BRB_DEFAULT;
    obj->zindex = 0.5f;

    for(i=0; i<BRICKBEHAVIOR_MAXARGS; i++)
        obj->behavior_arg[i] = 0.0f;

    return obj;
}

brickdata_t* brickdata_delete(brickdata_t *obj)
{
    if(obj != NULL) {
        if(obj->data != NULL)
            spriteinfo_destroy(obj->data);
        free(obj);
    }

    return NULL;
}

void validate_brickdata(const brickdata_t *obj)
{
    if(obj->data == NULL)
        fatal_error("Can't load bricks: all bricks must have a sprite!");
}

int traverse(const parsetree_statement_t *stmt)
{
    const char *identifier;
    const parsetree_parameter_t *param_list;
    const parsetree_parameter_t *p1, *p2;
    int brick_id;

    identifier = nanoparser_get_identifier(stmt);
    param_list = nanoparser_get_parameter_list(stmt);

    if(str_icmp(identifier, "brick") == 0) {
        p1 = nanoparser_get_nth_parameter(param_list, 1);
        p2 = nanoparser_get_nth_parameter(param_list, 2);

        nanoparser_expect_string(p1, "Can't load bricks: brick number must be provided");
        nanoparser_expect_program(p2, "Can't load bricks: brick attributes must be provided");

        brick_id = atoi(nanoparser_get_string(p1));
        if(brick_id < 0 || brick_id >= BRKDATA_MAX)
            fatal_error("Can't load bricks: brick number must be in range 0..%d", BRKDATA_MAX-1);

        if(brickdata[brick_id] != NULL)
            brickdata[brick_id] = brickdata_delete(brickdata[brick_id]);

        brickdata_count = max(brickdata_count, brick_id+1);
        brickdata[brick_id] = brickdata_new();
        nanoparser_traverse_program_ex(nanoparser_get_program(p2), (void*)brickdata[brick_id], traverse_brick_attributes);
        validate_brickdata(brickdata[brick_id]);
        brickdata[brick_id]->image = brickdata[brick_id]->data->frame_data[0];
    }
    else
        fatal_error("Can't load bricks: unknown identifier '%s'", identifier);

    return 0;
}

int traverse_brick_attributes(const parsetree_statement_t *stmt, void *brickdata)
{
    const char *identifier;
    const parsetree_parameter_t *param_list;
    const parsetree_parameter_t *p1, *pj;
    brickdata_t *dat = (brickdata_t*)brickdata;
    const char *type;
    int j;

    identifier = nanoparser_get_identifier(stmt);
    param_list = nanoparser_get_parameter_list(stmt);

    if(str_icmp(identifier, "type") == 0) {
        p1 = nanoparser_get_nth_parameter(param_list, 1);
        nanoparser_expect_string(p1, "Can't read brick attributes: must specify brick type");
        type = nanoparser_get_string(p1);

        if(str_icmp(type, "OBSTACLE") == 0)
            dat->property = BRK_OBSTACLE;
        else if(str_icmp(type, "PASSABLE") == 0)
            dat->property = BRK_NONE;
        else if(str_icmp(type, "CLOUD") == 0)
            dat->property = BRK_CLOUD;
        else
            fatal_error("Can't read brick attributes: unknown brick type '%s'", type);
    }
    else if(str_icmp(identifier, "behavior") == 0) {
        p1 = nanoparser_get_nth_parameter(param_list, 1);
        nanoparser_expect_string(p1, "Can't read brick attributes: must specify brick behavior");
        type = nanoparser_get_string(p1);

        if(str_icmp(type, "DEFAULT") == 0)
            dat->behavior = BRB_DEFAULT;
        else if(str_icmp(type, "CIRCULAR") == 0)
            dat->behavior = BRB_CIRCULAR;
        else if(str_icmp(type, "BREAKABLE") == 0)
            dat->behavior = BRB_BREAKABLE;
        else if(str_icmp(type, "FALL") == 0)
            dat->behavior = BRB_FALL;
        else
            fatal_error("Can't read brick attributes: unknown brick type '%s'", type);

        for(j=0; j<BRICKBEHAVIOR_MAXARGS; j++) {
            pj = nanoparser_get_nth_parameter(param_list, 2+j);
            dat->behavior_arg[j] = atof(nanoparser_get_string(pj));
        }
    }
    else if(str_icmp(identifier, "angle") == 0) {
        p1 = nanoparser_get_nth_parameter(param_list, 1);
        nanoparser_expect_string(p1, "Can't read brick attributes: must specify brick angle, a number between 0 and 359");
        dat->angle = ((atoi(nanoparser_get_string(p1)) % 360) + 360) % 360;
    }
    else if(str_icmp(identifier, "zindex") == 0) {
        p1 = nanoparser_get_nth_parameter(param_list, 1);
        nanoparser_expect_string(p1, "Can't read brick attributes: zindex must be a number between 0.0 and 1.0");
        dat->zindex = clip(atof(nanoparser_get_string(p1)), 0.0f, 1.0f);
    }
    else if(str_icmp(identifier, "sprite") == 0) {
        p1 = nanoparser_get_nth_parameter(param_list, 1);
        nanoparser_expect_program(p1, "Can't read brick attributes: a sprite block must be specified");
        if(dat->data != NULL)
            spriteinfo_destroy(dat->data);
        dat->data = spriteinfo_create(nanoparser_get_program(p1));
    }
    else
        fatal_error("Can't read brick attributes: unkown identifier '%s'", identifier);

    return 0;
}

