/*
 * level.c - code for the game levels
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

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <psp2/kernel/processmgr.h>
#include "level.h"
#include "confirmbox.h"
#include "gameover.h"
#include "pause.h"
#include "quest.h"
#include "../core/scene.h"
#include "../core/storyboard.h"
#include "../core/global.h"
#include "../core/input.h"
#include "../core/video.h"
#include "../core/audio.h"
#include "../core/timer.h"
#include "../core/sprite.h"
#include "../core/osspec.h"
#include "../core/stringutil.h"
#include "../core/logfile.h"
#include "../core/lang.h"
#include "../core/soundfactory.h"
#include "../core/nanoparser/nanoparser.h"
#include "../entities/brick.h"
#include "../entities/player.h"
#include "../entities/item.h"
#include "../entities/enemy.h"
#include "../entities/font.h"
#include "../entities/boss.h"
#include "../entities/camera.h"
#include "../entities/background.h"
#include "../entities/items/flyingtext.h"
#include "util/editorgrp.h"



/* ------------------------
 * Particles
 * ------------------------ */
/* structure */
typedef struct {
    image_t *image;
    v2d_t position;
    v2d_t speed;
    int destroy_on_brick;
} particle_t;

/* linked list of particle_ts */
typedef struct particle_list_t {
    particle_t *data;
    struct particle_list_t *next;
} particle_list_t;

/* internal methods */
static void particle_init();
static void particle_release();
static void particle_update_all(brick_list_t *brick_list);
static void particle_render_all();



/* ------------------------
 * Dialog Regions
 *
 * If the player gets inside
 * these regions, a dialog
 * box appears
 * ------------------------ */
/* constants */
#define DIALOGREGION_MAX 100

/* structure */
typedef struct {
    int rect_x, rect_y, rect_w, rect_h; /* region data */
    char title[128], message[1024]; /* dialog box data */
    int disabled; /* TRUE if this region is disabled */
} dialogregion_t;

/* internal data */
static int dialogregion_size; /* size of the vector */
static dialogregion_t dialogregion[DIALOGREGION_MAX];

/* internal methods */
static void update_dialogregions();



/* ------------------------
 * Level
 * ------------------------ */
/* constants */
#define DEFAULT_MARGIN          VIDEO_SCREEN_W
#define ACTCLEAR_BONUSMAX       3 /* ring bonus, secret bonus, total */
#define MAX_POWERUPS            10
#define DLGBOX_MAXTIME          7000

/* level attributes */
static char file[1024];
static char name[1024];
static char musicfile[1024];
static char theme[1024];
static char bgtheme[1024];
static char grouptheme[1024];
static char author[1024];
static char version[1024];
static int act; /* 1 <= act <= 3 */
static int requires[3]; /* this level requires engine version x.y.z */
static int readonly; /* we can't activate the level editor */

/* internal data */
static float gravity;
static int level_width;/* width of this level (in pixels) */
static int level_height; /* height of this level (in pixels) */
static float level_timer;
static brick_list_t *brick_list;
static item_list_t *item_list;
static enemy_list_t *enemy_list;
static particle_list_t *particle_list;
static v2d_t spawn_point;
static music_t *music;
static sound_t *override_music;
static int block_music;
static int quit_level;
static image_t *quit_level_img;
static bgtheme_t *backgroundtheme;

/* player data */
static player_t *team[3]; /* players */
static player_t *player; /* reference to the current player */
static int player_id; /* current player id (0, 1 or 2) */

/* camera */
static actor_t *camera_focus;

/* boss */
static boss_t *boss;
static int player_inside_boss_area;
static int boss_fight_activated;

/* gui / hud */
static actor_t *maingui, *lifegui;
static font_t *lifefnt, *mainfnt[3];

/* end of act (reached the goal) */
static int level_cleared;
static uint32 actclear_starttime, actclear_endtime, actclear_sampletimer;
static int actclear_prepare_next_level, actclear_goto_next_level, actclear_played_song;
static float actclear_ringbonus, actclear_secretbonus, actclear_totalbonus;
static font_t *actclear_teamname, *actclear_gotthrough, *actclear_bonusfnt[ACTCLEAR_BONUSMAX];
static actor_t *actclear_levelact, *actclear_bonus[ACTCLEAR_BONUSMAX];

/* opening animation */
static actor_t *levelop, *levelact;
static font_t *leveltitle;

/* dialog box */
static int dlgbox_active;
static uint32 dlgbox_starttime;
static actor_t *dlgbox;
static font_t *dlgbox_title, *dlgbox_message;

/* level management */
static void level_load(const char *filepath);
static void level_unload();
static void level_save(const char *filepath);
static int traverse_level(const parsetree_statement_t* stmt);

/* internal methods */
static void render_entities(); /* render bricks, items, enemies, players, etc. */
static void render_hud(); /* renders the hud */
static int got_boss(); /* does this level have a boss? */
static void brick_move(brick_t *brick); /* moveable platforms */
static int inside_screen(int x, int y, int w, int h, int margin);
static brick_list_t* brick_list_clip();
static item_list_t* item_list_clip();
static void brick_list_unclip(brick_list_t *list);
static void item_list_unclip(item_list_t *list);
static int get_brick_id(brick_t *b);
static int brick_sort_cmp(brick_t *a, brick_t *b);
static void insert_brick_sorted(brick_list_t *b);
static brick_t *create_fake_brick(int width, int height, v2d_t position, int angle);
static void destroy_fake_brick(brick_t *b);
static void update_level_size();
static void restart();
static void render_players(int bring_to_back);
static void update_music();
static void spawn_players();
static void remove_dead_bricks();
static void remove_dead_items();
static void remove_dead_objects();
static void render_powerups(); /* gui / hud related */
static void update_dlgbox(); /* dialog boxes */
static void render_dlgbox(); /* dialog boxes */



/* ------------------------
 * Level Editor
 * ------------------------ */
/* constants */
#define EDITOR_BGFILE       "images/editorbg.png"

/* methods */
static void editor_init();
static void editor_release();
static void editor_enable();
static void editor_disable();
static void editor_update();
static void editor_render();
static int editor_is_enabled();
static int editor_want_to_activate();
static void editor_render_background();
static void editor_save();
static void editor_scroll();

/* object type */
enum editor_object_type {
    EDT_BRICK,
    EDT_ITEM,
    EDT_ENEMY,
    EDT_GROUP
};
#define EDITORGRP_ENTITY_TO_EDT(t) \
                (t == EDITORGRP_ENTITY_BRICK) ? EDT_BRICK : \
                (t == EDITORGRP_ENTITY_ITEM) ? EDT_ITEM : \
                EDT_ENEMY;



/* internal stuff */
static int editor_enabled; /* is the level editor enabled? */
/*
 * editor_keybmap[] / editor_keybmap2[]
 * Dead data on this port: these are fed to input_create_keyboard(),
 * whose Vita implementation (input.c) ignores the keybmap[] argument
 * entirely -- there's no physical keyboard on this platform, so it just
 * returns a generic gamepad-backed input device regardless of what's
 * passed in. On top of that, the level editor these devices belong to
 * is mouse-driven and unreachable on Vita (see PROGRESS.md: "Level
 * editor is unreachable, not removed"). The 0 placeholders below simply
 * replace the undefined Allegro KEY_* constants so this compiles; they
 * carry no meaning and are never read.
 */
static int editor_keybmap[] = {
    0, 0, 0, 0,                             /* directional keys */
    0,                                       /* fire 1 */
    0,                                       /* fire 2 */
    0,                                       /* fire 3 */
    0                                        /* fire 4 */
};
int editor_keybmap2[] = {
    0, 0, 0, 0,
    0,
    0,
    0,
    0
};
static int editor_previous_video_resolution;
static int editor_previous_video_smooth;
static image_t *editor_bgimage;
static input_t *editor_mouse;
static input_t *editor_keyboard, *editor_keyboard2;
static v2d_t editor_camera, editor_cursor;
static enum editor_object_type editor_cursor_objtype;
static int editor_cursor_objid, editor_cursor_itemid;
static font_t *editor_cursor_font;
static font_t *editor_properties_font;
static const char* editor_object_category(enum editor_object_type objtype);
static const char* editor_object_info(enum editor_object_type objtype, int objid);
static void editor_next_category();
static void editor_previous_category();
static void editor_next_object();
static void editor_previous_object();
static int editor_item_list[] = {
    IT_RING, IT_LIFEBOX, IT_RINGBOX, IT_STARBOX, IT_SPEEDBOX, IT_GLASSESBOX, IT_TRAPBOX,
    IT_SHIELDBOX, IT_FIRESHIELDBOX, IT_THUNDERSHIELDBOX, IT_WATERSHIELDBOX,
    IT_ACIDSHIELDBOX, IT_WINDSHIELDBOX,
    IT_LOOPRIGHT, IT_LOOPMIDDLE, IT_LOOPLEFT, IT_LOOPNONE,
    IT_YELLOWSPRING, IT_BYELLOWSPRING, IT_RYELLOWSPRING, IT_LYELLOWSPRING,
    IT_TRYELLOWSPRING, IT_TLYELLOWSPRING, IT_BRYELLOWSPRING, IT_BLYELLOWSPRING,
    IT_REDSPRING, IT_BREDSPRING, IT_RREDSPRING, IT_LREDSPRING,
    IT_TRREDSPRING, IT_TLREDSPRING, IT_BRREDSPRING, IT_BLREDSPRING,
    IT_BLUESPRING, IT_BBLUESPRING, IT_RBLUESPRING, IT_LBLUESPRING,
    IT_TRBLUESPRING, IT_TLBLUESPRING, IT_BRBLUESPRING, IT_BLBLUESPRING,
    IT_BLUERING, IT_SWITCH, IT_DOOR, IT_TELEPORTER, IT_BIGRING, IT_CHECKPOINT, IT_GOAL,
    IT_ENDSIGN, IT_ENDLEVEL, IT_LOOPFLOOR, IT_LOOPFLOORNONE, IT_LOOPFLOORTOP, IT_BUMPER,
    IT_DANGER, IT_VDANGER, IT_FIREDANGER, IT_VFIREDANGER,
    IT_SPIKES, IT_CEILSPIKES, IT_LWSPIKES, IT_RWSPIKES, IT_PERSPIKES,
    IT_PERCEILSPIKES, IT_PERLWSPIKES, IT_PERRWSPIKES, IT_DNADOOR, IT_DNADOORNEON,
    IT_DNADOORCHARGE, IT_HDNADOOR, IT_HDNADOORNEON, IT_HDNADOORCHARGE,
    -1 /* -1 represents the end of this list */
};
static int editor_item_list_size; /* counted automatically */
static int editor_item_list_get_index(int item_id);
int editor_is_valid_item(int item_id); /* this is used by editorgrp */
static void editor_draw_object(enum editor_object_type obj_type, int obj_id, v2d_t position);

/* editor: enemy name list */
static const char** editor_enemy_name;
static int editor_enemy_name_length;
int editor_enemy_name2key(const char *name);
const char* editor_enemy_key2name(int key);


/* grid */
#define EDITOR_GRID_W         (int)(editor_grid_size().x)
#define EDITOR_GRID_H         (int)(editor_grid_size().y)
static int editor_grid_enabled; /* is the grid enabled? */

static void editor_grid_init();
static void editor_grid_release();
static void editor_grid_update();
static void editor_grid_render();

static v2d_t editor_grid_size(); /* returns the size of the grid */
static v2d_t editor_grid_snap(v2d_t position); /* aligns position to a cell in the grid */


/* implementing UNDO and REDO */

/* in order to implement UNDO and REDO,
 * we must register the actions of the
 * user */

/* action type */
enum editor_action_type {
    EDA_NEWOBJECT,
    EDA_DELETEOBJECT,
    EDA_CHANGESPAWN,
    EDA_RESTORESPAWN
};

/* action = action type + action properties */
typedef struct editor_action_t {
    enum editor_action_type type;
    enum editor_object_type obj_type;
    int obj_id;
    v2d_t obj_position;
    v2d_t obj_old_position;
} editor_action_t;

/* action: constructors */
static editor_action_t editor_action_entity_new(int is_new_object, enum editor_object_type obj_type, int obj_id, v2d_t obj_position);
static editor_action_t editor_action_spawnpoint_new(int is_changing, v2d_t obj_position, v2d_t obj_old_position);

/* linked list */
typedef struct editor_action_list_t {
    editor_action_t action; /* node data */
    int in_group; /* is this element part of a group? */
    uint32 group_key; /* internal use (used only if in_group is true) */
    struct editor_action_list_t *prev, *next; /* linked list: pointers */
} editor_action_list_t;

/* data */
editor_action_list_t *editor_action_buffer;
editor_action_list_t *editor_action_buffer_head; /* sentinel */
editor_action_list_t *editor_action_buffer_cursor;
    
/* methods */
static void editor_action_init();
static void editor_action_release();
static void editor_action_undo();
static void editor_action_redo();
static void editor_action_commit(editor_action_t action);
static void editor_action_register(editor_action_t action); /* internal */
static editor_action_list_t* editor_action_delete_list(editor_action_list_t *list); /* internal */












/* level-specific functions */

/*
 * level_load()
 * Loads a level from a file
 */
void level_load(const char *filepath)
{
    char abs_path[1024];
    parsetree_program_t *prog;

    setlocale(LC_NUMERIC, "C"); /* bugfix */
    logfile_message("level_load(\"%s\")", filepath);
    resource_filepath(abs_path, filepath, sizeof(abs_path), RESFP_READ);

    /* default values */
    strcpy(name, "Untitled");
    strcpy(musicfile, "");
    strcpy(theme, "");
    strcpy(bgtheme, "");
    strcpy(author, "");
    strcpy(version, "");
    strcpy(grouptheme, "");
    spawn_point = v2d_new(0,0);
    dialogregion_size = 0;
    boss = NULL;
    act = 1;
    requires[0] = GAME_VERSION;
    requires[1] = GAME_SUB_VERSION;
    requires[2] = GAME_WIP_VERSION;
    readonly = FALSE;

    /* traversing the level file */
    prog = nanoparser_construct_tree(abs_path);
    nanoparser_traverse_program(prog, traverse_level);
    prog = nanoparser_deconstruct_tree(prog);

    /* load the music */
    block_music = FALSE;
    music_set_volume(1.0);
    logfile_message("level_load(): block_music reset to FALSE, music_volume reset to 1.0 for '%s'", musicfile);
    music = music_load(musicfile);

    /* misc */
    update_level_size();

    /* success! */
    logfile_message("level_load() ok");
}

/*
 * level_unload()
 * Call manually after level_load() whenever
 * this level has to be released or changed
 */
