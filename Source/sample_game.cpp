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
#include "sample_game.hpp"
#include <cmath>

static void game_tick(game_data *game, const game_command *action);

void initialize_game(game_data *game, int64_t millisecond_clock_now)
{
	*game = { 0 };

	game->last_unpause_millis = millisecond_clock_now;
	game->time = 0;
	game->cube_rotation_speed = INITIAL_CUBE_ROTATION_SPEED;

	cube_object& cube0 = game->cubes[0];
	//cube0.position.x = -WORLD_ONE * 2;

	cube_object& cube1 = game->cubes[1];
	//cube1.position.x = +WORLD_ONE * 2;

	cube0.position.y = WORLD_ONE * 4;
	cube1.position.y = cube0.position.y;
}

void calc_game_elapsed_time(game_data *game, unsigned *ticks_elapsed, float *fractional_ticks_elapsed, int64_t millisecond_clock_now)
{
	if (game->paused)
	{
		*ticks_elapsed = 0;
		*fractional_ticks_elapsed = game->paused_fractional_ticks;
	}
	else
	{
		int64_t millisecs_since_pause = millisecond_clock_now - game->last_unpause_millis;

		// e.g. game fps = 30, milliseconds elapsed = 1010
		// game time = 1010/30 = 30.3
		// (30, 0.3 fractional time)

		unsigned long long temp = GAME_TICKS_PER_SECOND*millisecs_since_pause;

		game_time_t current_game_time = game->time_at_last_pause + (temp / 1000);
		unsigned fractional_game_time = temp % 1000;

		*ticks_elapsed = unsigned(current_game_time - game->time);
		*fractional_ticks_elapsed = fractional_game_time / 1000.0f;

		game->paused_fractional_ticks = *fractional_ticks_elapsed;
	}
}

void update_game(game_data *game, unsigned ticks_elapsed, const game_command *action, int64_t millisecond_clock_now)
{
	for (unsigned i = 0; i < ticks_elapsed; ++i)
	{
		game_tick(game, action);

		game->time += 1;
	}

	// handle pausing
	if (action->toggle_pause || (game->paused && action->force_unpause))
	{
		if (action->force_unpause)
		{
			game->paused = false;
		}
		else if (action->toggle_pause)
		{
			game->paused = !game->paused;
		}

		if (game->paused)
		{
			game->time_at_last_pause = game->time;
		}
		else
		{
			game->last_unpause_millis = millisecond_clock_now;
		}
	}
}

static void game_tick(game_data *game, const game_command *action)
{
	if (action)
	{
		game->cube_rotation_speed += 100 * (action->increase_rotation - action->decrease_rotation);
		if (abs(game->cube_rotation_speed) < 300) {
			game->cube_rotation_speed = int(0.9*game->cube_rotation_speed);
		}
	}

	for (int cube_index = 0; cube_index < 2; ++cube_index)
	{
		cube_object& cube = game->cubes[cube_index];

#if 0
		if (cube.position.y < 0)
		{
			// Cube is on or below the ground: Reset, and give the cube some upwards velocity.
			cube.position.y = 0;
			//cube.rotation = 0;
			cube.velocity.y = -GRAVITY * 15;
		}
		else
		{
			// Integrate:
			cube.position.x += cube.velocity.x;
			cube.position.y += cube.velocity.y;
			cube.position.z += cube.velocity.z;

			// Apply gravity:
			cube.velocity.y += GRAVITY;

			// Apply rotation:
			cube.rotation = (angle)(cube.rotation + game->cube_rotation_speed);
		}
#else
		cube.rotation = (angle)(cube.rotation + game->cube_rotation_speed);
#endif
	}
}
