/*
 * global.h - global definitions
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

#ifndef _GLOBAL_H
#define _GLOBAL_H

/* uncomment if this is the stable release
 * of the game */
#define GAME_STABLE_RELEASE

/* Game data */
#define GAME_UNIXNAME           "opensonic"
#define GAME_TITLE              "Open Sonic"
#define GAME_VERSION            0
#define GAME_SUB_VERSION        1
#define GAME_WIP_VERSION        4
#define GAME_WEBSITE            "http://opensnc.sourceforge.net"
#define GAME_UNIX_INSTALLDIR    "/usr/share/opensonic"
#define GAME_UNIX_COPYDIR       "/usr/bin"

/* Global definitions and constants */
#ifdef INFINITY
#undef INFINITY
#endif

#ifdef INFINITY_FLT
#undef INFINITY_FLT
#endif

#ifdef TRUE
#undef TRUE
#endif

#ifdef FALSE
#undef FALSE
#endif

#ifdef PI
#undef PI
#endif

#ifdef EPSILON
#undef EPSILON
#endif

#define TRUE                    -1
#define FALSE                   0
#define EPSILON                 1e-5
#define PI                      3.14159265
#define INFINITY                (1<<30)
#define INFINITY_FLT            (1.0/0.0) /*1e+34*/

/* Exact-width integer types */
#ifndef __MSVC__

#include <stdint.h>
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

#else

typedef __int8 int8;
typedef __int16 int16;
typedef __int32 int32;
typedef __int64 int64;
typedef unsigned __int8 uint8;
typedef unsigned __int16 uint16;
typedef unsigned __int32 uint32;
typedef unsigned __int64 uint64;

#endif

#endif