void level_unload()
{
    brick_list_t *node, *next;
    item_list_t *inode, *inext;
    enemy_list_t *enode, *enext;

    logfile_message("level_unload()");
    music_stop();

    /* music_release_now() (rather than a bare music_unref()) makes sure
     * this level's music is actually evicted from the resource cache
     * right away, instead of sitting there at refcount 0 until the
     * periodic garbage collector gets to it (up to 2s later, see
     * clean_garbage() in engine.c). Without this, back-to-back levels
     * that share the same track (e.g. Act1 -> Act2 of the same zone,
     * loaded within the same frame in story mode) would silently reuse
     * the exact same music_t object across this unload and the next
     * level_load() -- which used to be able to race against the audio
     * decoder thread still finishing up the old play-through on that
     * very object (see music_vf_mutex in audio.c). It also drains any
     * leftover reference count on the two jingles below, in case the
     * player picked up more than one star / speed shoes this level --
     * a bare unref only ever removed one reference, so multiple pickups
     * used to leak the entry until the level unloaded a second/third
     * time. Calling this on a track that was never loaded this level
     * (the common case for invincible.ogg/speed.ogg) remains a safe
     * no-op, same as the old music_unref() calls were. */
    music_release_now(musicfile);
    music_release_now("musics/invincible.ogg");
    music_release_now("musics/speed.ogg");
    music = NULL; /* was just destroyed (or never existed): don't leave a dangling pointer
                    * around in case update_music() is ever reached before the next
                    * level_load() reassigns it */

    /* clears the brick_list */
    logfile_message("releasing brick list...");
    for(node=brick_list; node; node=next) {
        next = node->next;
        free(node->data);
        free(node);
    }
    brick_list = NULL;

    /* clears the item list */
    logfile_message("releasing item list...");
    for(inode=item_list; inode; inode=inext) {
        inext = inode->next;
        item_destroy(inode->data);
        free(inode);
    }
    item_list = NULL;

    /* clears the enemy list */
    logfile_message("releasing enemy list...");
    for(enode=enemy_list; enode; enode=enext) {
        enext = enode->next;
        enemy_destroy(enode->data);
        free(enode);
    }
    enemy_list = NULL;

    /* releasing the boss */
    if(got_boss()) {
        logfile_message("releasing the boss...");
        boss_destroy(boss);
        boss = NULL;
    }

    /* unloading the brickset */
    logfile_message("unloading the brickset...");
    brickdata_unload();

    /* unloading the background */
    logfile_message("unloading the background...");
    backgroundtheme = background_unload(backgroundtheme);

    /* success! */
    logfile_message("level_unload() ok");
}


/*
 * level_save()
 * Saves the current level to a file
 */
void level_save(const char *filepath)
{
    int i;
    FILE *fp;
    char abs_path[1024];
    brick_list_t *itb;
    item_list_t *iti;
    enemy_list_t *ite;

    resource_filepath(abs_path, filepath, sizeof(abs_path), RESFP_WRITE);

    /* open for writing */
    logfile_message("level_save(\"%s\")", abs_path);
    if(NULL == (fp=fopen(abs_path, "w"))) {
        logfile_message("Warning: could not open \"%s\" for writing.", abs_path);
        video_showmessage("Could not open \"%s\" for writing.", abs_path);
        return;
    }

    /* meta information */
    fprintf(fp,
    "// ------------------------------------------------------------\n"
    "// %s %d.%d.%d level\n"
    "// This file was generated by the built-in level editor.\n"
    "// ------------------------------------------------------------\n"
    "\n"
    "// header\n"
    "name \"%s\"\n"
    "author \"%s\"\n"
    "version \"%s\"\n"
    "requires %d.%d.%d\n"
    "act %d\n"
    "theme \"%s\"\n"
    "bgtheme \"%s\"\n"
    "spawn_point %d %d\n",
    GAME_TITLE, GAME_VERSION, GAME_SUB_VERSION, GAME_WIP_VERSION,
    name, author, version, GAME_VERSION, GAME_SUB_VERSION, GAME_WIP_VERSION, act, theme, bgtheme,
    (int)spawn_point.x, (int)spawn_point.y);

    /* music? */
    if(strcmp(musicfile, "") != 0)
        fprintf(fp, "music \"%s\"\n", musicfile);

    /* grouptheme? */
    if(strcmp(grouptheme, "") != 0)
        fprintf(fp, "grouptheme \"%s\"\n", grouptheme);

    /* boss? */
    if(got_boss())
        fprintf(fp, "boss %d %d %d %d %d %d %d\n", boss->type, (int)boss->actor->spawn_point.x, (int)boss->actor->spawn_point.y, boss->rect_x, boss->rect_y, boss->rect_w, boss->rect_h);

    /* read only? */
    if(readonly)
        fprintf(fp, "readonly\n");

    /* dialog regions */
    fprintf(fp, "\n// dialog regions (xpos ypos width height title message)\n");
    for(i=0; i<dialogregion_size; i++)
        fprintf(fp, "dialogbox %d %d %d %d \"%s\" \"%s\"\n", dialogregion[i].rect_x, dialogregion[i].rect_y, dialogregion[i].rect_w, dialogregion[i].rect_h, dialogregion[i].title, dialogregion[i].message);

    /* brick list */
    fprintf(fp, "\n// brick list\n");
    for(itb=brick_list; itb; itb=itb->next) 
        fprintf(fp, "brick %d %d %d\n", get_brick_id(itb->data), (int)itb->data->sx, (int)itb->data->sy);

    /* item list */
    fprintf(fp, "\n// item list\n");
    for(iti=item_list; iti; iti=iti->next)
        fprintf(fp, "item %d %d %d\n", iti->data->type, (int)iti->data->actor->spawn_point.x, (int)iti->data->actor->spawn_point.y);

    /* enemy list */
    fprintf(fp, "\n// object list\n");
    for(ite=enemy_list; ite; ite=ite->next) {
        if(ite->data->created_from_editor)
            fprintf(fp, "object \"%s\" %d %d\n", str_addslashes(ite->data->name), (int)ite->data->actor->spawn_point.x, (int)ite->data->actor->spawn_point.y);
    }

    /* done! */
    fprintf(fp, "\n// EOF");
    fclose(fp);
    logfile_message("level_save() ok");
}

/*
 * traverse_level()
 * Level reader
 */
int traverse_level(const parsetree_statement_t* stmt)
{
    const char* identifier = nanoparser_get_identifier(stmt);
    const parsetree_parameter_t* param_list = nanoparser_get_parameter_list(stmt);
    int i, param_count;
    const char** param;

    /* read the parameters */
    param_count = nanoparser_get_number_of_parameters(param_list);
    param = mallocx(param_count * (sizeof *param));
    for(i=0; i<param_count; i++) {
        const parsetree_parameter_t* p;
        p = nanoparser_get_nth_parameter(param_list, 1+i);
        nanoparser_expect_string(p, "Level loader - string parameters are expected for every command");
        param[i] = nanoparser_get_string(p);
    }

    /* interpreting the command */
    if(str_icmp(identifier, "theme") == 0) {
        if(param_count == 1) {
            if(brickdata_size() == 0) {
                str_cpy(theme, param[0], sizeof(theme));
                brickdata_load(theme);
            }
        }
        else
            logfile_message("Level loader - command 'theme' expects one parameter: brickset filepath. Did you forget to double quote the brickset filepath?");
    }
    else if(str_icmp(identifier, "bgtheme") == 0) {
        if(param_count == 1) {
            if(backgroundtheme == NULL) {
                str_cpy(bgtheme, param[0], sizeof(bgtheme));
                backgroundtheme = background_load(bgtheme);
            }
        }
        else
            logfile_message("Level loader - command 'bgtheme' expects one parameter: background filepath. Did you forget to double quote the background filepath?");
    }
    else if(str_icmp(identifier, "grouptheme") == 0) {
        if(param_count == 1) {
            if(editorgrp_group_count() == 0) {
                str_cpy(grouptheme, param[0], sizeof(grouptheme));
                editorgrp_load_from_file(grouptheme);
            }
        }
        else
            logfile_message("Level loader - command 'grouptheme' expects one parameter: grouptheme filepath. Did you forget to double quote the grouptheme filepath?");
    }
    else if(str_icmp(identifier, "music") == 0) {
        if(param_count == 1)
            str_cpy(musicfile, param[0], sizeof(musicfile));
        else
            logfile_message("Level loader - command 'music' expects one parameter: music filepath. Did you forget to double quote the music filepath?");
    }
    else if(str_icmp(identifier, "name") == 0) {
        if(param_count == 1)
            str_cpy(name, param[0], sizeof(name));
        else
            logfile_message("Level loader - command 'name' expects one parameter: level name. Did you forget to double quote the level name?");
    }
    else if(str_icmp(identifier, "author") == 0) {
        if(param_count == 1)
            str_cpy(author, param[0], sizeof(name));
        else
            logfile_message("Level loader - command 'author' expects one parameter: author name. Did you forget to double quote the author name?");
    }
    else if(str_icmp(identifier, "version") == 0) {
        if(param_count == 1)
            str_cpy(version, param[0], sizeof(name));
        else
            logfile_message("Level loader - command 'version' expects one parameter: level version");
    }
    else if(str_icmp(identifier, "requires") == 0) {
        if(param_count == 1) {
            int i;
            sscanf(param[0], "%d.%d.%d", &requires[0], &requires[1], &requires[2]);
            for(i=0; i<3; i++) requires[i] = clip(requires[i], 0, 99);
            if(game_version_compare(requires[0], requires[1], requires[2]) < 0) {
                fatal_error(
                    "This level requires version %d.%d.%d or greater of the game engine.\nPlease check our for new versions at %s",
                    requires[0], requires[1], requires[2], GAME_WEBSITE
                );
            }
        }
        else
            logfile_message("Level loader - command 'requires' expects one parameter: minimum required engine version");
    }
    else if(str_icmp(identifier, "act") == 0) {
        if(param_count == 1)
            act = clip(atoi(param[0]), 1, 3);
        else
            logfile_message("Level loader - command 'act' expects one parameter: act number");
    }
    else if(str_icmp(identifier, "spawn_point") == 0) {
        if(param_count == 2) {
            int x, y;
            x = atoi(param[0]);
            y = atoi(param[1]);
            spawn_point = v2d_new(x, y);
        }
        else
            logfile_message("Level loader - command 'spawn_point' expects two parameters: xpos, ypos");
    }
    else if(str_icmp(identifier, "boss") == 0) {
        logfile_message("Level loader - WARNING: command 'boss' is deprecated!");
        if(param_count == 7) {
            int type, x, y, rx, ry, rw, rh;
            if(!got_boss()) {
                type = atoi(param[0]);
                x = atoi(param[1]);
                y = atoi(param[2]);
                rx = atoi(param[3]);
                ry = atoi(param[4]);
                rw = atoi(param[5]);
                rh = atoi(param[6]);
                boss = boss_create(type, v2d_new(x,y), rx, ry, rw, rh);
            }
        }
        else
            logfile_message("Level loader - command 'boss' expects seven parameters: type, xpos, ypos, rect_xpos, rect_ypos, rect_width, rect_height");
    }
    else if(str_icmp(identifier, "dialogbox") == 0) {
        if(param_count == 6) {
            dialogregion_t *d = &(dialogregion[dialogregion_size++]);
            d->disabled = FALSE;
            d->rect_x = atoi(param[0]);
            d->rect_y = atoi(param[1]);
            d->rect_w = atoi(param[2]);
            d->rect_h = atoi(param[3]);
            str_cpy(d->title, param[4], sizeof(d->title));
            str_cpy(d->message, param[5], sizeof(d->message));
        }
        else
            logfile_message("Level loader - command 'dialogbox' expects six parameters: rect_xpos, rect_ypos, rect_width, rect_height, title, message. Did you forget to double quote the message?");
    }
    else if(str_icmp(identifier, "readonly") == 0) {
        if(param_count == 0)
            readonly = TRUE;
        else
            logfile_message("Level loader - command 'readonly' expects no parameters");
    }
    else if(str_icmp(identifier, "brick") == 0) {
        if(param_count == 3) {
            if(str_icmp(theme, "") != 0) {
                int type;
                int x, y;

                type = clip(atoi(param[0]), 0, brickdata_size()-1);
                x = atoi(param[1]);
                y = atoi(param[2]);

                if(brickdata_get(type) != NULL)
                    level_create_brick(type, v2d_new(x,y));
                else
                    logfile_message("Level loader - invalid brick: %d", type);
            }
            else
                logfile_message("Level loader - warning: cannot create a new brick if the theme is not defined");
        }
        else
            logfile_message("Level loader - command 'brick' expects three parameters: type, xpos, ypos");
    }
    else if(str_icmp(identifier, "item") == 0) {
        if(param_count == 3) {
            int type;
            int x, y;

            type = clip(atoi(param[0]), 0, ITEMDATA_MAX-1);
            x = atoi(param[1]);
            y = atoi(param[2]);

            level_create_item(type, v2d_new(x,y));
        }
        else
            logfile_message("Level loader - command 'item' expects three parameters: type, xpos, ypos");
    }
    else if((str_icmp(identifier, "enemy") == 0) || (str_icmp(identifier, "object") == 0)) {
        if(param_count == 3) {
            const char *name;
            int x, y;

            name = param[0];
            x = atoi(param[1]);
            y = atoi(param[2]);

            level_create_enemy(name, v2d_new(x,y));
        }
        else
            logfile_message("Level loader - command '%s' expects three parameters: enemy_name, xpos, ypos", identifier);
    }

    /* bye! */
    free(param);
    return 0;
}



/* scene functions */

/*
 * level_init()
 * Initializes the scene
 */
void level_init()
{
    int i;

    /* main init */
    logfile_message("level_init()");
    brick_list = NULL;
    item_list = NULL;
    gravity = 800;
    level_width = level_height = 0;
    level_timer = 0;
    dialogregion_size = 0;
    override_music = NULL;
    level_cleared = FALSE;
    quit_level = FALSE;
    quit_level_img = image_create(video_get_backbuffer()->w, video_get_backbuffer()->h);
    actclear_starttime = actclear_endtime = actclear_sampletimer = 0;
    actclear_ringbonus = actclear_secretbonus = actclear_totalbonus = 0;
    actclear_prepare_next_level = actclear_goto_next_level = FALSE;
    actclear_played_song = FALSE;
    backgroundtheme = NULL;

    /* helpers */
    particle_init();
    editor_init();

    /* level init */
    level_load(file);

    /* loading players */
    logfile_message("Creating players...");
    team[0] = player_create(PL_SONIC);
    team[1] = player_create(PL_TAILS);
    team[2] = player_create(PL_KNUCKLES);
    spawn_players();
    player_id = 0;
    player = team[player_id]; 
    camera_init();
    camera_set_position(player->actor->position);
    player_set_rings(0);
    level_set_camera_focus(player->actor);
    player_inside_boss_area = FALSE;
    boss_fight_activated = FALSE;

    /* gui */
    logfile_message("Loading hud...");
    maingui = actor_create();
    maingui->position = v2d_new(16, 7);
    actor_change_animation(maingui, sprite_get_animation("SD_MAINGUI", 0));
    lifegui = actor_create();
    lifegui->position = v2d_new(16, VIDEO_SCREEN_H-23);
    actor_change_animation(lifegui, sprite_get_animation("SD_LIFEGUI", 0));
    lifefnt = font_create(0);
    lifefnt->position = v2d_add(lifegui->position, v2d_new(32,11)); 
    for(i=0; i<3; i++) {
        mainfnt[i] = font_create(2);
        mainfnt[i]->position = v2d_add(maingui->position, v2d_new(42, i*16+2));
    }

    /* level opening */
    levelop = actor_create();
    levelop->position = v2d_new(0,-240);
    actor_change_animation(levelop, sprite_get_animation("SD_LEVELOP", 0));
    levelact = actor_create();
    levelact->position = v2d_new(260,250);
    actor_change_animation(levelact, sprite_get_animation("SD_LEVELACT", act-1));
    leveltitle = font_create(3);
    leveltitle->position = v2d_new(330,50);
    font_set_text(leveltitle, str_to_upper(name));
    font_set_width(leveltitle, 180);

    /* end of act */
    actclear_teamname = font_create(4);
    actclear_gotthrough = font_create(7);
    actclear_levelact = actor_create();
    for(i=0; i<ACTCLEAR_BONUSMAX; i++) {
        actclear_bonusfnt[i] = font_create(2);
        actclear_bonus[i] = actor_create();
    }

    /* dialog box */
    dlgbox_active = FALSE;
    dlgbox_starttime = 0;
    dlgbox = actor_create();
    dlgbox->position.y = VIDEO_SCREEN_H;
    actor_change_animation(dlgbox, sprite_get_animation("SD_DIALOGBOX", 0));
    dlgbox_title = font_create(8);
    dlgbox_message = font_create(8);

    logfile_message("level_init() ok");
}


