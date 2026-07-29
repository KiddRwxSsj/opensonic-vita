/*
 * elliptical_trajectory.h - This decorator makes the object follow an elliptical trajectory
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
 */

#ifndef _OD_ELLIPTICALTRAJECTORY_H
#define _OD_ELLIPTICALTRAJECTORY_H

#include "base/objectdecorator.h"

/*
Please provide:

    amplitude_x     (in pixels)
    amplitude_y     (in pixels)
    angularspeed_x  (in revolutions per second)
    angularspeed_y  (in revolutions per second)
    initialphase_x  (in degrees)
    initialphase_y  (in degrees)
*/
objectmachine_t* objectdecorator_ellipticaltrajectory_new(objectmachine_t *decorated_machine, float amplitude_x, float amplitude_y, float angularspeed_x, float angularspeed_y, float initialphase_x, float initialphase_y);

#endif

