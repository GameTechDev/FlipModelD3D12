////////////////////////////////////////////////////////////////////////////////
// Copyright 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License.  You may obtain a copy
// of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
// License for the specific language governing permissions and limitations
// under the License.
////////////////////////////////////////////////////////////////////////////////
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