/*
 * level_update()
 * Updates the scene (this one runs
 * every cycle of the program)
 */
void level_update()
{
    int i, cbox;
    int got_dying_player = FALSE;
    int block_pause = FALSE, block_quit = FALSE;
    float dt = timer_get_delta();
    brick_list_t *major_bricks, *fake_bricks, *bnode, *bnext;
    item_list_t *major_items, *inode;
    enemy_list_t *enode;

    remove_dead_bricks();
    remove_dead_items();
    remove_dead_objects();

    if(!editor_is_enabled()) {

        /* displaying message: "do you really want to quit?" */
        block_quit = level_timer < 5; /* opening animation? */
        for(i=0; i<3 && !block_quit; i++)
            block_quit = team[i]->dead;

        if(input_button_pressed(player->actor->input, IB_FIRE4) && !block_quit) {
            char op[3][512];

            image_blit(video_get_backbuffer(), quit_level_img, 0, 0, 0, 0, quit_level_img->w, quit_level_img->h);
            music_pause();

            lang_getstring("CBOX_QUIT_QUESTION", op[0], sizeof(op[0]));
            lang_getstring("CBOX_QUIT_OPTION1", op[1], sizeof(op[1]));
            lang_getstring("CBOX_QUIT_OPTION2", op[2], sizeof(op[2]));
            confirmbox_alert(op[0], op[1], op[2]);

            scenestack_push(storyboard_get_scene(SCENE_CONFIRMBOX));
            return;
        }

        cbox = confirmbox_selected_option();
        if(cbox == 1)
            quit_level = TRUE;
        else if(cbox == 2)
            music_resume();

        if(quit_level) {
            if(fadefx_over()) {
                scenestack_pop();
                quest_abort();
                return;
            }
            fadefx_out(image_rgb(0,0,0), 1.0);
            return;
        }

        /* open level editor */
        if(editor_want_to_activate()) {
            if(readonly) {
                video_showmessage("No way!");
                sound_play( soundfactory_get("deny") );
            }
            else {
                editor_enable();
                return;
            }
        }

        /* pause game */
        block_pause = level_timer < 5; /* are we viewing opening animation? */
        for(i=0; i<3 && !block_pause; i++)
            block_pause = team[i]->dying || team[i]->dead;
        if(input_button_pressed(player->actor->input, IB_FIRE3) && !block_pause) {
            player->spin_dash = player->braking = FALSE;
            music_pause();
            scenestack_push(storyboard_get_scene(SCENE_PAUSE));
            return;
        }




        /* gui */
        actor_change_animation(maingui, sprite_get_animation("SD_MAINGUI", player_get_rings()>0 ? 0 : 1));
        actor_change_animation(lifegui, sprite_get_animation("SD_LIFEGUI", player_id));
        font_set_text(lifefnt, "%2d", player_get_lives());
        font_set_text(mainfnt[0], "% 7d", player_get_score());
        font_set_text(mainfnt[1], "%d:%02d", (int)level_timer/60, (int)level_timer%60);
        font_set_text(mainfnt[2], "% 4d", player_get_rings());

        /* level opening */
        if(level_timer < 5) {
            if(level_timer < 1.5) {
                levelop->position.y += 360*dt;
                if(levelop->position.y > -2)
                    levelop->position.y = -2;

                leveltitle->position.x -= 320*dt;
                if(leveltitle->position.x < 140)
                    leveltitle->position.x = 140;

                levelact->position.y -= 200*dt;
                if(levelact->position.y < 200)
                    levelact->position.y = 200;
            }
            else if(level_timer > 3.5) {
                levelop->position.x -= 320*dt;
            }
        }
        else {
            levelop->visible = FALSE;
            leveltitle->visible = FALSE;
            levelact->visible = FALSE;
        }


        /* end of act (reached the goal) */
        if(level_cleared) {
            float total = 0;
            uint32 tmr = timer_get_ticks();
            sound_t *ring = soundfactory_get("ring count");
            sound_t *cash = soundfactory_get("cash");
            sound_t *glasses = soundfactory_get("glasses");

            /* level music fadeout */
            if(music_is_playing())
                music_set_volume(1.0 - (float)(tmr-actclear_starttime)/2000.0);

            /* show scores */
            if(tmr >= actclear_starttime + 2000) {
                /* lock characters */
                for(i=0; i<3; i++)
                    team[i]->actor->speed.x = 0;

                /* set positions... */
                actclear_teamname->position.x = min(actclear_teamname->position.x + 800*dt, 30);
                actclear_gotthrough->position.x = min(actclear_gotthrough->position.x + 700*dt, 12);
                actclear_levelact->position.x = max(actclear_levelact->position.x - 700*dt, 250);

                for(i=0; i<ACTCLEAR_BONUSMAX; i++) {
                    actclear_bonus[i]->position.x = min(actclear_bonus[i]->position.x + (400-50*i)*dt, 50);
                    actclear_bonusfnt[i]->position.x = max(actclear_bonusfnt[i]->position.x - (400-50*i)*dt, 230);
                }

                /* counters (bonus) */
                total = actclear_totalbonus - (actclear_ringbonus + actclear_secretbonus);
                font_set_text(actclear_bonusfnt[0], "%d", (int)actclear_ringbonus);
                font_set_text(actclear_bonusfnt[1], "%d", (int)actclear_secretbonus);
                font_set_text(actclear_bonusfnt[ACTCLEAR_BONUSMAX-1], "%d", (int)total);

                /* reached the goal song */
                if(!actclear_played_song) {
                    music_stop();
                    sound_play( soundfactory_get("goal") );
                    actclear_played_song = TRUE;
                }
            }

            /* decreasing counters (bonus) */
            if(tmr >= actclear_starttime + 6000 && !actclear_prepare_next_level) {
                /* decreasing */
                actclear_ringbonus = max(0, actclear_ringbonus-400*dt);
                actclear_secretbonus = max(0, actclear_secretbonus-2000*dt);

                /* sound effects */
                if(actclear_ringbonus > 0 || actclear_secretbonus > 0) {
                    /* ring */
                    if(ring && tmr >= actclear_sampletimer) {
                        actclear_sampletimer = tmr+100;
                        sound_play(ring);
                    }
                }
                else {
                    /* cash */
                    if(cash) {
                        actclear_prepare_next_level = TRUE;
                        actclear_endtime = tmr + 4000;
                        sound_play(cash);
                    }

                    /* got glasses? */
                    for(i=0; i<3 && glasses; i++) {
                        if(team[i]->got_glasses) {
                            sound_play(glasses);
                            break;
                        }
                    }
                }
            }

            /* go to next level? */
            if(actclear_prepare_next_level && tmr >= actclear_endtime)
                actclear_goto_next_level = TRUE;
        }

        /* dialog box */
        update_dialogregions();
        update_dlgbox();


        /* *** updating the objects *** */

        got_dying_player = FALSE;
        for(i=0; i<3; i++) {
            if(team[i]->dying)
                got_dying_player = TRUE;
        }

        major_items = item_list_clip();
        major_bricks = brick_list_clip();
        fake_bricks = NULL;

        /* update background */
        background_update(backgroundtheme);

        /* update items */
        for(i=0;i<3;i++) team[i]->entering_loop=FALSE;
        for(inode = item_list; inode; inode=inode->next) {
            float x = inode->data->actor->position.x;
            float y = inode->data->actor->position.y;
            float w = actor_image(inode->data->actor)->w;
            float h = actor_image(inode->data->actor)->h;

            if(inside_screen(x, y, w, h, DEFAULT_MARGIN)) {
                item_update(inode->data, team, 3, major_bricks, item_list /*major_items*/, enemy_list); /* major_items bugs the switch/teleporter */
                if(inode->data->obstacle) { /* is this item an obstacle? */
                    /* we'll create a fake brick here */
                    brick_list_t *bn1, *bn2;
                    int offset = 1;
                    v2d_t v = v2d_add(inode->data->actor->hot_spot, v2d_new(0,-offset));
                    image_t *img = actor_image(inode->data->actor);
                    brick_t *fake = create_fake_brick(img->w, img->h-offset, v2d_subtract(inode->data->actor->position,v), 0);
                    fake->brick_ref->zindex = inode->data->bring_to_back ? 0.4 : 0.5;

                    /* add to the fake bricks list */
                    bn1 = mallocx(sizeof *bn1);
                    bn1->next = fake_bricks;
                    bn1->data = fake;
                    fake_bricks = bn1;

                    /* add to the major bricks list */
                    bn2 = mallocx(sizeof *bn2);
                    bn2->next = major_bricks;
                    bn2->data = fake;
                    major_bricks = bn2;
                }
            }
            else {
                /* this item is outside the screen... */
                if(!inode->data->preserve)
                    inode->data->state = IS_DEAD;
            }
        }



        /* update enemies */
        for(enode = enemy_list; enode; enode=enode->next) {
            float x = enode->data->actor->position.x;
            float y = enode->data->actor->position.y;
            float w = actor_image(enode->data->actor)->w;
            float h = actor_image(enode->data->actor)->h;

            if(inside_screen(x, y, w, h, DEFAULT_MARGIN) || enode->data->always_active) {
                /* update this object */
                if(!input_is_ignored(player->actor->input)) {
                    if(!got_dying_player && !level_cleared)
                        enemy_update(enode->data, team, 3, major_bricks, major_items, enemy_list);
                }

                /* is this object an obstacle? */
                if(enode->data->obstacle) {
                    /* we'll create a fake brick here */
                    brick_list_t *bn1, *bn2;
                    int offset = 1;
                    v2d_t v = v2d_add(enode->data->actor->hot_spot, v2d_new(0,-offset));
                    image_t *img = actor_image(enode->data->actor);
                    brick_t *fake = create_fake_brick(img->w, img->h-offset, v2d_subtract(enode->data->actor->position,v), enode->data->obstacle_angle);

                    /* add to the fake bricks list */
                    bn1 = mallocx(sizeof *bn1);
                    bn1->next = fake_bricks;
                    bn1->data = fake;
                    fake_bricks = bn1;

                    /* add to the major bricks list */
                    bn2 = mallocx(sizeof *bn2);
                    bn2->next = major_bricks;
                    bn2->data = fake;
                    major_bricks = bn2;
                }
            }
            else {
                /* this object is outside the screen... */
                if(!enode->data->preserve)
                    enode->data->state = ES_DEAD;
                else if(!inside_screen(enode->data->actor->spawn_point.x, enode->data->actor->spawn_point.y, w, h, DEFAULT_MARGIN))
                    enode->data->actor->position = enode->data->actor->spawn_point;
            }
        }


        /* update boss */
        if(got_boss()) {
            actor_t *pa = player->actor;
            float ba[4] = { pa->position.x, pa->position.y, pa->position.x+1, pa->position.y+1 };
            float bb[4] = { boss->rect_x, boss->rect_y, boss->rect_x+boss->rect_w, boss->rect_y+boss->rect_h };

            /* boss fight! */
            if(!got_dying_player)
                boss_update(boss, team, brick_list); /* bouken deshou, deshou!? */
            if(!boss_defeated(boss) && bounding_box(ba, bb)) { /* honto ga uso ni kawaru sekai de */
                player_inside_boss_area = TRUE; /* yume ga aru kara tsuyoku naru no yo */
                boss_fight_activated = TRUE; /* dare no tame janai */
                level_hide_dialogbox();
            }

            /* only the active player can enter the boss area */
            if(!boss_defeated(boss)) {
                int br = 30; /* border */
                actor_t *ta;

                for(i=0; i<3; i++) {
                    if(team[i] == player && !(player->actor->carrying))
                        continue;

                    ta = team[i]->actor;

                    if(ta->position.x > boss->rect_x-br && ta->position.x < boss->rect_x) {
                        ta->position.x = boss->rect_x-br;
                        ta->speed.x = 0;
                    }

                    if(ta->position.x > boss->rect_x+boss->rect_w && ta->position.x < boss->rect_x+boss->rect_w+br) {
                        ta->position.x = boss->rect_x+boss->rect_w+br;
                        ta->speed.x = 0;
                    }
                }
            }

            /* the boss has been defeated... */
            if(boss_defeated(boss) || player->dying) {
                player_inside_boss_area = FALSE;
                if(music) { /* fade-out music */
                    music_set_volume(music_get_volume() - 0.5*dt);
                    if(music_get_volume() < EPSILON) {
                        logfile_message("level.c: block_music = TRUE (boss_defeated=%d, player->dying=%d) -- update_music() fallback disabled for the rest of this level", boss_defeated(boss), player->dying);
                        music_stop();
                        music_set_volume(1.0);
                        block_music = TRUE;
                    }
                }
            }
        }



        /* update players */
        for(i=0; i<3; i++)
            input_ignore(team[i]->actor->input);

        if(level_timer >= 3.5 && camera_focus == player->actor) /* not (opening animation) */
            input_restore(player->actor->input);

        for(i=0; i<3; i++) {
            float x = team[i]->actor->position.x;
            float y = team[i]->actor->position.y;
            float w = actor_image(team[i]->actor)->w;
            float h = actor_image(team[i]->actor)->h;
            float hy = team[i]->actor->hot_spot.y;

            /* somebody is hurt! show it to the user */
            if(i != player_id) {
                if(team[i]->getting_hit)
                    level_change_player(i);

                if(team[i]->dying) {
                    level_change_player(i);
                    if(camera_focus != team[i]->actor)
                        camera_move_to(team[i]->actor->position, 0.0);
                }
            }

            /* death */
            if(team[i]->dead) {
                if(player_get_lives() > 1) {
                    /* restart the level! */
                    if(fadefx_over()) {
                        quest_setvalue(QUESTVALUE_TOTALTIME, quest_getvalue(QUESTVALUE_TOTALTIME)+level_timer);
                        player_set_lives(player_get_lives()-1);
                        restart();
                        return;
                    }
                    fadefx_out(image_rgb(0,0,0), 1.0);
                }
                else {
                    /* game over */
                    scenestack_pop();
                    scenestack_push(storyboard_get_scene(SCENE_GAMEOVER));
                    return;
                }
            }

            /* level cleared! */
            if(actclear_goto_next_level) {
                if(fadefx_over()) {
                    scenestack_pop();
                    return;
                }
                fadefx_out(image_rgb(0,0,0), 1.0);
            }

            /* updating... */
            if(inside_screen(x, y, w, h, DEFAULT_MARGIN/4) || team[i]->dying) {
                if(!got_dying_player || team[i]->dying || team[i]->getting_hit)
                    player_update(team[i], team, major_bricks);
            }

            /* clipping... */
            if(team[i]->actor->position.y < hy && !team[i]->dying) {
                team[i]->actor->position.y = hy;
                team[i]->actor->speed.y = 0;
            }
            else if(team[i]->actor->position.y > level_height-(h-hy)) {
                if(inside_screen(x,y,w,h,DEFAULT_MARGIN/4))
                    player_kill(team[i]);
            }
        }

        /* change the active team member */
        if(!got_dying_player && !level_cleared) {
            level_timer += timer_get_delta();
            if(input_button_pressed(player->actor->input, IB_FIRE2))  {
                if(fabs(player->actor->speed.y) < EPSILON && !player->on_moveable_platform && !player_inside_boss_area && !player->disable_movement && !player->in_locked_area)
                    level_change_player((player_id+1) % 3);
                else
                    sound_play( soundfactory_get("deny") );
            }
        }

        /* boss area */
        if(got_boss() && player_inside_boss_area) {
            actor_t *pa = player->actor;

            if(pa->position.x < boss->rect_x) {
                pa->position.x = boss->rect_x;
                pa->speed.x = max(0, pa->speed.x);
            }
            else if(pa->position.x > boss->rect_x+boss->rect_w) {
                pa->position.x = boss->rect_x+boss->rect_w;
                pa->speed.x = min(pa->speed.x, 0);
            }

            pa->position.y = clip(pa->position.y, boss->rect_y, boss->rect_y+boss->rect_h);
        }

        /* if someone is dying, fade out the music; restore it once nobody is dying anymore
         * (previously this restore only existed inside the got_boss() branch below, so any
         * death in a non-boss level left music_volume decayed for the rest of the session) */
        if(got_dying_player)
            music_set_volume(music_get_volume() - 0.5*dt);
        else if(music_get_volume() < 1.0) {
            logfile_message("level.c: restoring music_volume to 1.0 (was %.3f, nobody dying anymore)", music_get_volume());
            music_set_volume(1.0);
        }

        /* update particles */
        particle_update_all(major_bricks);

        /* update bricks */
        for(bnode=major_bricks; bnode; bnode=bnode->next) {

            /* <breakable bricks> */
            if(bnode->data->brick_ref->behavior == BRB_BREAKABLE) {
                int brkw = bnode->data->brick_ref->image->w;
                int brkh = bnode->data->brick_ref->image->h;
                float a[4], b[4] = { bnode->data->x, bnode->data->y, bnode->data->x + brkw, bnode->data->y + brkh };

                for(i=0; i<3; i++) {
                    a[0] = team[i]->actor->position.x - team[i]->actor->hot_spot.x - 3;
                    a[1] = team[i]->actor->position.y - team[i]->actor->hot_spot.y - 3;
                    a[2] = a[0] + actor_image(team[i]->actor)->w + 6;
                    a[3] = a[1] + actor_image(team[i]->actor)->h + 6;

                    if((team[i]->spin_dash || team[i]->spin || team[i]->type == PL_KNUCKLES) && bounding_box(a,b)) {
                        /* particles */
                        int bi, bj, bh, bw;
                        bw = max(bnode->data->brick_ref->behavior_arg[0], 1);
                        bh = max(bnode->data->brick_ref->behavior_arg[1], 1);
                        for(bi=0; bi<bw; bi++) {
                            for(bj=0; bj<bh; bj++) {
                                v2d_t brkpos = v2d_new(bnode->data->x + (bi*brkw)/bw, bnode->data->y + (bj*brkh)/bh);
                                v2d_t brkspeed = v2d_new(-team[i]->actor->speed.x*0.3, -100-random(50));
                                image_t *brkimg = image_create(brkw/bw, brkh/bh);

                                image_blit(bnode->data->brick_ref->image, brkimg, (bi*brkw)/bw, (bj*brkh)/bh, 0, 0, brkw/bw, brkh/bh);
                                if(fabs(brkspeed.x) > EPSILON) brkspeed.x += (brkspeed.x>0?1:-1) * random(50);
                                level_create_particle(brkimg, brkpos, brkspeed, FALSE);
                            }
                        }

                        /* bye bye, brick! */
                        sound_play( soundfactory_get("break") );
                        bnode->data->state = BRS_DEAD;
                    }
                }
            }
            /* </breakable bricks> */

            /* <falling bricks> */
            if(bnode->data->brick_ref->behavior == BRB_FALL && bnode->data->state == BRS_ACTIVE) {
                brick_t *brick_down = bnode->data;
                brick_down->value[1] += timer_get_delta(); /* timer */
                if(brick_down->value[1] >= BRB_FALL_TIME) {
                    int bi, bj, bw, bh;
                    int right_oriented = ((int)brick_down->brick_ref->behavior_arg[2] != 0);
                    image_t *brkimg = brick_down->brick_ref->image;
                
                    /* particles */
                    bw = max(brick_down->brick_ref->behavior_arg[0], 1);
                    bh = max(brick_down->brick_ref->behavior_arg[1], 1);
                    for(bi=0; bi<bw; bi++) {
                        for(bj=0; bj<bh; bj++) {
                            v2d_t piecepos = v2d_new(brick_down->x + (bi*brkimg->w)/bw, brick_down->y + (bj*brkimg->h)/bh);
                            v2d_t piecespeed = v2d_new(0, 20+bj*20+ (right_oriented?bi:bw-bi)*20);
                            image_t *piece = image_create(brkimg->w/bw, brkimg->h/bh);

                            image_blit(brkimg, piece, (bi*brkimg->w)/bw, (bj*brkimg->h)/bh, 0, 0, piece->w, piece->h);
                            level_create_particle(piece, piecepos, piecespeed, FALSE);
                        }
                    }

                    /* bye, brick! :] */
                    sound_play( soundfactory_get("break") );
                    brick_down->state = BRS_DEAD;
                }            
            }
            /* </falling bricks> */

            /* <moveable bricks> */
            brick_move(bnode->data);
            /* </moveable bricks> */
        }

        /* cleanup the fake bricks list */
        for(bnode=fake_bricks; bnode; bnode=bnext) {
            bnext = bnode->next;
            destroy_fake_brick(bnode->data);
            free(bnode);
        }
        fake_bricks = NULL;


        brick_list_unclip(major_bricks);
        item_list_unclip(major_items);


        /* update camera */
        if(level_cleared)
            camera_move_to(v2d_add(camera_focus->position, v2d_new(0, -90)), 0.17);
        else if(player_inside_boss_area) {
            float lock[2] = { boss->rect_x+VIDEO_SCREEN_W/2, boss->rect_x+boss->rect_w-VIDEO_SCREEN_W/2 };
            v2d_t offv = v2d_new( clip(camera_focus->position.x, lock[0], lock[1]), camera_focus->position.y );
            camera_move_to(v2d_add(offv, v2d_new(0, -90)), 0.17);
        }
        else if(!got_dying_player)
            camera_move_to(camera_focus->position, 0.10);

        camera_update();
    }
    else {
        /* level editor */
        editor_update();
    }

    /* other stuff */
    update_music();
}



