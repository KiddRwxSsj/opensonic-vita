/*
 * main.c - entry point
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
 * END_OF_MAIN() is Allegro/DOS-era plumbing for platforms that need a
 * special export wrapper around main(); VitaSDK's own crt0 needs no
 * such thing, so it's dropped along with the include.
 *
 * sceUserMainThreadStackSize/sceLibcHeapSize are read by VitaSDK's crt0
 * before main() runs: the default 256KB stack is too small once
 * nanoparser recursion and the entity/scene call chains are in play,
 * and the default heap is too small for this game's PNG-decoded sprite
 * sheets and level data all resident at once.
 */

#include "core/engine.h"

unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
unsigned int sceLibcHeapSize = 128 * 1024 * 1024;

/*
 * main()
 * Entry point
 */
int main(int argc, char *argv[])
{
    engine_init(argc, argv);
    engine_mainloop();
    engine_release();

    return 0;
}
