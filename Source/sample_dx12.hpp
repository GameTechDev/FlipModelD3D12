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