/* --- profiling: TEMPORARY, remove once the framerate issue is
 * resolved. Isolates, inside scn->render() in level.c, how much time
 * goes into: background parallax, tiles/bricks, enemies, the rest of
 * the entities (players/items/boss), and foreground parallax + HUD.
 * Same as in engine.c: accumulates in microseconds and is flushed via
 * logfile_message() every 60 rendered level frames. */
static SceUInt64 prof_bg_us = 0, prof_bricks_us = 0, prof_enemies_us = 0, prof_other_ents_us = 0, prof_fg_hud_us = 0;
static int prof_bricks_count = 0, prof_enemies_count = 0;
static int prof_level_frame_count = 0;

/*
 * level_render()
 * Rendering function
 */
void level_render()
{
    SceUInt64 t0, t1, t2, t3;

    /* quit... */
    if(quit_level) {
        image_blit(quit_level_img, video_get_backbuffer(), 0, 0, 0, 0, quit_level_img->w, quit_level_img->h);
        return;
    }

    /* render the level editor? */
    if(editor_is_enabled()) {
        editor_render();
        return;
    }

    t0 = sceKernelGetProcessTimeWide();
    image_clear(video_get_backbuffer(), image_rgb(0, 0, 0));
    
    /* background */
    background_render_bg(backgroundtheme, camera_get_position());

    t1 = sceKernelGetProcessTimeWide();

    /* entities (bricks/enemies/players/items/boss -- render_entities()
     * does its own finer-grained timing internally, see below) */
    render_entities();

    t2 = sceKernelGetProcessTimeWide();

    /* foreground */
    background_render_fg(backgroundtheme, camera_get_position());

    /* hud */
    render_hud();

    t3 = sceKernelGetProcessTimeWide();

    prof_bg_us += (t1 - t0);
    prof_fg_hud_us += (t3 - t2);
    prof_level_frame_count++;

    if(prof_level_frame_count >= 60) {
        SceUInt64 total_us = prof_bg_us + prof_bricks_us + prof_enemies_us + prof_other_ents_us + prof_fg_hud_us;
        logfile_message(
            "PROFILE(level): avg parallax_bg=%.2fms avg bricks=%.2fms (n=%d/frame) avg enemies=%.2fms (n=%d/frame) avg other_entities=%.2fms avg parallax_fg+hud=%.2fms => scn->render total avg=%.2fms",
            (double)prof_bg_us / prof_level_frame_count / 1000.0,
            (double)prof_bricks_us / prof_level_frame_count / 1000.0,
            prof_bricks_count / prof_level_frame_count,
            (double)prof_enemies_us / prof_level_frame_count / 1000.0,
            prof_enemies_count / prof_level_frame_count,
            (double)prof_other_ents_us / prof_level_frame_count / 1000.0,
            (double)prof_fg_hud_us / prof_level_frame_count / 1000.0,
            (double)total_us / prof_level_frame_count / 1000.0
        );
        prof_bg_us = prof_bricks_us = prof_enemies_us = prof_other_ents_us = prof_fg_hud_us = 0;
        prof_bricks_count = prof_enemies_count = 0;
        prof_level_frame_count = 0;
    }
}



/*
 * level_release()
 * Releases the scene
 */
void level_release()
{
    int i;

    logfile_message("level_release()");

    image_destroy(quit_level_img);
    particle_release();
    level_unload();
    for(i=0; i<3; i++)
        player_destroy(team[i]);
    camera_release();
    editor_release();

    actor_destroy(lifegui);
    actor_destroy(maingui);
    font_destroy(lifefnt);
    for(i=0; i<3; i++)
        font_destroy(mainfnt[i]);

    actor_destroy(levelop);
    actor_destroy(levelact);
    font_destroy(leveltitle);

    font_destroy(actclear_teamname);
    font_destroy(actclear_gotthrough);
    actor_destroy(actclear_levelact);
    for(i=0; i<ACTCLEAR_BONUSMAX; i++) {
        font_destroy(actclear_bonusfnt[i]);
        actor_destroy(actclear_bonus[i]);
    }

    font_destroy(dlgbox_title);
    font_destroy(dlgbox_message);
    actor_destroy(dlgbox);

    logfile_message("level_release() ok");
}




/*
 * level_setfile()
 * Call this before initializing this scene. This
 * function tells the scene what level it must
 * load... then it gets initialized.
 */
void level_setfile(const char *level)
{
    strcpy(file, level);
    logfile_message("level_setfile('%s')", level);
}


/*
 * level_create_particle()
 * Creates a new particle.
 */
void level_create_particle(image_t *image, v2d_t position, v2d_t speed, int destroy_on_brick)
{
    particle_t *p;
    particle_list_t *node;

    /* no, you can't create a new particle! */
    if(editor_is_enabled()) {
        image_destroy(image);
        return;
    }

    p = mallocx(sizeof *p);
    p->image = image;
    p->position = position;
    p->speed = speed;
    p->destroy_on_brick = destroy_on_brick;

    node = mallocx(sizeof *node);
    node->data = p;
    node->next = particle_list;
    particle_list = node;
}


/*
 * level_player()
 * Returns the current player
 */
player_t* level_player()
{
    return player;
}



/*
 * level_change_player()
 * Changes the current player
 */
void level_change_player(int id)
{
    player->spin_dash = player->braking = FALSE;
    player_id = id;
    player = team[player_id];
    level_set_camera_focus(player->actor);
    input_restore(player->actor->input);
}

/*
 * level_create_brick()
 * Creates and adds a brick to the level. This function
 * returns a pointer to the created brick.
 */
brick_t* level_create_brick(int type, v2d_t position)
{
    int i;
    brick_list_t *node;

    node = mallocx(sizeof *node);
    node->data = mallocx(sizeof(brick_t));

    node->data->brick_ref = brickdata_get(type);
    node->data->animation_frame = 0;
    node->data->x = node->data->sx = (int)position.x;
    node->data->y = node->data->sy = (int)position.y;
    node->data->enabled = TRUE;
    node->data->state = BRS_IDLE;
    for(i=0; i<BRICK_MAXVALUES; i++)
        node->data->value[i] = 0;

    insert_brick_sorted(node);
    return node->data;
}



/*
 * level_create_item()
 * Creates and adds an item to the level. Returns the
 * created item.
 */
item_t* level_create_item(int type, v2d_t position)
{
    item_list_t *node;

    node = mallocx(sizeof *node);
    node->data = item_create(type);
    node->data->actor->spawn_point = position;
    node->data->actor->position = position;
    node->next = item_list;
    item_list = node;

    return node->data;
}



/*
 * level_create_enemy()
 * Creates and adds an enemy to the level. Returns the
 * created enemy.
 */
enemy_t* level_create_enemy(const char *name, v2d_t position)
{
    enemy_list_t *node;

    node = mallocx(sizeof *node);
    node->data = enemy_create(name);
    node->data->actor->spawn_point = position;
    node->data->actor->position = position;
    node->next = enemy_list;
    enemy_list = node;

    return node->data;
}


/*
 * level_item_list()
 * Returns the item list
 */
item_list_t* level_item_list()
{
    return item_list;
}



/*
 * level_enemy_list()
 * Returns the enemy list
 */
enemy_list_t* level_enemy_list()
{
    return enemy_list;
}



/*
 * level_gravity()
 * Returns the gravity of the level
 */
float level_gravity()
{
    return gravity;
}


/*
 * level_player_id()
 * Returns the ID of the current player
 */
int level_player_id()
{
    return player_id;
}



/*
 * level_add_to_score()
 * Adds a value to the player's score.
 * It also creates a flying text that
 * shows that score.
 */
void level_add_to_score(int score)
{
    item_t *flyingtext;
    char buf[80];

    score = max(0, score);
    player_set_score(player_get_score() + score);

    sprintf(buf, "%d", score);
    flyingtext = level_create_item(IT_FLYINGTEXT, v2d_add(player->actor->position, v2d_new(-9,0)));
    flyingtext_set_text(flyingtext, buf);
}


/*
 * level_create_animal()
 * Creates a random animal
 */
item_t* level_create_animal(v2d_t position)
{
    item_t *animal = level_create_item(IT_ANIMAL, position);
    return animal;
}



/*
 * level_set_camera_focus()
 * Sets a new focus to the camera
 */
void level_set_camera_focus(actor_t *act)
{
    camera_focus = act;
}



/*
 * level_editmode()
 * Is the level editor activated?
 */
int level_editmode()
{
    return editor_is_enabled();
}


/*
 * level_size()
 * Returns the size of the level
 */
v2d_t level_size()
{
    return v2d_new(level_width, level_height);
}


/*
 * level_override_music()
 * Stops the music while the given sample is playing.
 * After it gets finished, the music gets played again.
 */
void level_override_music(sound_t *sample)
{
    if(music) music_stop();
    override_music = sample;
    sound_play(override_music);
}



/*
 * level_set_spawn_point()
 * Defines a new spawn point
 */
void level_set_spawn_point(v2d_t newpos)
{
    spawn_point = newpos;
}


/*
 * level_clear()
 * Call this when the player clears this level
 */
