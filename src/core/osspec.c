/*
 * osspec.c - OS Specific Routines
 * Copyright (C) 2009-2010  Alexandre Martins <alemartf(at)gmail(dot)com>
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
 * Upstream's absolute_filepath()/home_filepath() exist to cope with two
 * desktop problems that don't exist here:
 *
 *   1) "Where is the game installed?" -- resolved by inspecting argv[0]
 *      (get_executable_name()) against GAME_UNIX_INSTALLDIR/COPYDIR.
 *      On Vita, bundled assets are always at the fixed "app0:" mount
 *      handed to the process by the OS -- there's no search involved.
 *
 *   2) "Is the install directory writable?" -- probed with a test
 *      fopen()+delete_file(). app0: is a read-only archive on this
 *      platform, always, so that probe can never succeed; writes
 *      always go to the per-title save-data partition, "ux0:data/".
 *
 * fix_case_path() (case-insensitive filename fallback, for porting
 * assets authored on a case-sensitive Linux tree onto other Allegro
 * targets) is dropped: we package images/, levels/, musics/, themes/,
 * objects/ and config/ into the VPK ourselves from this same upstream
 * tree, so the paths the game code asks for already match byte-for-byte
 * what's on disk -- the mismatch that routine guarded against cannot
 * occur here.
 *
 * create_process() has no callers anywhere in the upstream source (verified
 * by inspection) -- it's dead code kept only for header/API compatibility.
 *
 * The resource path cache (a small binary tree keyed by relative path)
 * has no Allegro dependency and is kept verbatim: repeated stat() calls
 * against app0:/ux0: are not free, so this still earns its keep here.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include "global.h"
#include "osspec.h"
#include "util.h"
#include "stringutil.h"
#include "logfile.h"

/* bundled, read-only game data (packaged into the VPK under data/) */
#define GAME_DATA_PATH    "app0:data/"

/* per-title, writable save-data partition */
#define GAME_HOME_PATH    "ux0:data/OSNC00001/"

/* private stuff */
static void search_the_file(char *dest, const char *relativefp, size_t dest_size);

/* cache: a basic dictionary, implemented as a binary tree */
static void cache_init();
static void cache_release();
static char *cache_search(const char *key);
static void cache_insert(const char *key, char *value);

typedef struct cache_t {
    char *key, *value;
    struct cache_t *left, *right;
} cache_t;
static cache_t *cache_root;
static cache_t *cachetree_release(cache_t *node);
static cache_t *cachetree_search(cache_t *node, const char *key);
static cache_t *cachetree_insert(cache_t *node, const char *key, const char *value);


/* public functions */

/*
 * osspec_init()
 * Operating System Specifics - initialization
 */
void osspec_init()
{
    static const char *subdirs[] = {
        "", "levels", "screenshots", "mods", "themes", "quests"
    };
    char tmp[1024];
    size_t i;

    for(i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); i++) {
        home_filepath(tmp, subdirs[i], sizeof(tmp));
        mkdir(tmp, 0777);
    }

    cache_init();
}


/*
 * osspec_release()
 * Operating System Specifics - release
 */
void osspec_release()
{
    cache_release();
}


/*
 * filepath_exists()
 * Returns TRUE if the given file exists
 * or FALSE otherwise
 */
int filepath_exists(const char *filepath)
{
    struct stat st;
    return (stat(filepath, &st) == 0) && !S_ISDIR(st.st_mode);
}


/*
 * directory_exists()
 * Returns TRUE if the given directory exists
 * or FALSE otherwise
 */
int directory_exists(const char *dirpath)
{
    struct stat st;
    return (stat(dirpath, &st) == 0) && S_ISDIR(st.st_mode);
}


/*
 * absolute_filepath()
 * Resolves relativefp against the bundled, read-only data mount.
 */
void absolute_filepath(char *dest, const char *relativefp, size_t dest_size)
{
    snprintf(dest, dest_size, GAME_DATA_PATH "%s", relativefp);
}


/*
 * home_filepath()
 * Resolves relativefp against the writable per-title save-data mount.
 */
void home_filepath(char *dest, const char *relativefp, size_t dest_size)
{
    snprintf(dest, dest_size, GAME_HOME_PATH "%s", relativefp);
}


/*
 * scan_directory()
 * Vita replacement for Allegro's for_each_file_ex(). Allegro's version
 * took a glob pattern (e.g. "sprites/*.spr") and a deny-flags mask
 * (FA_DIREC | FA_LABEL, to skip subdirectories and volume labels) and
 * invoked the callback with each matching file's full path.
 * sceIoDopen()/sceIoDread() only list a directory's entries, with no
 * glob support, so dirpath here is the plain directory (no wildcard)
 * and matching by extension is done by hand; subdirectories are skipped
 * via SCE_S_ISDIR(), this platform's equivalent of Allegro's FA_DIREC
 * deny flag (there are no volume labels on this filesystem, so FA_LABEL
 * has no equivalent to port).
 */
