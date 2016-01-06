//--------------------------------------------------------------------------------------
// Copyright 2015 Intel Corporation
// All Rights Reserved
//
// Permission is granted to use, copy, distribute and prepare derivative works of this
// software for any purpose and without fee, provided, that the above copyright notice
// and this statement appear in all copies.  Intel makes no representations about the
// suitability of this software for any purpose.  THIS SOFTWARE IS PROVIDED "AS IS."
// INTEL SPECIFICALLY DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, AND ALL LIABILITY,
// INCLUDING CONSEQUENTIAL AND OTHER INDIRECT DAMAGES, FOR THE USE OF THIS SOFTWARE,
// INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PROPRIETARY RIGHTS, AND INCLUDING THE
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  Intel does not
// assume any responsibility for any errors which may appear in this software nor any
// responsibility to update it.
//--------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

enum
{
	GAME_TICKS_PER_SECOND = 24,
	WORLD_ONE = 512,
	GRAVITY = -int(WORLD_ONE / GAME_TICKS_PER_SECOND), // world-unit delta-v per tick
	FULL_CIRCLE = 0x10000,

	INITIAL_CUBE_ROTATION_SPEED = FULL_CIRCLE / (4 * GAME_TICKS_PER_SECOND)
};

typedef unsigned long long game_time_t;
typedef int coordinate;
typedef unsigned short angle;

struct world_vector
{
	coordinate x, y, z;
};

struct cube_object
{
	world_vector position, velocity;
	angle rotation;
	bool initialized;
};

struct game_command
{
	bool decrease_rotation;
	bool increase_rotation;

	bool toggle_pause;
	bool force_unpause;
};

struct game_data
{
	bool paused;
	float paused_fractional_ticks;

	int64_t startup_millis, last_unpause_millis;
	game_time_t time, time_at_last_pause;

	// The game world consists of two cubes.
	int cube_rotation_speed;
	cube_object cubes[2];
};

void initialize_game(game_data *game, int64_t millisecond_clock_now);
void calc_game_elapsed_time(game_data *game, unsigned *ticks_elapsed, float *fractional_ticks_elapsed, int64_t millisecond_clock_now);
void update_game(game_data *game, unsigned ticks_elapsed, const game_command *input, int64_t millisecond_clock_now);
void dispose_game(game_data *game);
