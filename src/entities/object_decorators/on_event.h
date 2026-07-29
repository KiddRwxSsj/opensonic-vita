/*
 * on_event.h - Events: if an event is true, then the state is changed
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

#ifndef _OD_ONEVENT_H
#define _OD_ONEVENT_H

#include "base/objectdecorator.h"

/* general events */
objectmachine_t* objectdecorator_ontimeout_new(objectmachine_t *decorated_machine, float timeout, const char *new_state_name);
objectmachine_t* objectdecorator_oncollision_new(objectmachine_t *decorated_machine, const char *target_name, const char *new_state_name);
objectmachine_t* objectdecorator_onanimationfinished_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onrandomevent_new(objectmachine_t *decorated_machine, float probability, const char *new_state_name);

/* player events */
objectmachine_t* objectdecorator_onplayercollision_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onplayerattack_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onplayerrectcollision_new(objectmachine_t *decorated_machine, int x1, int y1, int x2, int y2, const char *new_state_name);
objectmachine_t* objectdecorator_onnoshield_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onshield_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onfireshield_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onthundershield_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onwatershield_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onacidshield_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onwindshield_new(objectmachine_t *decorated_machine, const char *new_state_name);

/* brick events */
objectmachine_t* objectdecorator_onbrickcollision_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onfloorcollision_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onceilingcollision_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onleftwallcollision_new(objectmachine_t *decorated_machine, const char *new_state_name);
objectmachine_t* objectdecorator_onrightwallcollision_new(objectmachine_t *decorated_machine, const char *new_state_name);

#endif