void scan_directory(const char *dirpath, const char *extension, int (*callback)(const char*, int, void*), void *param)
{
    SceUID dfd;
    SceIoDirent entry;
    char full_path[1024];
    size_t extlen = strlen(extension);

    dfd = sceIoDopen(dirpath);
    if(dfd < 0) {
        logfile_message("scan_directory(): can't open directory \"%s\" (0x%08X)", dirpath, dfd);
        return;
    }

    while(sceIoDread(dfd, &entry) > 0) {
        size_t namelen;

        if(SCE_S_ISDIR(entry.d_stat.st_mode))
            continue; /* skip subdirectories, same as Allegro's FA_DIREC deny flag */

        namelen = strlen(entry.d_name);
        if(namelen <= extlen || str_icmp(entry.d_name + (namelen - extlen), extension) != 0)
            continue; /* not a match for *extension */

        snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry.d_name);
        callback(full_path, 0, param);
    }

    sceIoDclose(dfd);
}


/*
 * resource_filepath()
 * Similar to absolute_filepath() and home_filepath(), but this routine
 * searches the specified file both in the home directory and in the
 * game directory
 */
void resource_filepath(char *dest, const char *relativefp, size_t dest_size, int resfp_mode)
{
    /* scan_directory() hands its callbacks (dirfill()/dircount() in
     * stageselect.c, langselect.c, ...) a path that is already fully
     * resolved (e.g. "app0:data/levels/blue_ocean_2.lev"), since it's
     * built by concatenating the dirpath it was given (itself already
     * absolute) with the directory entry name. If that path is then
     * fed back into resource_filepath() -- as stagedata_load() and
     * lang_readcompatibility()/lang_readstring() do -- treating it as
     * a relative path re-prepends GAME_DATA_PATH/GAME_HOME_PATH and
     * produces a bogus double-prefixed path such as
     * "app0:data/app0:data/levels/blue_ocean_2.lev", which nanoparser
     * then fails to open. Detect this case and pass the path through
     * unchanged instead of re-resolving it. */
    if(strncmp(relativefp, "app0:", 5) == 0 || strncmp(relativefp, "ux0:", 4) == 0) {
        str_cpy(dest, relativefp, dest_size);
        return;
    }

    switch(resfp_mode) {
        /* I'll read the file */
        case RESFP_READ: {
            char *path;
            if(NULL == (path = cache_search(relativefp))) {
                search_the_file(dest, relativefp, dest_size);

                path = mallocx((strlen(dest) + 1) * sizeof *path);
                strcpy(path, dest);
                cache_insert(relativefp, path);
            }
            else
                str_cpy(dest, path, dest_size);

            break;
        }

        /* I'll write to the file: always goes to the writable partition */
        case RESFP_WRITE: {
            home_filepath(dest, relativefp, dest_size);
            break;
        }

        /* Unknown mode */
        default: {
            fprintf(stderr, "resource_filepath(): invalid resfp_mode (%d)", resfp_mode);
            break;
        }
    }
}


/*
 * create_process()
 * No callers in the upstream source; Vita apps also cannot fork/exec
 * arbitrary executables the way a desktop OS can. Kept for API
 * compatibility only.
 */
void create_process(const char *path, int argc, char *argv[])
{
    (void)path;
    (void)argc;
    (void)argv;
}


/*
 * basename()
 * Finds out the filename portion of a completely specified file path
 */
char *basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *colon = strrchr(path, ':');
    const char *last = slash;

    if(colon && (!last || colon > last))
        last = colon;

    return (char *)(last ? last + 1 : path);
}


/* private methods */

/* auxiliary routine for resource_filepath(): given a relative path,
 * prefers a user-provided/save-data copy (mods, custom levels, saved
 * settings) over the bundled one, falling back to the bundled data */
void search_the_file(char *dest, const char *relativefp, size_t dest_size)
{
    home_filepath(dest, relativefp, dest_size);
    if(!filepath_exists(dest) && !directory_exists(dest))
        absolute_filepath(dest, relativefp, dest_size);
}


/* ------- cache interface -------- */

void cache_init()
{
    cache_root = NULL;
}

void cache_release()
{
    cache_root = cachetree_release(cache_root);
}

char *cache_search(const char *key)
{
    cache_t *node = cachetree_search(cache_root, key);
    return node ? node->value : NULL;
}

void cache_insert(const char *key, char *value)
{
    cache_root = cachetree_insert(cache_root, key, value);
}


/* ------ cache implementation --------- */
cache_t *cachetree_release(cache_t *node)
{
    if(node) {
        node->left = cachetree_release(node->left);
        node->right = cachetree_release(node->right);
        free(node->key);
        free(node->value);
        free(node);
    }

    return NULL;
}

cache_t *cachetree_search(cache_t *node, const char *key)
{
    int cmp;

    if(node) {
        cmp = strcmp(key, node->key);

        if(cmp < 0)
            return cachetree_search(node->left, key);
        else if(cmp > 0)
            return cachetree_search(node->right, key);
        else
            return node;
    }
    else
        return NULL;
}

cache_t *cachetree_insert(cache_t *node, const char *key, const char *value)
{
    int cmp;
    cache_t *t;

    if(node) {
        cmp = strcmp(key, node->key);

        if(cmp < 0)
            return (node->left = cachetree_insert(node->left, key, value));
        else if(cmp > 0)
            return (node->right = cachetree_insert(node->right, key, value));
        else
            return node;
    }
    else {
        t = mallocx(sizeof *t);
        t->key = mallocx(sizeof *(t->key) * (strlen(key) + 1));
        t->value = mallocx(sizeof *(t->value) * (strlen(value) + 1));
        t->left = t->right = NULL;
        strcpy(t->key, key);
        strcpy(t->value, value);
        return t;
    }
}
