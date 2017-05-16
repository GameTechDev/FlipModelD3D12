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
#include "wsi.hpp"
#include "sample_dx12.hpp"
#include "sample_game.hpp"

static wsi::ScreenState screen;
static wsi::ScreenMode mode;
static unsigned max_fps;
static float current_fps, current_fps_cpu, current_fps_gpu;
static float current_frametime_cpu, current_frametime_gpu;
static bool caption_changed;
static bool running;
static float paused_fractional_ticks;
static float frame_latency;
static float frame_latency_stddev, frame_latency_minmaxd;

static dx12_swapchain_options swapchain_opts;

static WCHAR hud_string[4096];

static void rebuild_hud_string(game_data *game)
{
	if (!caption_changed)
	{
		return;
	}

	caption_changed = false;

#define NEWLINE "\n"
	swprintf_s(hud_string, L""
		"Controls:" NEWLINE
		"[%d] Pause: Space" NEWLINE
		"[%d] Fullscreen: F11" NEWLINE
		"[%d] Vsync: Ctrl+K" NEWLINE
		"[%d] Use Waitable Object: Ctrl+W" NEWLINE
		"[%d] MaximumFrameLatency: Ctrl+,Ctrl-" NEWLINE
		"[%d] BufferCount: +,-" NEWLINE
		"[%d] FrameCount: [,]" NEWLINE
		"[%.1f] GPU Workload up,down" NEWLINE
		"[%d] CPU Workload Ctrl+up, Ctrl+down" NEWLINE
		"Stats:" NEWLINE
		"     Avg. Present Latency = %.2f ms" NEWLINE
		"     Latency StdDev = %.2fms" NEWLINE
		"     Latency MinMaxDev = %.2fms" NEWLINE
		"     Fps = %.2f (%.2fms)" NEWLINE
		"     GPU fps = %.2f (%.2fms)" NEWLINE
		"     CPU fps = %.2f (%.2fms)" NEWLINE,
		game->paused,
		screen.prefs.windowed==0,
		screen.prefs.vsync,
		swapchain_opts.create_time.use_waitable_object,
		swapchain_opts.create_time.max_frame_latency,
		swapchain_opts.create_time.swapchain_buffer_count,
		swapchain_opts.create_time.gpu_frame_count,
		swapchain_opts.any_time.overdraw_factor,
		swapchain_opts.any_time.cpu_draw_ms,
		frame_latency,
		frame_latency_stddev, frame_latency_minmaxd,
		current_fps, 1000 / current_fps,
		current_fps_gpu, 1000*current_frametime_gpu,
		current_fps_cpu, 1000*current_frametime_cpu
		);
}

static void update_fps()
{
	enum {
		FRAMETIME_BUFFER_LENGTH = 32,
		FPS_UPDATES_PER_SECOND = 4
	};
	static wsi::millisec64_t frame_timestamps[FRAMETIME_BUFFER_LENGTH];
	static wsi::millisec64_t frame_timestamp_write_index;
	static wsi::millisec64_t last_update;

	wsi::millisec64_t now = wsi::milliseconds();
	wsi::millisec64_t then = frame_timestamps[frame_timestamp_write_index];
	frame_timestamps[frame_timestamp_write_index] = now;
	frame_timestamp_write_index = (frame_timestamp_write_index + 1) & (FRAMETIME_BUFFER_LENGTH - 1);

	if (now > then)
	{
		if (FPS_UPDATES_PER_SECOND*(now - last_update) > 1000)
		{
			float fps = (1000.0f*FRAMETIME_BUFFER_LENGTH) / (now - then);
			last_update = now;
			caption_changed = true;
			current_fps = fps;
			current_fps_cpu = 1.0f / current_frametime_cpu;
			current_fps_gpu = 1.0f / current_frametime_gpu;
		}
	}
	else
	{
		current_fps = 0;
	}
}