void level_clear(actor_t *end_sign)
{
    int i;

    if(level_cleared)
        return;

    /* act cleared! */
    level_cleared = TRUE;
    actclear_starttime = timer_get_ticks();

    /* bonus */
    actclear_ringbonus = player_get_rings()*10;
    actclear_totalbonus += actclear_ringbonus;
    for(i=0; i<3; i++) {
        if(team[i]->got_glasses) {
            level_add_to_secret_bonus(5000);
            quest_setvalue(QUESTVALUE_GLASSES, quest_getvalue(QUESTVALUE_GLASSES)+1);
        }
    }
    player_set_score( player_get_score() + actclear_totalbonus );
    quest_setvalue(QUESTVALUE_TOTALTIME, quest_getvalue(QUESTVALUE_TOTALTIME)+level_timer);

    /* ignore input and focus the camera on the end sign */
    for(i=0; i<3; i++) {
        input_ignore(team[i]->actor->input);
        team[i]->spin_dash = FALSE;
    }
    level_set_camera_focus(end_sign);
    level_hide_dialogbox();

    /* initializing resources... */
    font_set_text(actclear_teamname, "TEAM SONIC");
    actclear_teamname->position = v2d_new(-500, 20);

    font_set_text(actclear_gotthrough, "GOT THROUGH");
    actclear_gotthrough->position = v2d_new(-500, 46);

    actor_change_animation(actclear_levelact, sprite_get_animation("SD_LEVELACT", act-1));
    actclear_levelact->position = v2d_new(820, 25);

    for(i=0; i<ACTCLEAR_BONUSMAX; i++) {
        actclear_bonus[i]->position = v2d_new(-500, 120+i*20);
        actclear_bonusfnt[i]->position = v2d_new(820, 120+i*20);
    }

    actor_change_animation(actclear_bonus[0], sprite_get_animation("SD_RINGBONUS", 0));
    actor_change_animation(actclear_bonus[1], sprite_get_animation("SD_SECRETBONUS", 0));
    actor_change_animation(actclear_bonus[ACTCLEAR_BONUSMAX-1], sprite_get_animation("SD_TOTAL", 0));
}


/*
 * level_add_to_secret_bonus()
 * Adds a value to the secret bonus
 */
void level_add_to_secret_bonus(int value)
{
    actclear_secretbonus += value;
    actclear_totalbonus += value;
}



/*
 * level_call_dialogbox()
 * Calls a dialog box
 */
void level_call_dialogbox(char *title, char *message)
{
    if(dlgbox_active && strcmp(font_get_text(dlgbox_title), title) == 0 && strcmp(font_get_text(dlgbox_message), message) == 0)
        return;

    dlgbox_active = TRUE;
    dlgbox_starttime = timer_get_ticks();
    font_set_text(dlgbox_title, title);
    font_set_text(dlgbox_message, message);
    font_set_width(dlgbox_message, 260);
}



/*
 * level_hide_dialogbox()
 * Hides the current dialog box (if any)
 */
void level_hide_dialogbox()
{
    dlgbox_active = FALSE;
}



/*
 * level_boss_battle()
 * Is/was the player fighting against the level boss (if any)?
 */
int level_boss_battle()
{
    return boss_fight_activated;
}


/*
 * level_kill_all_baddies()
 * Kills all the baddies on the level
 */
void level_kill_all_baddies()
{
    enemy_list_t *it;
    enemy_t *en;

    for(it=enemy_list; it; it=it->next) {
        en = it->data;
        en->state = ES_DEAD;
    }
}





/* private functions */


/* renders the entities of the level: bricks,
 * enemies, items, players, etc. */
void render_entities()
{
    brick_list_t *major_bricks, *p;
    item_list_t *inode;
    enemy_list_t *enode;
    SceUInt64 ta, tb;

    /* initializing major_bricks */
    major_bricks = brick_list_clip();

    ta = sceKernelGetProcessTimeWide();

    /* render bricks - background */
    for(p=major_bricks; p; p=p->next) {
        brickdata_t *ref = p->data->brick_ref;
        if(ref->zindex < 0.5) {
            brick_animate(p->data);
            image_draw(brick_image(p->data), video_get_backbuffer(), p->data->x-((int)camera_get_position().x-VIDEO_SCREEN_W/2), p->data->y-((int)camera_get_position().y-VIDEO_SCREEN_H/2), IF_NONE);
            prof_bricks_count++;
        }
    }

    tb = sceKernelGetProcessTimeWide();
    prof_bricks_us += (tb - ta);
    ta = tb;

    /* render players (bring to back?) */
    render_players(TRUE);

    tb = sceKernelGetProcessTimeWide();
    prof_other_ents_us += (tb - ta);
    ta = tb;

    /* render bricks - platform level (back) */
    for(p=major_bricks; p; p=p->next) {
        brickdata_t *ref = p->data->brick_ref;
        if(fabs(ref->zindex-0.5) < EPSILON && ref->property != BRK_OBSTACLE) {
            brick_animate(p->data);
            image_draw(brick_image(p->data), video_get_backbuffer(), p->data->x-((int)camera_get_position().x-VIDEO_SCREEN_W/2), p->data->y-((int)camera_get_position().y-VIDEO_SCREEN_H/2), IF_NONE);
            prof_bricks_count++;
        }
    }

    tb = sceKernelGetProcessTimeWide();
    prof_bricks_us += (tb - ta);
    ta = tb;

    /* render items (bring to back) */
    for(inode=item_list; inode; inode=inode->next) {
        if(inode->data->bring_to_back)
            item_render(inode->data, camera_get_position());
    }

    tb = sceKernelGetProcessTimeWide();
    prof_other_ents_us += (tb - ta);
    ta = tb;

    /* render bricks - platform level (front) */
    for(p=major_bricks; p; p=p->next) {
        brickdata_t *ref = p->data->brick_ref;
        if(fabs(ref->zindex-0.5) < EPSILON && ref->property == BRK_OBSTACLE) {
            brick_animate(p->data);
            image_draw(brick_image(p->data), video_get_backbuffer(), p->data->x-((int)camera_get_position().x-VIDEO_SCREEN_W/2), p->data->y-((int)camera_get_position().y-VIDEO_SCREEN_H/2), IF_NONE);
            prof_bricks_count++;
        }
    }

    tb = sceKernelGetProcessTimeWide();
    prof_bricks_us += (tb - ta);
    ta = tb;

    /* render boss (bring to back) */
    if(got_boss() && !boss->bring_to_front)
        boss_render(boss, camera_get_position());

    tb = sceKernelGetProcessTimeWide();
    prof_other_ents_us += (tb - ta);
    ta = tb;

    /* render enemies */
    for(enode=enemy_list; enode; enode=enode->next) {
        enemy_render(enode->data, camera_get_position());
        prof_enemies_count++;
    }

    tb = sceKernelGetProcessTimeWide();
    prof_enemies_us += (tb - ta);
    ta = tb;

    /* render players (bring to front?) */
    render_players(FALSE);

    /* render boss (bring to front) */
    if(got_boss() && boss->bring_to_front)
        boss_render(boss, camera_get_position());

    tb = sceKernelGetProcessTimeWide();
    prof_other_ents_us += (tb - ta);
    ta = tb;

    /* render items (bring to front) */
    for(inode=item_list; inode; inode=inode->next) {
        if(!inode->data->bring_to_back)
            item_render(inode->data, camera_get_position());
    }

    tb = sceKernelGetProcessTimeWide();
    prof_other_ents_us += (tb - ta);
    ta = tb;

    /* render particles */
    particle_render_all();

    tb = sceKernelGetProcessTimeWide();
    prof_other_ents_us += (tb - ta);
    ta = tb;

    /* render bricks - foreground */
    for(p=major_bricks; p; p=p->next) {
        brickdata_t *ref = p->data->brick_ref;
        if(ref->zindex > 0.5) {
            brick_animate(p->data);
            image_draw(brick_image(p->data), video_get_backbuffer(), p->data->x-((int)camera_get_position().x-VIDEO_SCREEN_W/2), p->data->y-((int)camera_get_position().y-VIDEO_SCREEN_H/2), IF_NONE);
            prof_bricks_count++;
        }
    }

    tb = sceKernelGetProcessTimeWide();
    prof_bricks_us += (tb - ta);

    /* releasing major_bricks */
    brick_list_unclip(major_bricks);
}

/* returns TRUE if a given region is
 * inside the screen position (camera-related) */
int inside_screen(int x, int y, int w, int h, int margin)
{
    v2d_t cam = level_editmode() ? editor_camera : camera_get_position();
    float a[4] = { x, y, x+w, y+h };
    float b[4] = {
        cam.x-VIDEO_SCREEN_W/2 - margin,
        cam.y-VIDEO_SCREEN_H/2 - margin,
        cam.x+VIDEO_SCREEN_W/2 + margin,
        cam.y+VIDEO_SCREEN_H/2 + margin
    };
    return bounding_box(a,b);
}

/* returns a list with every brick
 * inside an area of a given rectangle */
brick_list_t* brick_list_clip()
{
    brick_list_t *list = NULL, *p, *q;
    int bx, by, bw, bh;

    /* initial clipping */
    for(p=brick_list; p; p=p->next) {
        bx = min(p->data->x, p->data->sx);
        by = min(p->data->y, p->data->sy);
        bw = p->data->brick_ref->image->w;
        bh = p->data->brick_ref->image->h;
        if(inside_screen(bx,by,bw,bh,DEFAULT_MARGIN*2) || p->data->brick_ref->behavior == BRB_CIRCULAR) {
            q = mallocx(sizeof *q);
            q->data = p->data;
            q->next = list;
            list = q;
        }
    }

    return list;
}

/* returns a list with every item
 * inside an area of a given rectangle */
item_list_t* item_list_clip()
{
    item_list_t *list = NULL, *p, *q;
    int ix, iy, iw, ih;
    image_t *img;

    for(p=item_list; p; p=p->next) {
        img = actor_image(p->data->actor);
        ix = (int)p->data->actor->position.x;
        iy = (int)p->data->actor->position.y;
        iw = img->w;
        ih = img->h;
        if(inside_screen(ix,iy,iw,ih,DEFAULT_MARGIN)) {
            q = mallocx(sizeof *q);
            q->data = p->data;
            q->next = list;
            list = q;
        }
    }

    return list;
}

/* deletes the list generated by
 * brick_list_clip() */
void brick_list_unclip(brick_list_t *list)
{
    brick_list_t *next;

    while(list) {
        next = list->next;
        free(list);
        list = next;
    }
}


/* deletes the list generated by
 * item_list_clip() */
void item_list_unclip(item_list_t *list)
{
    item_list_t *next;

    while(list) {
        next = list->next;
        free(list);
        list = next;
    }
}


/* calculates the size of the
 * current level */
void update_level_size()
{
    int max_x, max_y;
    brick_list_t *p;

    max_x = max_y = -INFINITY;

    for(p=brick_list; p; p=p->next) {
        if(p->data->brick_ref->property != BRK_NONE) {
            max_x = max(max_x, p->data->sx + brick_image(p->data)->w);
            max_y = max(max_y, p->data->sy + brick_image(p->data)->h);
        }
    }

    level_width = max(max_x, VIDEO_SCREEN_W);
    level_height = max(max_y, VIDEO_SCREEN_H);
}

/* returns the ID of a given brick,
 * or -1 if it was not found */
int get_brick_id(brick_t *b)
{
    int i;

    for(i=0; i<brickdata_size(); i++) {
        if(b->brick_ref == brickdata_get(i))
            return i;
    }

    return -1;
}

/* comparsion routine. It returns:
 * <0 if a < b (a is shown behind b)
 * 0 if a == b (undefined)
 * >0 if a > b (a is shown in front of b)
 *
 * comparsion criteria:
 * 1. z-index
 * 2. obstacle bricks x passable bricks
 * 3. render walls/slopes before than floors/ceils
 * 4. y position
*/
int brick_sort_cmp(brick_t *a, brick_t *b)
{
    brickdata_t *ra = a->brick_ref, *rb = b->brick_ref;

    if(ra->zindex < rb->zindex)
        return -1;
    else if(ra->zindex > rb->zindex)
        return 1;
    else {
        /* we have the same z-index */
        if(ra->property != rb->property) {
            int score[] = {
                0,      /* BRK_NONE */
                100,    /* BRK_OBSTACLE */
                50      /* BRK_CLOUD */
            };
            return score[ra->property] - score[rb->property];
        }
        else {
            /* we also have the same brick property! */
            if((ra->angle % 180 != 0) && (rb->angle % 180 == 0))
                return -1;
            else if((ra->angle % 180 == 0) && (rb->angle % 180 != 0))
                return 1;
            else {
                /* we also have the same angle policy */
                return a->sy - b->sy; /* sort by ypos */
            }
        }
    }
}

/* inserts a brick into brick_list, a linked
 * list that's always sorted by brick_sort_cmp() */
void insert_brick_sorted(brick_list_t *b)
{
    /* b,p are unitary linked list nodes */
    brick_list_t *p;

    /* note that brick_list_clip() will reverse
     * part of this list later */
    if(brick_list) {
        if(brick_sort_cmp(b->data, brick_list->data) >= 0) {
            b->next = brick_list;
            brick_list = b;
        }
        else {
            p = brick_list;
            while(p->next && brick_sort_cmp(p->next->data, b->data) > 0)
                p = p->next;
            b->next = p->next;
            p->next = b;
        }
    }
    else {
        b->next = NULL;
        brick_list = b;
    }
}


/* restarts the level preserving
 * the current spawn point */
void restart()
{
    v2d_t sp = spawn_point;
    level_release();
    level_init();
    spawn_point = sp;
    spawn_players();
}



/* creates a fake brick (useful on
 * item-generated bricks) */
brick_t *create_fake_brick(int width, int height, v2d_t position, int angle)
{
    int i;
    brick_t *b = mallocx(sizeof *b);
    brickdata_t *d = mallocx(sizeof *d);

    d->data = NULL;
    d->image = image_create(width, height);
    d->angle = angle;
    d->property = BRK_OBSTACLE;
    d->behavior = BRB_DEFAULT;
    d->zindex = 0.5;
    for(i=0; i<BRICKBEHAVIOR_MAXARGS; i++)
        d->behavior_arg[i] = 0;

    b->brick_ref = d;
    b->animation_frame = 0;
    b->enabled = TRUE;
    b->x = b->sx = (int)position.x;
    b->y = b->sy = (int)position.y;
    for(i=0; i<BRICK_MAXVALUES; i++)
        b->value[i] = 0;

    return b;
}


/* destroys a previously created
 * fake brick */
void destroy_fake_brick(brick_t *b)
{
    image_destroy(b->brick_ref->image);
    free(b->brick_ref);
    free(b);
}


/* renders the players */
void render_players(int bring_to_back)
{
    int i;

    for(i=2; i>=0; i--) {
        if(team[i] != player && (team[i]->bring_to_back?TRUE:FALSE) == bring_to_back)
            player_render(team[i], camera_get_position());
    }

    if((player->bring_to_back?TRUE:FALSE) == bring_to_back) /* comparing two booleans */
        player_render(player, camera_get_position());
}


/* updates the music */
void update_music()
{
    if(music != NULL && !level_cleared && !block_music) {

        if(override_music && !sound_is_playing(override_music)) {
            logfile_message("update_music(): override sample finished, clearing override_music (invincible=%d, got_speedshoes=%d)", player->invincible, player->got_speedshoes);
            override_music = NULL;
            if(!player->invincible && !player->got_speedshoes)
                music_play(music, INFINITY);
        }

        if(!override_music && !music_is_playing()) {
            logfile_message("update_music(): fallback fired -- music not playing (music=%p, override_music=%p, block_music=%d), restarting", (void*)music, (void*)override_music, block_music);
            music_play(music, INFINITY);
        }

    }
}


/* puts the players at the spawn point */
void spawn_players()
{
    int i, v;

    for(i=0; i<3; i++) {
        v = ((int)spawn_point.x <= level_width/2) ? 2-i : i;
        team[i]->actor->mirror = ((int)spawn_point.x <= level_width/2) ? IF_NONE : IF_HFLIP;
        team[i]->actor->spawn_point.x = team[i]->actor->position.x = spawn_point.x + 15*v;
        team[i]->actor->spawn_point.y = team[i]->actor->position.y = spawn_point.y;
    }
}


