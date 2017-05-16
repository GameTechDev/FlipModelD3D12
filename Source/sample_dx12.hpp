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

#include <cassert>

struct game_data;
struct text_rectangle;

struct dx12_swapchain_options
{
	// these can be altered freely
	struct {
		float overdraw_factor;
		int cpu_draw_ms;
	} any_time;

	// changing these will cause the device to be recreated
	struct {
		int use_waitable_object;
		int max_frame_latency;
		int swapchain_buffer_count;
		int gpu_frame_count;
	} create_time;

	struct {
		int cpu_hiccup_size;
		int cpu_hiccup_count;
	} inject;
};

struct dx12_render_stats
{
	float cpu_frame_time;
	float gpu_frame_time;
	float latency;
	float minmax_jitter;
	float stddev_jitter;
};

bool initialize_dx12(dx12_swapchain_options *opts);
void trim_dx12();
void shutdown_dx12();

bool set_swapchain_options_dx12(void *pHWND, void *pCoreWindow,float x_dips, float y_dips, float dpi, dx12_swapchain_options *opts);

void render_game_dx12(wchar_t *hud_text, game_data *game, float fractional_ticks, int vsync_interval, dx12_render_stats *stats);

void pause_eviz_dx12(bool pause);
