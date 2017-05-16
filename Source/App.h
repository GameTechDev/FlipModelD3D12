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

#define NOMINMAX
#include <wrl.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <algorithm>

#include "sample_game.hpp"
#include "sample_dx12.hpp"

namespace FlipModelUniversal
{
	// Main entry point for our app. Connects the app with the Windows shell and handles application lifecycle events.
	ref class App sealed : public Windows::ApplicationModel::Core::IFrameworkView
	{
	public:

		// IFrameworkView methods.
		virtual void Initialize(Windows::ApplicationModel::Core::CoreApplicationView^ applicationView);
		virtual void SetWindow(Windows::UI::Core::CoreWindow^ window);
		virtual void Load(Platform::String^ entryPoint);
		virtual void Run();
		virtual void Uninitialize();

	protected:
		// Application lifecycle event handlers.
		void OnActivated(Windows::ApplicationModel::Core::CoreApplicationView^ applicationView, Windows::ApplicationModel::Activation::IActivatedEventArgs^ args);
		void OnSuspending(Platform::Object^ sender, Windows::ApplicationModel::SuspendingEventArgs^ args);
		void OnResuming(Platform::Object^ sender, Platform::Object^ args);

		// Window event handlers.
		void OnWindowClosed(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::CoreWindowEventArgs^ args);

		// Input event handlers.
		void OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
		void OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);

	private:

		void update_fps();
		void serialize_swapchain_options(bool write);

		bool m_initialized = 0;
		bool m_quit = 0;
		bool m_fullscreen = 0;

		game_data m_game = { 0 };
		game_command m_action = { 0 };
		dx12_swapchain_options m_swapchain_opts = { 0 };

		enum {
			FRAMETIME_BUFFER_LENGTH = 32,
			FPS_UPDATES_PER_SECOND = 4
		};

		int64_t m_fps_frame_timestamps[FRAMETIME_BUFFER_LENGTH] = { 0 };
		int64_t m_fps_frame_timestamp_write_index = 0;
		int64_t m_fps_last_update = 0;

		float m_current_fps = 0, m_current_fps_cpu = 0, m_current_fps_gpu = 0;
		float m_current_frametime_cpu = 0, m_current_frametime_gpu = 0;
		float m_frame_latency = 0;
		float m_frame_latency_stddev = 0, m_frame_latency_minmaxd = 0;

		bool m_vsync = 1;
		
		float m_windowWidthDips = 0;
		float m_windowHeightDips = 0;

		float m_windowDpi = 0;

		bool m_windowClosed = 0;
		bool m_windowVisible = 1;
	};
}