/* renders the hud */
void render_hud()
{
    int i;
    v2d_t fixedcam = v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2);

    if(!level_cleared) {
        /* hud */
        actor_render(maingui, fixedcam);
        actor_render(lifegui, fixedcam);
        font_render(lifefnt, fixedcam);
        for(i=0;i<3;i++)
            font_render(mainfnt[i], fixedcam);

        /* powerups */
        render_powerups();
    }
    else {
        /* reached the goal */
        actor_render(actclear_levelact, fixedcam);
        font_render(actclear_teamname, fixedcam);
        font_render(actclear_gotthrough, fixedcam);
        for(i=0; i<ACTCLEAR_BONUSMAX; i++) {
            actor_render(actclear_bonus[i], fixedcam);
            font_render(actclear_bonusfnt[i], fixedcam);
        }
    }

    /* level opening */
    if(level_timer < 2.5) 
        image_clear(video_get_backbuffer(), image_rgb(0,0,0));
    actor_render(levelop, fixedcam);
    actor_render(levelact, fixedcam);
    font_render(leveltitle, fixedcam);

    /* dialog box */
    render_dlgbox(fixedcam);
}




/* removes every brick that satisfies (brick->state == BRS_DEAD) */
void remove_dead_bricks()
{
    brick_list_t *p, *next;

    if(!brick_list)
        return;

    /* first element (assumed to exist) */
    if(brick_list->data->state == BRS_DEAD) {
        next = brick_list->next;
        free(brick_list->data);
        free(brick_list);
        brick_list = next;
    }

    /* others */
    for(p=brick_list; p && p->next; p=p->next) {
        if(p->next->data->state == BRS_DEAD) {
            next = p->next;
            p->next = next->next;
            free(next->data);
            free(next);
        }
    }
}

/* removes the dead items */
void remove_dead_items()
{
    item_list_t *p, *next;

    if(!item_list)
        return;

    /* first element (assumed to exist) */
    if(item_list->data->state == IS_DEAD) {
        next = item_list->next;
        item_destroy(item_list->data);
        free(item_list);
        item_list = next;
    }

    /* others */
    for(p=item_list; p && p->next; p=p->next) {
        if(p->next->data->state == IS_DEAD) {
            next = p->next;
            p->next = next->next;
            item_destroy(next->data);
            free(next);
        }
    }
}

/* removes the dead objects */
void remove_dead_objects()
{
    enemy_list_t *p, *next;

    if(!enemy_list)
        return;

    /* first element (assumed to exist) */
    if(enemy_list->data->state == ES_DEAD) {
        next = enemy_list->next;
        enemy_destroy(enemy_list->data);
        free(enemy_list);
        enemy_list = next;
    }

    /* others */
    for(p=enemy_list; p && p->next; p=p->next) {
        if(p->next->data->state == ES_DEAD) {
            next = p->next;
            p->next = next->next;
            enemy_destroy(next->data);
            free(next);
        }
    }
}

/* updates the dialog box */
void update_dlgbox()
{
    float speed = 100.0; /* y speed */
    float dt = timer_get_delta();
    uint32 t = timer_get_ticks();

    if(dlgbox_active) {
        if(t >= dlgbox_starttime + DLGBOX_MAXTIME) {
            dlgbox_active = FALSE;
            return;
        }
        dlgbox->position.x = (VIDEO_SCREEN_W - actor_image(dlgbox)->w)/2;
        dlgbox->position.y = max(dlgbox->position.y - speed*dt, VIDEO_SCREEN_H - actor_image(dlgbox)->h*1.3);

    }
    else {
        dlgbox->position.y = min(dlgbox->position.y + speed*dt, VIDEO_SCREEN_H);
    }

    dlgbox_title->position = v2d_add(dlgbox->position, v2d_new(7, 8));
    dlgbox_message->position = v2d_add(dlgbox->position, v2d_new(7, 20));
}


/* renders the dialog box */
void render_dlgbox(v2d_t camera_position)
{
    actor_render(dlgbox, camera_position);
    font_render(dlgbox_title, camera_position);
    font_render(dlgbox_message, camera_position);
}


/* is this a boss level? */
int got_boss()
{
    return (boss != NULL);
}







/* camera facade */
void level_lock_camera(int x1, int y1, int x2, int y2) /* the camera can show any point in this rectangle */
{
    camera_lock(x1+VIDEO_SCREEN_W/2, y1+VIDEO_SCREEN_H/2, x2-VIDEO_SCREEN_W/2, y2-VIDEO_SCREEN_H/2);
}

void level_unlock_camera()
{
    camera_unlock();
}




/* music */
void level_restore_music()
{
    if(music != NULL)
        music_stop();

    /* update_music will do the rest */
}




/* particle programming */

/* particle_init(): initializes the particle module */
void particle_init()
{
    particle_list = NULL;
}


/* particle_release(): releases the particle module */
void particle_release()
{
    particle_list_t *it, *next;
    particle_t *p;

    for(it=particle_list; it; it=next) {
        p = it->data;
        next = it->next;

        image_destroy(p->image);
        free(p);
        free(it);
    }

    particle_list = NULL;
}


/* particle_update_all(): updates every particle on this level */
void particle_update_all(brick_list_t *brick_list)
{
    float dt = timer_get_delta(), g = level_gravity();
    int got_brick, inside_area;
    particle_list_t *it, *prev = NULL, *next;
    particle_t *p;

    for(it=particle_list; it; it=next) {
        p = it->data;
        next = it->next;
        inside_area = inside_screen(p->position.x, p->position.y, p->position.x+p->image->w, p->position.y+p->image->h, DEFAULT_MARGIN);

        /* collided with bricks? */
        got_brick = FALSE;
        if(p->destroy_on_brick && inside_area && p->speed.y > 0) {
            float a[4] = { p->position.x, p->position.y, p->position.x+p->image->w, p->position.y+p->image->h };
            brick_list_t *itb;
            for(itb=brick_list; itb && !got_brick; itb=itb->next) {
                brick_t *brk = itb->data;
                if(brk->brick_ref->property == BRK_OBSTACLE && brk->brick_ref->angle == 0) {
                    float b[4] = { brk->x, brk->y, brk->x+brk->brick_ref->image->w, brk->y+brk->brick_ref->image->h };
                    if(bounding_box(a,b))
                        got_brick = TRUE;
                }
            }
        }

        /* update particle */
        if(!inside_area || got_brick) {
            /* remove this particle */
            if(prev)
                prev->next = next;
            else
                particle_list = next;

            image_destroy(p->image);
            free(p);
            free(it);
        }
        else {
            /* update this particle */
            p->position.x += p->speed.x*dt;
            p->position.y += p->speed.y*dt + 0.5*g*(dt*dt);
            p->speed.y += g*dt;
            prev = it;
        }
    }
}


/* particle_render_all(): renders the particles */
void particle_render_all()
{
    particle_list_t *it;
    particle_t *p;
    v2d_t topleft = v2d_new(camera_get_position().x-VIDEO_SCREEN_W/2, camera_get_position().y-VIDEO_SCREEN_H/2);

    for(it=particle_list; it; it=it->next) {
        p = it->data;
        image_draw(p->image, video_get_backbuffer(), (int)(p->position.x-topleft.x), (int)(p->position.y-topleft.y), IF_NONE);
    }
}





/* dialog regions */


/* update_dialogregions(): updates all the dialog regions.
 * This checks if the player enters one region, and if he/she does,
 * this function shows the corresponding dialog box */
void update_dialogregions()
{
    int i;
    float a[4], b[4];

    if(level_timer < 2.0)
        return;

    a[0] = player->actor->position.x;
    a[1] = player->actor->position.y;
    a[2] = a[0] + actor_image(player->actor)->w;
    a[3] = a[1] + actor_image(player->actor)->h;

    for(i=0; i<dialogregion_size; i++) {
        if(dialogregion[i].disabled)
            continue;

        b[0] = dialogregion[i].rect_x;
        b[1] = dialogregion[i].rect_y;
        b[2] = b[0]+dialogregion[i].rect_w;
        b[3] = b[1]+dialogregion[i].rect_h;

        if(bounding_box(a, b)) {
            dialogregion[i].disabled = TRUE;
            level_call_dialogbox(dialogregion[i].title, dialogregion[i].message);
            break;
        }
    }
}



/* moveable platforms */


/*
 * level_brick_move_actor()
 * If the given brick moves, then the actor
 * must move too. Returns a delta_speed vector
 */
v2d_t level_brick_move_actor(brick_t *brick, actor_t *act)
{
    float t, rx, ry, sx, sy, ph;

    if(!brick)
        return v2d_new(0,0);

    t = brick->value[0]; /* time elapsed ONLY FOR THIS brick */
    switch(brick->brick_ref->behavior) {
        case BRB_CIRCULAR:
            rx = brick->brick_ref->behavior_arg[0];             /* x-dist */
            ry = brick->brick_ref->behavior_arg[1];             /* y-dist */
            sx = brick->brick_ref->behavior_arg[2] * (2*PI);    /* x-speed */
            sy = brick->brick_ref->behavior_arg[3] * (2*PI);    /* y-speed */
            ph = brick->brick_ref->behavior_arg[4] * PI/180.0;  /* initial phase */

            /* take the derivative. e.g.,
               d[ sx + A*cos(PI*t) ]/dt = -A*PI*sin(PI*t) */
            return v2d_new( (-rx*sx)*sin(sx*t+ph), (ry*sy)*cos(sy*t+ph) );

        default:
            return v2d_new(0,0);
    }
}



/* brick_move()
 * moves a brick depending on its type */
void brick_move(brick_t *brick)
{
    float t, rx, ry, sx, sy, ph;

    if(!brick)
        return;

    brick->value[0] += timer_get_delta();
    t = brick->value[0]; /* time elapsed ONLY FOR THIS brick */
    switch(brick->brick_ref->behavior) {
        case BRB_CIRCULAR:
            rx = brick->brick_ref->behavior_arg[0];             /* x-dist */
            ry = brick->brick_ref->behavior_arg[1];             /* y-dist */
            sx = brick->brick_ref->behavior_arg[2] * (2*PI);    /* x-speed */
            sy = brick->brick_ref->behavior_arg[3] * (2*PI);    /* y-speed */
            ph = brick->brick_ref->behavior_arg[4] * PI/180.0;  /* initial phase */

            brick->x = brick->sx + round(rx*cos(sx*t+ph));
            brick->y = brick->sy + round(ry*sin(sy*t+ph));
            break;

        default:
            break;
    }
}


/* misc */

/* render powerups */
void render_powerups()
{
    image_t *icon[MAX_POWERUPS]; /* icons */
    int visible[MAX_POWERUPS]; /* is icon[i] visible? */
    int i, c = 0; /* c is the icon count */
    float t = timer_get_ticks() * 0.001;

    for(i=0; i<MAX_POWERUPS; i++)
        visible[i] = TRUE;

    if(player) {
        if(player->got_glasses)
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 6) , 0 );

        switch (player->shield_type)
        {
        case SH_SHIELD:
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 7) , 0 );
            break;
        case SH_FIRESHIELD:
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 11) , 0 );
            break;
        case SH_THUNDERSHIELD:
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 12) , 0 );
            break;
        case SH_WATERSHIELD:
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 13) , 0 );
            break;
        case SH_ACIDSHIELD:
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 14) , 0 );
            break;
        case SH_WINDSHIELD:
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 15) , 0 );
            break;
        }

        if(player->invincible) {
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 4) , 0 );
            if(player->invtimer >= PLAYER_MAX_INVINCIBILITY*0.75) { /* it blinks */
                /* we want something that blinks faster as player->invtimer tends to PLAYER_MAX_INVINCIBLITY */
                float x = ((PLAYER_MAX_INVINCIBILITY-player->invtimer)/(PLAYER_MAX_INVINCIBILITY*0.25)); /* 1 = x --> 0 */
                visible[c-1] = sin( (0.5*PI*t) / (x+0.1) ) >= 0;
            }
        }

        if(player->got_speedshoes) {
            icon[c++] = sprite_get_image( sprite_get_animation("SD_ICON", 5) , 0 );
            if(player->speedshoes_timer >= PLAYER_MAX_SPEEDSHOES*0.75) { /* it blinks */
                /* we want something that blinks faster as player->speedshoes_timer tends to PLAYER_MAX_SPEEDSHOES */
                float x = ((PLAYER_MAX_SPEEDSHOES-player->speedshoes_timer)/(PLAYER_MAX_SPEEDSHOES*0.25)); /* 1 = x --> 0 */
                visible[c-1] = sin( (0.5*PI*t) / (x+0.1) ) >= 0;
            }
        }
    }

    for(i=0; i<c; i++) {
        if(visible[i])
            image_draw(icon[i], video_get_backbuffer(), VIDEO_SCREEN_W - (icon[i]->w+5)*(i+1), 5, IF_NONE);
    }
}






/*
                                                                                                              
                                                                                                              
                                                                                                              
                                                                  MZZM                                        
                                                       MMMMMMMMMMMZOOZM MMMMMMMMM                             
             MM                                       M=...........MOOOM=.........M       MM          MM      
             MM                             MM          M7......77MOOOOOOM7.....77M             MM    MM      
             MM                             MM          M=......7MOOOOOMM=.....77M              MM    MM      
             MM    MM     MM     MMMMM    MMMMMM        M=......7MOOOMM==....77M          MM  MMMMMM  MM      
             MM    MM     MM    MMMMMMMM  MMMMMM        M=......7MOOOM==....77M           MM  MMMMMM  MM      
             MM    MM     MM   MM           MM         MM=......7MOM==....77MOOOM         MM    MM    MM      
             MM    MM     MM   MMMM         MM        MZM=......7MM==....77MOOOOOM        MM    MM    MM      
             MM    MM     MM      MMMMMM    MM      MZOOM=......7==....77MOOOOOOOOOM      MM    MM    MM      
     MM      MM    MM     MM           MM   MM       MZOM=......7....77MOOOOOOOOOOM       MM    MM            
      MM    MM      MM   MMM   MM     MMM   MM        MZM=..........MMMOOOOOOOOOOM        MM    MM            
        MMMM          MMM MM     MMMMMM      MMM        M=.........M..MOOOOOOOOM          MM     MMM  MM      
                                                        M=........7MMMOMMMOMMMMMM                             
                                                        M=......77MM..M...........M                           
                                                        M=....77MOM..MM..OM..MM..M                            
                                                        M=...77MOOM..MM..MM..MM..M                            
                                                         M.77M  MM...M..MM..MM...M                            
                                                          MMM    MMMMOMM  MM  MMM                             
                                                                   MM                                         
                                                                                                              
*/





/* Level Editor */



/*
 * editor_init()
 * Initializes the level editor data
 */
void editor_init()
{
    logfile_message("editor_init()");

    /* intializing... */
    editor_enabled = FALSE;
    editor_item_list_size = -1;
    while(editor_item_list[++editor_item_list_size] >= 0) { }
    editor_cursor_objtype = EDT_ITEM;
    editor_cursor_objid = 0;
    editor_previous_video_resolution = video_get_resolution();
    editor_previous_video_smooth = video_is_smooth();
    editor_enemy_name = objects_get_list_of_names(&editor_enemy_name_length);

    /* creating objects */
    editor_bgimage = image_load(EDITOR_BGFILE);
    editor_keyboard = input_create_keyboard(editor_keybmap);
    editor_keyboard2 = input_create_keyboard(editor_keybmap2);
    editor_mouse = input_create_mouse();
    editor_cursor_font = font_create(8);
    editor_properties_font = font_create(8);

    /* groups */
    editorgrp_init();

    /* grid */
    editor_grid_init();

    /* done */
    logfile_message("editor_init() ok");
}