void process_inputs(game_command *out_action)
{
	// Process windows thread messages - this may or may not be needed for your game,
	// depending on what you run in the main thread.
	{
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			DispatchMessage(&msg);
		}
	}

	// Process queued state updates from the UI thread
	if (wsi::read_state(&screen, (unsigned)-1))
	{
		if (screen.changed_fields & wsi::QuitRaised)
		{
			running = false;
		}

		unsigned ideal_graphics_fps = screen.dm.dmDisplayFrequency;

		// Compute the desired framerate based on window and power state.
		if (screen.window_status == wsi::Hidden)
		{
			ideal_graphics_fps = 0;
		}
		else
		{
			if (screen.window_status == wsi::Background)
			{
				
			}
			else if (screen.window_status == wsi::Normal)
			{
				ideal_graphics_fps *= 2;
			}

			if (screen.power_status == wsi::OnBattery)
			{
				ideal_graphics_fps /= 2;
			}
		}

		max_fps = std::max((unsigned)GAME_TICKS_PER_SECOND, ideal_graphics_fps);

		wsi::enable_timer_boost(screen.power_status == wsi::PluggedIn && screen.window_status == wsi::Normal);

		caption_changed = true;
	}

	// Process queued input events from the UI thread
	wsi::WindowMessage message;
	auto old_opts = swapchain_opts;
	while (wsi::read_message(&message))
	{
		switch (message.type)
		{
		case wsi::Character:
			// process text input
			break;
		case wsi::Keystroke:
		{
			if (message.keystroke.modkeys & wsi::modRepeat) {
				break;
			}
			if (message.keystroke.code == 'W' && (message.keystroke.modkeys & wsi::modControl)) {
				swapchain_opts.create_time.use_waitable_object = !swapchain_opts.create_time.use_waitable_object;
			}
			if (message.keystroke.code == VK_F11) {
				wsi::toggle_fullscreen();
			}
			else if (message.keystroke.code == VK_UP) {
				if (message.keystroke.modkeys & wsi::modControl) {
					swapchain_opts.any_time.cpu_draw_ms = std::min(swapchain_opts.any_time.cpu_draw_ms + 1, 33);
				} else {
					swapchain_opts.any_time.overdraw_factor = std::min<float>(swapchain_opts.any_time.overdraw_factor + 0.5f, 20);
				}
			}
			else if (message.keystroke.code == VK_DOWN) {
				if (message.keystroke.modkeys & wsi::modControl) {
					swapchain_opts.any_time.cpu_draw_ms = std::max(0, swapchain_opts.any_time.cpu_draw_ms - 1);
				} else {
					swapchain_opts.any_time.overdraw_factor = std::max<float>(0, swapchain_opts.any_time.overdraw_factor - 0.5f);
				}
			}
			else if (message.keystroke.code == VK_OEM_MINUS) {
				if (message.keystroke.modkeys & wsi::modControl) {
					swapchain_opts.create_time.max_frame_latency = std::max(1, swapchain_opts.create_time.max_frame_latency - 1);
				}
				else {
					swapchain_opts.create_time.swapchain_buffer_count = std::max(2, swapchain_opts.create_time.swapchain_buffer_count - 1);
				}
			}
			else if (message.keystroke.code == VK_OEM_PLUS) {
				if (message.keystroke.modkeys & wsi::modControl) {
					swapchain_opts.create_time.max_frame_latency = std::min(8, swapchain_opts.create_time.max_frame_latency + 1);
				}
				else {
					swapchain_opts.create_time.swapchain_buffer_count = std::min(8, swapchain_opts.create_time.swapchain_buffer_count + 1);
				}
			}
			else if (message.keystroke.code == VK_OEM_4 /*[*/) {
				swapchain_opts.create_time.gpu_frame_count = std::max(1, swapchain_opts.create_time.gpu_frame_count - 1);
			}
			else if (message.keystroke.code == VK_OEM_6 /*]*/) {
				swapchain_opts.create_time.gpu_frame_count = std::min(8, swapchain_opts.create_time.gpu_frame_count + 1);
			}
			else if (message.keystroke.code == VK_TAB) {
				swapchain_opts.inject.cpu_hiccup_count = 1;
				swapchain_opts.inject.cpu_hiccup_size = 10;
			}
			else if (message.keystroke.code == VK_ESCAPE)
			{
				running = false;
			}
			else if (message.keystroke.code == VK_SPACE)
			{
				out_action->toggle_pause = true;
			}
		}
		break;
		case wsi::Mouse:
			// process mouse input
			break;
		}
	}

	if (memcmp(&swapchain_opts, &old_opts, sizeof(old_opts)))
	{
		out_action->force_unpause = true;
		out_action->toggle_pause = false;
	}

	// Process continuous/time-based (not event based) input:
	if (screen.window_status == wsi::Normal)
	{
		out_action->decrease_rotation = wsi::keyboard_state[VK_LEFT];
		out_action->increase_rotation = wsi::keyboard_state[VK_RIGHT];
	}
	
	// Process queued input from the network

	// etc.
}