/*
 * editor_release()
 * Releases the level editor data
 */
void editor_release()
{
    logfile_message("editor_release()");

    /* grid */
    editor_grid_release();

    /* groups */
    editorgrp_release();

    /* destroying objects */
    image_unref(EDITOR_BGFILE);
    input_destroy(editor_keyboard2);
    input_destroy(editor_keyboard);
    input_destroy(editor_mouse);
    font_destroy(editor_properties_font);
    font_destroy(editor_cursor_font);

    /* releasing... */
    editor_enabled = FALSE;
    editor_cursor_objtype = EDT_ITEM;
    editor_cursor_objid = 0;

    logfile_message("editor_release() ok");
}


/*
 * editor_update()
 * Updates the level editor
 */
void editor_update()
{
    item_list_t *it, *major_items;
    brick_list_t *major_bricks;
    image_t *cursor_arrow = sprite_get_image(sprite_get_animation("SD_ARROW", 0), 0);
    int w = font_get_charsize(editor_cursor_font).x;
    int h = font_get_charsize(editor_cursor_font).y;
    int pick_object, delete_object = FALSE;
    v2d_t topleft = v2d_subtract(editor_camera, v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2));

    /* update items */
    major_items = item_list_clip();
    major_bricks = brick_list_clip();
    for(it=major_items; it!=NULL; it=it->next)
        item_update(it->data, team, 3, major_bricks, item_list /*major_items*/, enemy_list); /* major_items bugs the switch/teleporter */
    brick_list_unclip(major_bricks);
    item_list_unclip(major_items);

    /* save the level */
    if(input_button_down(editor_keyboard, IB_FIRE3)) {
        if(input_button_pressed(editor_keyboard, IB_FIRE4))
            editor_save();
    }

    /* disable the level editor */
    if(input_button_pressed(editor_keyboard, IB_FIRE4)) {
        editor_disable();
        return;
    }

    /* change category / object */
    if(input_button_down(editor_keyboard, IB_FIRE3)) {
        /* change category */
        if(input_button_pressed(editor_keyboard, IB_FIRE1) || input_button_pressed(editor_mouse, IB_DOWN))
            editor_next_category();

        if(input_button_pressed(editor_keyboard, IB_FIRE2) || input_button_pressed(editor_mouse, IB_UP))
            editor_previous_category();
    }
    else {
        /* change object */
        if(input_button_pressed(editor_keyboard, IB_FIRE1) || input_button_pressed(editor_mouse, IB_DOWN))
            editor_next_object();

        if(input_button_pressed(editor_keyboard, IB_FIRE2) || input_button_pressed(editor_mouse, IB_UP))
            editor_previous_object();
    }

    /* mouse cursor */
    editor_cursor.x = clip(input_get_xy(editor_mouse).x, 0, VIDEO_SCREEN_W-cursor_arrow->w);
    editor_cursor.y = clip(input_get_xy(editor_mouse).y, 0, VIDEO_SCREEN_H-cursor_arrow->h);

    /* new spawn point */
    if(input_button_pressed(editor_mouse, IB_FIRE1) && input_button_down(editor_keyboard, IB_FIRE3)) {
        v2d_t nsp = editor_grid_snap(editor_cursor);
        editor_action_t eda = editor_action_spawnpoint_new(TRUE, nsp, spawn_point);
        editor_action_commit(eda);
        editor_action_register(eda);
    }

    /* new object */
    if(input_button_pressed(editor_mouse, IB_FIRE1) && !input_button_down(editor_keyboard, IB_FIRE3)) {
        editor_action_t eda = editor_action_entity_new(TRUE, editor_cursor_objtype, editor_cursor_objid, editor_grid_snap(editor_cursor));
        editor_action_commit(eda);
        editor_action_register(eda);
    }

    /* pick or delete object */
    pick_object = input_button_pressed(editor_mouse, IB_FIRE3) || input_button_pressed(editor_keyboard2, IB_FIRE4);
    delete_object = input_button_pressed(editor_mouse, IB_FIRE2);
    if(pick_object || delete_object) {
        brick_list_t *itb;
        item_list_t *iti;
        enemy_list_t *ite;

        switch(editor_cursor_objtype) {
            /* brick */
            case EDT_BRICK:
                for(itb=brick_list;itb;itb=itb->next) {
                    float a[4] = {itb->data->x, itb->data->y, itb->data->x + itb->data->brick_ref->image->w, itb->data->y + itb->data->brick_ref->image->h};
                    float b[4] = { editor_cursor.x+topleft.x , editor_cursor.y+topleft.y , editor_cursor.x+topleft.x+1 , editor_cursor.y+topleft.y+1 };
                    if(bounding_box(a,b)) {
                        if(pick_object) {
                            editor_cursor_objid = get_brick_id(itb->data);
                        }
                        else {
                            editor_action_t eda = editor_action_entity_new(FALSE, EDT_BRICK, get_brick_id(itb->data), v2d_new(itb->data->x, itb->data->y));
                            editor_action_commit(eda);
                            editor_action_register(eda);
                            break;
                        }
                    }
                }
                break;

            /* item */
            case EDT_ITEM:
                for(iti=item_list;iti;iti=iti->next) {
                    float a[4] = {iti->data->actor->position.x-iti->data->actor->hot_spot.x, iti->data->actor->position.y-iti->data->actor->hot_spot.y, iti->data->actor->position.x-iti->data->actor->hot_spot.x + actor_image(iti->data->actor)->w, iti->data->actor->position.y-iti->data->actor->hot_spot.y + actor_image(iti->data->actor)->h};
                    float b[4] = { editor_cursor.x+topleft.x , editor_cursor.y+topleft.y , editor_cursor.x+topleft.x+1 , editor_cursor.y+topleft.y+1 };

                    if(bounding_box(a,b)) {
                        if(pick_object) {
                            int index = editor_item_list_get_index(iti->data->type);
                            if(index >= 0) {
                                editor_cursor_itemid = index;
                                editor_cursor_objid = editor_item_list[index];
                            }
                        }
                        else {
                            editor_action_t eda = editor_action_entity_new(FALSE, EDT_ITEM, iti->data->type, iti->data->actor->position);
                            editor_action_commit(eda);
                            editor_action_register(eda);
                            break;
                        }
                    }
                }
                break;

            /* enemy */
            case EDT_ENEMY:
                for(ite=enemy_list;ite;ite=ite->next) {
                    float a[4] = {ite->data->actor->position.x-ite->data->actor->hot_spot.x, ite->data->actor->position.y-ite->data->actor->hot_spot.y, ite->data->actor->position.x-ite->data->actor->hot_spot.x + actor_image(ite->data->actor)->w, ite->data->actor->position.y-ite->data->actor->hot_spot.y + actor_image(ite->data->actor)->h};
                    float b[4] = { editor_cursor.x+topleft.x , editor_cursor.y+topleft.y , editor_cursor.x+topleft.x+1 , editor_cursor.y+topleft.y+1 };
                    int mykey = editor_enemy_name2key(ite->data->name);
                    if(mykey >= 0 && bounding_box(a,b)) {
                        if(pick_object) {
                            editor_cursor_objid = mykey;
                        }
                        else {
                            editor_action_t eda = editor_action_entity_new(FALSE, EDT_ENEMY, mykey, ite->data->actor->position);
                            editor_action_commit(eda);
                            editor_action_register(eda);
                            break;
                        }
                    }
                }
                break;

            /* can't pick-up/delete a group */
            case EDT_GROUP:
                break;
        }
    }

    /* undo & redo */
    if(input_button_down(editor_keyboard, IB_FIRE3)) {
        if(input_button_pressed(editor_keyboard2, IB_FIRE1))
            editor_action_undo();
        else if(input_button_pressed(editor_keyboard2, IB_FIRE2))
            editor_action_redo();
    }

    /* grid */
    editor_grid_update();

    /* scrolling */
    editor_scroll();

    /* cursor coordinates */
    font_set_text(editor_cursor_font, "%d,%d", (int)editor_grid_snap(editor_cursor).x, (int)editor_grid_snap(editor_cursor).y);
    editor_cursor_font->position.x = clip((int)editor_cursor.x, 10, VIDEO_SCREEN_W-w*strlen(font_get_text(editor_cursor_font))-10);
    editor_cursor_font->position.y = clip((int)editor_cursor.y-3*h, 10, VIDEO_SCREEN_H-10);

    /* object properties */
    editor_properties_font->position = v2d_new(10, 10);
    
    if(editor_cursor_objtype != EDT_ENEMY) {
        font_set_text(
            editor_properties_font,
            "<color=ffff00>%s %d</color>\n%s",
            editor_object_category(editor_cursor_objtype),
            editor_cursor_objid,
            editor_object_info(editor_cursor_objtype, editor_cursor_objid)
        );
    }
    else {
        font_set_text(
            editor_properties_font,
            "<color=ffff00>%s \"%s\"</color>\n%s",
            editor_object_category(editor_cursor_objtype),
            str_addslashes(editor_enemy_key2name(editor_cursor_objid)),
            editor_object_info(editor_cursor_objtype, editor_cursor_objid)
        );
    }
}



/*
 * editor_render()
 * Renders the level editor
 */
void editor_render()
{
    image_t *cursor_arrow;
    v2d_t topleft = v2d_subtract(editor_camera, v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2));

    /* background */
    editor_render_background();

    /* grid */
    editor_grid_render();

    /* entities */
    render_entities();

    /* drawing the object */
    editor_draw_object(editor_cursor_objtype, editor_cursor_objid, v2d_subtract(editor_grid_snap(editor_cursor), topleft));

    /* drawing the cursor arrow */
    cursor_arrow = sprite_get_image(sprite_get_animation("SD_ARROW", 0), 0);
    image_draw(cursor_arrow, video_get_backbuffer(), (int)editor_cursor.x, (int)editor_cursor.y, IF_NONE);

    /* cursor coordinates */
    font_render(editor_cursor_font, v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2));

    /* object properties */
    font_render(editor_properties_font, v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2));
}



/*
 * editor_enable()
 * Enables the level editor
 */
void editor_enable()
{
    logfile_message("editor_enable()");

    /* activating the editor */
    editor_action_init();
    editor_enabled = TRUE;
    editor_camera.x = (int)camera_get_position().x;
    editor_camera.y = (int)camera_get_position().y;
    editor_cursor = v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2);
    video_showmessage("Welcome to the Level Editor! Read readme.html to know how to use it.");

    /* changing the video resolution */
    editor_previous_video_resolution = video_get_resolution();
    editor_previous_video_smooth = video_is_smooth();
    video_changemode(VIDEORESOLUTION_EDT, FALSE, video_is_fullscreen());

    logfile_message("editor_enable() ok");
}


/*
 * editor_disable()
 * Disables the level editor
 */
void editor_disable()
{
    logfile_message("editor_disable()");

    /* disabling the level editor */
    update_level_size();
    editor_action_release();
    editor_enabled = FALSE;

    /* restoring the video resolution */
    video_changemode(editor_previous_video_resolution, editor_previous_video_smooth, video_is_fullscreen());

    logfile_message("editor_disable() ok");
}


/*
 * editor_is_enabled()
 * Is the level editor activated?
 */
int editor_is_enabled()
{
    return editor_enabled;
}

/*
 * editor_want_to_activate()
 * The level editor wants to be activated!
 */
int editor_want_to_activate()
{
    return input_button_pressed(editor_keyboard, IB_FIRE4);
}


/*
 * editor_render_background()
 * Renders the background image of the
 * level editor
 */
void editor_render_background()
{
    float x = VIDEO_SCREEN_W/editor_bgimage->w;
    float y = VIDEO_SCREEN_H/editor_bgimage->h;
    image_draw_scaled(editor_bgimage, video_get_backbuffer(), 0, 0, v2d_new(x,y), IF_NONE);
}


/*
 * editor_save()
 * Saves the level
 */
void editor_save()
{
    level_save(file);
    sound_play( soundfactory_get("level saved") );
    video_showmessage("Level saved.");
}


/*
 * editor_scroll()
 * Scrolling - "free-look mode"
 */
void editor_scroll()
{
    float camera_speed;
    float dt = timer_get_delta();

    /* camera speed */
    if(input_button_down(editor_keyboard, IB_FIRE3))
        camera_speed = 5 * 750;
    else
        camera_speed = 750;

    /* scrolling... */
    if(input_button_down(editor_keyboard, IB_UP) || input_button_down(editor_keyboard2, IB_UP))
        editor_camera.y -= camera_speed*dt;

    if(input_button_down(editor_keyboard, IB_DOWN) || input_button_down(editor_keyboard2, IB_DOWN))
        editor_camera.y += camera_speed*dt;

    if(input_button_down(editor_keyboard, IB_LEFT) || input_button_down(editor_keyboard2, IB_LEFT))
        editor_camera.x-= camera_speed*dt;

    if(input_button_down(editor_keyboard, IB_RIGHT) || input_button_down(editor_keyboard2, IB_RIGHT))
        editor_camera.x += camera_speed*dt;

    /* make sure it doesn't go off the bounds */
    editor_camera.x = (int)max(editor_camera.x, VIDEO_SCREEN_W/2);
    editor_camera.y = (int)max(editor_camera.y, VIDEO_SCREEN_H/2);
    camera_set_position(editor_camera);
}



/* private stuff (level editor) */



/* returns a string containing a text
 * that corresponds to the given editor category
 * object id */
const char *editor_object_category(enum editor_object_type objtype)
{
    switch(objtype) {
        case EDT_BRICK:
            return "brick";

        case EDT_ITEM:
            return "built-in item";

        case EDT_ENEMY:
            return "object";

        case EDT_GROUP:
            return "group";
    }

    return "unknown";
}


/* returns a string containing information
 * about a given object */
const char *editor_object_info(enum editor_object_type objtype, int objid)
{
    static char buf[128];
    strcpy(buf, "");

    switch(objtype) {
        case EDT_BRICK: {
            brickdata_t *x = brickdata_get(objid);
            if(x && x->image)
                sprintf(buf, "angle: %d\nsize: %dx%d\nproperty: %s\nbehavior: %s\nzindex: %.2lf", x->angle, x->image->w, x->image->h, brick_get_property_name(x->property), brick_get_behavior_name(x->behavior), x->zindex);
            else
                sprintf(buf, "WARNING: missing brick");
            break;
        }

        case EDT_ITEM: {
            item_t *x = item_create(objid);
            sprintf(buf, "obstacle: %s\nbring_to_back: %s", x->obstacle ? "TRUE" : "FALSE", x->bring_to_back ? "TRUE" : "FALSE");
            item_destroy(x);
            break;
        }

        case EDT_ENEMY: {
            break;
        }

        case EDT_GROUP: {
            break;
        }
    }

    return buf;
}


/* jump to next category */
void editor_next_category()
{
    editor_cursor_objtype =
    (editor_cursor_objtype == EDT_BRICK) ? EDT_ITEM :
    (editor_cursor_objtype == EDT_ITEM) ? EDT_ENEMY :
    (editor_cursor_objtype == EDT_ENEMY) ? EDT_GROUP :
    (editor_cursor_objtype == EDT_GROUP) ? EDT_BRICK :
    editor_cursor_objtype;

    editor_cursor_objid = 0;
    editor_cursor_itemid = 0;

    if(editor_cursor_objtype == EDT_GROUP && editorgrp_group_count() == 0)
        editor_next_category();

    if(editor_cursor_objtype == EDT_ENEMY && editor_enemy_name_length == 0)
        editor_next_category();
}


/* jump to previous category */
void editor_previous_category()
{
    editor_cursor_objtype =
    (editor_cursor_objtype == EDT_ITEM) ? EDT_BRICK :
    (editor_cursor_objtype == EDT_ENEMY) ? EDT_ITEM :
    (editor_cursor_objtype == EDT_GROUP) ? EDT_ENEMY :
    (editor_cursor_objtype == EDT_BRICK) ? EDT_GROUP :
    editor_cursor_objtype;

    editor_cursor_objid = 0;
    editor_cursor_itemid = 0;

    if(editor_cursor_objtype == EDT_GROUP && editorgrp_group_count() == 0)
        editor_previous_category();

    if(editor_cursor_objtype == EDT_ENEMY && editor_enemy_name_length == 0)
        editor_previous_category();
}


/* select the next object */
void editor_next_object()
{
    int size;

    switch(editor_cursor_objtype) {
        /* brick */
        case EDT_BRICK: {
            size = brickdata_size();
            editor_cursor_objid = (editor_cursor_objid + 1) % size;
            if(brickdata_get(editor_cursor_objid) == NULL)
                editor_next_object(); /* invalid brick? */
            break;
        }

        /* item */
        case EDT_ITEM: {
            size = editor_item_list_size;
            editor_cursor_itemid = (editor_cursor_itemid + 1) % size;
            editor_cursor_objid = editor_item_list[editor_cursor_itemid];
            break;
        }

        /* enemy */
        case EDT_ENEMY: {
            size = editor_enemy_name_length;
            editor_cursor_objid = (editor_cursor_objid + 1) % size;
            break;
        }

        /* group */
        case EDT_GROUP: {
            size = editorgrp_group_count();
            editor_cursor_objid = (editor_cursor_objid + 1) % size;
            break;
        }
    }
}


/* select the previous object */
void editor_previous_object()
{
    int size;

    switch(editor_cursor_objtype) {
        /* brick */
        case EDT_BRICK: {
            size = brickdata_size();
            editor_cursor_objid = ((editor_cursor_objid - 1) + size) % size;
            if(brickdata_get(editor_cursor_objid) == NULL)
                editor_previous_object(); /* invalid brick? */
            break;
        }

        /* item */
        case EDT_ITEM: {
            size = editor_item_list_size;
            editor_cursor_itemid = ((editor_cursor_itemid - 1) + size) % size;
            editor_cursor_objid = editor_item_list[editor_cursor_itemid];
            break;
        }

        /* enemy */
        case EDT_ENEMY: {
            size = editor_enemy_name_length;
            editor_cursor_objid = ((editor_cursor_objid - 1) + size) % size;
            break;
        }

        /* group */
        case EDT_GROUP: {
            size = editorgrp_group_count();
            editor_cursor_objid = ((editor_cursor_objid - 1) + size) % size;
            break;
        }
    }
}


/* returns the index of item_id on
 * editor_item_list or -1 if the search fails */
int editor_item_list_get_index(int item_id)
{
    int i;

    for(i=0; i<editor_item_list_size; i++) {
        if(item_id == editor_item_list[i])
            return i;
    }

    return -1;
}


/*
 * editor_is_valid_item()
 * Is the given item valid to be used in
 * the level editor?
 */
int editor_is_valid_item(int item_id)
{
    return (editor_item_list_get_index(item_id) != -1);
}



/* draws the given object at [position] */
void editor_draw_object(enum editor_object_type obj_type, int obj_id, v2d_t position)
{
    image_t *cursor = NULL;
    v2d_t offset = v2d_new(0, 0);

    /* getting the image of the current object */
    switch(obj_type) {
        case EDT_BRICK: {
            if(brickdata_get(obj_id) != NULL) {
                cursor = brickdata_get(obj_id)->image;
            }
            break;
        }
        case EDT_ITEM: {
            item_t *item = item_create(obj_id);
            if(item != NULL) {
                cursor = actor_image(item->actor);
                offset = item->actor->hot_spot;
                offset.y -= 2;
                item_destroy(item);
            }
            break;
        }
        case EDT_ENEMY: {
            enemy_t *enemy = enemy_create(editor_enemy_key2name(obj_id));
            if(enemy != NULL) {
                cursor = actor_image(enemy->actor);
                offset = enemy->actor->hot_spot;
                offset.y -= 2;
                enemy_destroy(enemy);
            }
            break;
        }
        case EDT_GROUP: {
            editorgrp_entity_list_t *list, *it;
            list = editorgrp_get_group(obj_id);
            for(it=list; it; it=it->next) {
                enum editor_object_type my_type = EDITORGRP_ENTITY_TO_EDT(it->entity.type);
                editor_draw_object(my_type, it->entity.id, v2d_add(position, it->entity.position));
            }
            break;
        }
    }

    /* drawing the object */
    if(cursor != NULL)
        image_draw_trans(cursor, video_get_backbuffer(), (int)(position.x-offset.x), (int)(position.y-offset.y), image_rgb(255,255,255), 0.5, IF_NONE);
}


/* level editor: enemy name list */
int editor_enemy_name2key(const char *name)
{
    int i;

    for(i=0; i<editor_enemy_name_length; i++) {
        if(strcmp(name, editor_enemy_name[i]) == 0)
            return i;
    }

    return -1; /* not found */
}

const char* editor_enemy_key2name(int key)
{
    key = clip(key, 0, editor_enemy_name_length-1);
    return editor_enemy_name[key];
}




/* level editor: grid */

/* initializes the grid module */
void editor_grid_init()
{
    editor_grid_enabled = FALSE;
}


/* releases the grid module */
void editor_grid_release()
{
}


/* updates the grid module */
void editor_grid_update()
{
    if(input_button_pressed(editor_keyboard2, IB_FIRE3))
        editor_grid_enabled = !editor_grid_enabled;
}


/* renders the grid */
void editor_grid_render()
{
    if(editor_grid_enabled) {
        int i, j;
        image_t *grid;
        uint32 color;
        v2d_t topleft = v2d_subtract(editor_camera, v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2));

        /* creating the grid image */
        grid = image_create(EDITOR_GRID_W, EDITOR_GRID_H);
        color = image_rgb(0,128,160);
        image_clear(grid, video_get_maskcolor());
        for(i=0; i<grid->h; i++)
            image_putpixel(grid, grid->w-1, i, color);
        for(i=0; i<grid->w; i++)
            image_putpixel(grid, i, grid->h-1, color);

        /* drawing the grid... */
        for(i=0; i<=VIDEO_SCREEN_W/EDITOR_GRID_W; i++) {
            for(j=0; j<=VIDEO_SCREEN_H/EDITOR_GRID_H; j++) {
                v2d_t v = v2d_subtract(editor_grid_snap(v2d_new(i*grid->w, j*grid->h)), topleft);
                image_draw(grid, video_get_backbuffer(), (int)v.x, (int)v.y, IF_NONE);
            }
        }

        /* done! */
        image_destroy(grid);
    }
}


/* returns the size of the grid */
v2d_t editor_grid_size()
{
    if(!editor_grid_enabled)
        return v2d_new(1,1);
    else
        return v2d_new(8,8);
}

/* aligns position to a cell in the grid */
v2d_t editor_grid_snap(v2d_t position)
{
    v2d_t topleft = v2d_subtract(editor_camera, v2d_new(VIDEO_SCREEN_W/2, VIDEO_SCREEN_H/2));

    int w = EDITOR_GRID_W;
    int h = EDITOR_GRID_H;
    int cx = (int)topleft.x % w;
    int cy = (int)topleft.y % h;

    int xpos = -cx + ((int)position.x / w) * w;
    int ypos = -cy + ((int)position.y / h) * h;

    return v2d_add(topleft, v2d_new(xpos, ypos));
}




/* level editor actions */



/* action: constructor (entity) */
editor_action_t editor_action_entity_new(int is_new_object, enum editor_object_type obj_type, int obj_id, v2d_t obj_position)
{
    editor_action_t o;
    o.type = is_new_object ? EDA_NEWOBJECT : EDA_DELETEOBJECT;
    o.obj_type = obj_type;
    o.obj_id = obj_id;
    o.obj_position = obj_position;
    return o;
}

/* action: constructor (spawn point) */
editor_action_t editor_action_spawnpoint_new(int is_changing, v2d_t obj_position, v2d_t obj_old_position)
{
    editor_action_t o;
    o.type = is_changing ? EDA_CHANGESPAWN : EDA_RESTORESPAWN;
    o.obj_position = obj_position;
    o.obj_old_position = obj_old_position;
    return o;
}

/* initializes the editor_action module */
void editor_action_init()
{
    /* linked list */
    editor_action_buffer_head = mallocx(sizeof *editor_action_buffer_head);
    editor_action_buffer_head->in_group = FALSE;
    editor_action_buffer_head->prev = NULL;
    editor_action_buffer_head->next = NULL;
    editor_action_buffer = editor_action_buffer_head;
    editor_action_buffer_cursor = editor_action_buffer_head;
}


/* releases the editor_action module */
void editor_action_release()
{
    /* linked list */
    editor_action_buffer_head = editor_action_delete_list(editor_action_buffer_head);
    editor_action_buffer = NULL;
    editor_action_buffer_cursor = NULL;
}

/* registers a new editor_action */
void editor_action_register(editor_action_t action)
{
    /* ugly, but these fancy group stuff
     * shouldn't be availiable on the interface */
    static int registering_group = FALSE;
    static uint32 group_key;

    if(action.obj_type != EDT_GROUP) {
        editor_action_list_t *c, *it, *node;

        /* creating new node */
        node = mallocx(sizeof *node);
        node->action = action;
        node->in_group = registering_group;
        if(node->in_group)
            node->group_key = group_key;

        /* cursor */
        c = editor_action_buffer_cursor;
        if(c != NULL)
            c->next = editor_action_delete_list(c->next);

        /* inserting the node into the linked list */
        it = editor_action_buffer;
        while(it->next != NULL)
            it = it->next;
        it->next = node;
        node->prev = it;
        node->next = NULL;

        /* updating the cursor */
        editor_action_buffer_cursor = node;
    }
    else {
        static uint32 auto_increment = 0xbeef; /* dummy value */
        editorgrp_entity_list_t *list, *it;

        /* registering a group of objects */
        registering_group = TRUE;
        group_key = auto_increment++;
        list = editorgrp_get_group(action.obj_id);
        for(it=list; it; it=it->next) {
            editor_action_t a;
            editorgrp_entity_t e = it->entity;
            enum editor_object_type my_type = EDITORGRP_ENTITY_TO_EDT(e.type);
            a = editor_action_entity_new(TRUE, my_type, e.id, v2d_add(e.position, action.obj_position));
            editor_action_register(a);
        }
        registering_group = FALSE;
    }
}

/* deletes an editor_action list */
editor_action_list_t* editor_action_delete_list(editor_action_list_t *list)
{
    editor_action_list_t *p, *next;

    p = list;
    while(p != NULL) {
        next = p->next;
        free(p);
        p = next;
    }

    return NULL;
}

/* undo */
void editor_action_undo()
{
    editor_action_list_t *p;
    editor_action_t a;

    if(editor_action_buffer_cursor != editor_action_buffer_head) {
        /* moving the cursor */
        p = editor_action_buffer_cursor;
        editor_action_buffer_cursor = editor_action_buffer_cursor->prev;

        /* UNDOing a group? */
        if(p->in_group && p->prev && p->prev->in_group && p->group_key == p->prev->group_key)
            editor_action_undo();

        /* undo */
        a = p->action;
        a.type = /* reverse of a.type ??? */
        (a.type == EDA_NEWOBJECT) ? EDA_DELETEOBJECT :
        (a.type == EDA_DELETEOBJECT) ? EDA_NEWOBJECT :
        (a.type == EDA_CHANGESPAWN) ? EDA_RESTORESPAWN :
        (a.type == EDA_RESTORESPAWN) ? EDA_CHANGESPAWN :
        a.type;
        editor_action_commit(a);
    }
    else
        video_showmessage("Already at oldest change.");
}


/* redo */
void editor_action_redo()
{
    editor_action_list_t *p;
    editor_action_t a;

    if(editor_action_buffer_cursor->next != NULL) {
        /* moving the cursor */
        editor_action_buffer_cursor = editor_action_buffer_cursor->next;
        p = editor_action_buffer_cursor;

        /* REDOing a group? */
        if(p->in_group && p->next && p->next->in_group && p->group_key == p->next->group_key)
            editor_action_redo();
        
        /* redo */
        a = p->action;
        editor_action_commit(a);
    }
    else
        video_showmessage("Already at newest change.");
}


/* commit action */
void editor_action_commit(editor_action_t action)
{
    if(action.type == EDA_NEWOBJECT) {
        /* new object */
        switch(action.obj_type) {
            case EDT_BRICK: {
                /* new brick */
                level_create_brick(action.obj_id, action.obj_position);
                break;
            }

            case EDT_ITEM: {
                /* new item */
                level_create_item(action.obj_id, action.obj_position);
                break;
            }

            case EDT_ENEMY: {
                /* new enemy */
                level_create_enemy(editor_enemy_key2name(action.obj_id), action.obj_position);
                break;
            }

            case EDT_GROUP: {
                /* new group of objects */
                editorgrp_entity_list_t *list, *it;
                list = editorgrp_get_group(action.obj_id);
                for(it=list; it; it=it->next) {
                    editor_action_t a;
                    editorgrp_entity_t e = it->entity;
                    enum editor_object_type my_type = EDITORGRP_ENTITY_TO_EDT(e.type);
                    a = editor_action_entity_new(TRUE, my_type, e.id, v2d_add(e.position, action.obj_position));
                    editor_action_commit(a);
                }
                break;
            }
        }
    }
    else if(action.type == EDA_DELETEOBJECT) {
        /* delete object */
        switch(action.obj_type) {
            case EDT_BRICK: {
                /* delete brick */
                brick_list_t *it;
                brickdata_t *ref = brickdata_get(action.obj_id);
                for(it=brick_list; it; it=it->next) {
                    if(it->data->brick_ref == ref) {
                        float dist = v2d_magnitude(v2d_subtract(v2d_new(it->data->x, it->data->y), action.obj_position));
                        if(dist < EPSILON)
                            it->data->state = BRS_DEAD;
                    }
                }
                break;
            }
            case EDT_ITEM: {
                /* delete item */
                item_list_t *it;
                int id = action.obj_id;
                for(it=item_list; it; it=it->next) {
                    if(it->data->type == id) {
                        float dist = v2d_magnitude(v2d_subtract(it->data->actor->position, action.obj_position));
                        if(dist < EPSILON)
                            it->data->state = IS_DEAD;
                    }
                }
                break;
            }
            case EDT_ENEMY: {
                /* delete enemy */
                enemy_list_t *it;
                int id = action.obj_id;
                for(it=enemy_list; it; it=it->next) {
                    if(editor_enemy_name2key(it->data->name) == id) {
                        float dist = v2d_magnitude(v2d_subtract(it->data->actor->position, action.obj_position));
                        if(dist < EPSILON)
                            it->data->state = ES_DEAD;
                    }
                }
                break;
            }
            case EDT_GROUP: {
                /* can't delete a group directly */
                break;
            }
        }
    }
    else if(action.type == EDA_CHANGESPAWN) {
        /* change spawn point */
        level_set_spawn_point(action.obj_position);
        spawn_players();
    }
    else if(action.type == EDA_RESTORESPAWN) {
        /* restore spawn point */
        level_set_spawn_point(action.obj_old_position);
        spawn_players();
    }
}