static void run_game()
{
	game_data game;

	initialize_game(&game, wsi::milliseconds());

	running = true;

	for (;;)
	{
		wsi::update(&screen);

		bool rendering = wsi::should_render();

		int64_t millisecond_clock_now = wsi::milliseconds();

		unsigned ticks_elapsed = 0;
		float fractional_ticks = 0;
		calc_game_elapsed_time(&game, &ticks_elapsed, &fractional_ticks, millisecond_clock_now);

		game_command action = { 0 };
		process_inputs(&action);

		if (!running) break;

		update_game(&game, ticks_elapsed, &action, millisecond_clock_now);
		update_fps();

		if (action.toggle_pause)
		{
			pause_eviz_dx12(game.paused);
		}

		if (rendering)
		{
			float dpi = (float)screen.dpi.cx;
			float x_dips = screen.dimensions.cx * 96.0f / dpi;
			float y_dips = screen.dimensions.cy * 96.0f / dpi;
			set_swapchain_options_dx12(&wsi::screen_window, NULL, x_dips, y_dips, dpi, &swapchain_opts);
			rebuild_hud_string(&game);
			dx12_render_stats stats;
			render_game_dx12(hud_string, &game, fractional_ticks, screen.prefs.vsync, &stats);
			if (stats.latency) {
				const float alpha = 0.1f;
				frame_latency = (1-alpha)*frame_latency + alpha*stats.latency;
				frame_latency_stddev = stats.stddev_jitter;
				frame_latency_minmaxd = stats.minmax_jitter;
				current_frametime_cpu = (1 - alpha)*current_frametime_cpu + alpha*stats.cpu_frame_time;
				current_frametime_gpu = (1 - alpha)*current_frametime_gpu + alpha*stats.gpu_frame_time;
			}
		}

		//wsi::limit_fps(max_fps);
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow)
{
#ifdef _DEBUG
	struct leakcheck {
		static void dump() {
			_CrtDumpMemoryLeaks();
		}
	};
	atexit(leakcheck::dump);
#endif

	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpszCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	wsi::initialize(TEXT("FlipModelD3D12"), NULL, NULL, "log.txt");

	wsi::set_thread_name(GetCurrentThreadId(), "Main Thread");

	// Set the screen mode (and prefs which is optional)
	{
		wsi::ScreenModePrefs prefs;

		mode.dm.dmPelsWidth = 1024;
		mode.dm.dmPelsHeight = 768;
		mode.dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

		mode.min_width = 1024;
		mode.min_height = 768;

		mode.allow_windowed = true;
		mode.allow_fullscreen = true;
		mode.allow_borderless = true;
		mode.allow_resize = true;

		prefs.windowed = true;
		prefs.top = false;
		prefs.clip_cursor = false;
		prefs.vsync = true;

		wsi::set_mode(mode, &prefs);
	}

	swapchain_opts.any_time.overdraw_factor = 8;
	swapchain_opts.any_time.cpu_draw_ms = 8;
	swapchain_opts.create_time.gpu_frame_count = 2;
	swapchain_opts.create_time.swapchain_buffer_count = 3;
	swapchain_opts.create_time.use_waitable_object = 1;
	swapchain_opts.create_time.max_frame_latency = 2;

	if (initialize_dx12(&swapchain_opts))
	{
		run_game();
	}
	else
	{
		MessageBox(wsi::screen_window, "Could not initialize D3D12; the sample cannot run.", "Error", MB_ICONEXCLAMATION);
		wsi::log_message(0, 0, "Could not initialize D3D12; the sample cannot run.");
	}

	shutdown_dx12();
	wsi::shutdown();
	
	return 0;
}
