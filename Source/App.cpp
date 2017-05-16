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
#include "App.h"

using namespace FlipModelUniversal;

using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;

// The DirectX 12 Application template is documented at http://go.microsoft.com/fwlink/?LinkID=613670&clcid=0x409

ref struct Direct3DApplicationSource sealed : Windows::ApplicationModel::Core::IFrameworkViewSource
{
	virtual Windows::ApplicationModel::Core::IFrameworkView^ CreateView()
	{
		return ref new App();
	}
};

// The main function is only used to initialize our IFrameworkView class.
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^)
{

	auto direct3DApplicationSource = ref new Direct3DApplicationSource();
	CoreApplication::Run(direct3DApplicationSource);
	return 0;
}

// The first method called when the IFrameworkView is being created.
void App::Initialize(CoreApplicationView^ applicationView)
{
	// Register event handlers for app lifecycle. This example includes Activated, so that we
	// can make the CoreWindow active and start rendering on the window.
	applicationView->Activated +=
		ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &App::OnActivated);

	CoreApplication::Suspending +=
		ref new EventHandler<SuspendingEventArgs^>(this, &App::OnSuspending);

	CoreApplication::Resuming +=
		ref new EventHandler<Platform::Object^>(this, &App::OnResuming);
}

// Called when the CoreWindow object is created (or re-created).
void App::SetWindow(CoreWindow^ window)
{
	window->Closed += 
		ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &App::OnWindowClosed);

	window->KeyDown +=
		ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &App::OnKeyDown);

	window->KeyUp +=
		ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &App::OnKeyUp);
}

// Initializes scene resources, or loads a previously saved app state.
void App::Load(Platform::String^ entryPoint)
{
	initialize_game(&m_game, GetTickCount64());

	serialize_swapchain_options(false);
}

// This method is called after the window becomes active.
void App::Run()
{
	while (!m_windowClosed)
	{
		ZeroMemory(&m_action, sizeof(m_action));

		CoreWindow^ window = CoreWindow::GetForCurrentThread();
		window->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

		m_windowDpi = DisplayInformation::GetForCurrentView()->LogicalDpi;
		m_windowWidthDips = window->Bounds.Width;
		m_windowHeightDips = window->Bounds.Height;
		m_windowVisible = window->Visible;

		if (window->GetAsyncKeyState(VirtualKey::Left) == CoreVirtualKeyStates::Down)
		{
			m_action.decrease_rotation = true;
		}

		if (window->GetAsyncKeyState(VirtualKey::Right) == CoreVirtualKeyStates::Down)
		{
			m_action.increase_rotation = true;
		}

		int64_t millisecond_clock_now = GetTickCount64();

		unsigned ticks_elapsed = 0;
		float fractional_ticks = 0;
		calc_game_elapsed_time(&m_game, &ticks_elapsed, &fractional_ticks, millisecond_clock_now);

		update_game(&m_game, ticks_elapsed, &m_action, millisecond_clock_now);
		update_fps();

		if (m_action.toggle_pause)
		{
			pause_eviz_dx12(m_game.paused);
		}

		if (m_windowVisible)
		{
			set_swapchain_options_dx12(NULL, reinterpret_cast<IUnknown*>(window),
				m_windowWidthDips, m_windowHeightDips, m_windowDpi, &m_swapchain_opts);

			WCHAR hud_string[4096];

			{
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
					"     DPI: %.2f, %.2fx%.2f" NEWLINE
					"     Avg. Present Latency = %.2f ms" NEWLINE
					"     Latency StdDev = %.2fms" NEWLINE
					"     Latency MinMaxDev = %.2fms" NEWLINE
					"     Fps = %.2f (%.2fms)" NEWLINE
					"     GPU fps = %.2f (%.2fms)" NEWLINE
					"     CPU fps = %.2f (%.2fms)" NEWLINE,
					m_game.paused,
					m_fullscreen,
					m_vsync,
					m_swapchain_opts.create_time.use_waitable_object,
					m_swapchain_opts.create_time.max_frame_latency,
					m_swapchain_opts.create_time.swapchain_buffer_count,
					m_swapchain_opts.create_time.gpu_frame_count,
					m_swapchain_opts.any_time.overdraw_factor,
					m_swapchain_opts.any_time.cpu_draw_ms,
					m_windowDpi, m_windowWidthDips, m_windowHeightDips,
					m_frame_latency,
					m_frame_latency_stddev, m_frame_latency_minmaxd,
					m_current_fps, 1000 / m_current_fps,
					m_current_fps_gpu, 1000 * m_current_frametime_gpu,
					m_current_fps_cpu, 1000 * m_current_frametime_cpu
					);
			}

			dx12_render_stats stats = {0};
			render_game_dx12(hud_string, &m_game, fractional_ticks, m_vsync, &stats);
			if (stats.latency) {
				const float alpha = 0.1f;
				m_frame_latency = (1 - alpha)*m_frame_latency + alpha*stats.latency;
				m_frame_latency_stddev = stats.stddev_jitter;
				m_frame_latency_minmaxd = stats.minmax_jitter;
				m_current_frametime_cpu = (1 - alpha)*m_current_frametime_cpu + alpha*stats.cpu_frame_time;
				m_current_frametime_gpu = (1 - alpha)*m_current_frametime_gpu + alpha*stats.gpu_frame_time;
			}
		}
		else
		{
			Sleep(1);
		}

		if (m_quit)
		{
			// FIXME: this isn't allowed.. ??
			//window->Close();
			//m_quit = false;
		}
	}
}

// Required for IFrameworkView.
// Terminate events do not cause Uninitialize to be called. It will be called if your IFrameworkView
// class is torn down while the app is in the foreground.
void App::Uninitialize()
{
}

// Application lifecycle event handlers.

void App::OnActivated(CoreApplicationView^ applicationView, IActivatedEventArgs^ args)
{
	// Run() won't start until the CoreWindow is activated.
	CoreWindow::GetForCurrentThread()->Activate();
}

void App::OnSuspending(Platform::Object^ sender, SuspendingEventArgs^ args)
{
	// Save app state asynchronously after requesting a deferral. Holding a deferral
	// indicates that the application is busy performing suspending operations. Be
	// aware that a deferral may not be held indefinitely. After about five seconds,
	// the app will be forced to exit.
	/*SuspendingDeferral^ deferral = args->SuspendingOperation->GetDeferral();
	
	create_task([this, deferral]()
	{
		// TODO: Insert your code here.
		//m_sceneRenderer->SaveState();

		deferral->Complete();
	});*/

	//ApplicationData.Current.LocalSettings

	serialize_swapchain_options(true);

	trim_dx12();
}

void App::OnResuming(Platform::Object^ sender, Platform::Object^ args)
{
	// Restore any data or state that was unloaded on suspend. By default, data
	// and state are persisted when resuming from suspend. Note that this event
	// does not occur if the app was previously terminated.

	// TODO: Replace this with your app's resuming logic.
}

// Window event handlers.

void App::OnWindowClosed(CoreWindow^ sender, CoreWindowEventArgs^ args)
{
	m_windowClosed = true;
}

void App::OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args)
{
	bool controlDown = (sender->GetKeyState(VirtualKey::Control) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down;
	auto vkey = args->VirtualKey;

	auto status = args->KeyStatus;
	if (status.RepeatCount > 1) {
		return;
	}

	if (vkey == VirtualKey::F11) {
		auto applicationView = Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
		if (applicationView->IsFullScreenMode) {
			applicationView->ExitFullScreenMode();
		} else {
			applicationView->TryEnterFullScreenMode();
		}
		m_fullscreen = applicationView->IsFullScreenMode;
	}
	else if (vkey == VirtualKey::K && controlDown) {
		m_vsync = !m_vsync;
	}
	else if (vkey == VirtualKey::W && controlDown) {
		m_swapchain_opts.create_time.use_waitable_object = !m_swapchain_opts.create_time.use_waitable_object;
	}
	if (vkey == VirtualKey::Up) {
		if(controlDown){
			m_swapchain_opts.any_time.cpu_draw_ms = std::min(m_swapchain_opts.any_time.cpu_draw_ms + 1, 33);
		}
		else {
			m_swapchain_opts.any_time.overdraw_factor = std::min<float>(m_swapchain_opts.any_time.overdraw_factor + 0.5f, 20);
		}
	}
	else if (vkey == VirtualKey::Down) {
		if(controlDown){
			m_swapchain_opts.any_time.cpu_draw_ms = std::max(0, m_swapchain_opts.any_time.cpu_draw_ms - 1);
		}
		else {
			m_swapchain_opts.any_time.overdraw_factor = std::max<float>(0, m_swapchain_opts.any_time.overdraw_factor - 0.5f);
		}
	}
	else if ((int)vkey == VK_OEM_MINUS) {
		if(controlDown){
			m_swapchain_opts.create_time.max_frame_latency = std::max(1, m_swapchain_opts.create_time.max_frame_latency - 1);
		}
		else {
			m_swapchain_opts.create_time.swapchain_buffer_count = std::max(2, m_swapchain_opts.create_time.swapchain_buffer_count - 1);
		}
	}
	else if ((int)vkey == VK_OEM_PLUS) {
		if(controlDown){
			m_swapchain_opts.create_time.max_frame_latency = std::min(8, m_swapchain_opts.create_time.max_frame_latency + 1);
		}
		else {
			m_swapchain_opts.create_time.swapchain_buffer_count = std::min(8, m_swapchain_opts.create_time.swapchain_buffer_count + 1);
		}
	}
	else if ((int)vkey == VK_OEM_4 /*[*/) {
		m_swapchain_opts.create_time.gpu_frame_count = std::max(1, m_swapchain_opts.create_time.gpu_frame_count - 1);
	}
	else if ((int)vkey == VK_OEM_6 /*]*/) {
		m_swapchain_opts.create_time.gpu_frame_count = std::min(8, m_swapchain_opts.create_time.gpu_frame_count + 1);
	}
	else if (vkey == VirtualKey::Tab) {
		m_swapchain_opts.inject.cpu_hiccup_count = 1;
		m_swapchain_opts.inject.cpu_hiccup_size = 10;
	}
	else if (vkey == VirtualKey::Escape)
	{
		m_quit = true;
	}
	else if (vkey == VirtualKey::Space)
	{
		m_action.toggle_pause = true;
	}
}

void App::OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args)
{
}

void FlipModelUniversal::App::update_fps()
{
	int64_t now = GetTickCount64();
	int64_t then = m_fps_frame_timestamps[m_fps_frame_timestamp_write_index];
	m_fps_frame_timestamps[m_fps_frame_timestamp_write_index] = now;
	m_fps_frame_timestamp_write_index = (m_fps_frame_timestamp_write_index + 1) & (FRAMETIME_BUFFER_LENGTH - 1);

	if (now > then)
	{
		if (FPS_UPDATES_PER_SECOND*(now - m_fps_last_update) > 1000)
		{
			float fps = (1000.0f*FRAMETIME_BUFFER_LENGTH) / (now - then);
			m_fps_last_update = now;
			m_current_fps = fps;
			m_current_fps_cpu = 1.0f / m_current_frametime_cpu;
			m_current_fps_gpu = 1.0f / m_current_frametime_gpu;
		}
	}
	else
	{
		m_current_fps = 0;
	}
}

static void serialize(
	Windows::Foundation::Collections::IPropertySet ^props,
	bool write, Platform::String^ key,
	int& value, int default_value)
{
	if (write) {
		props->Insert(key, value);
	}
	else {
		value = default_value;
		auto entry = props->Lookup(key);
		if (entry) {
			auto propVal = dynamic_cast<IPropertyValue^>(entry);
			if (propVal) {
				value = propVal->GetInt32();
			}
		}
	}
};

static void serialize(
	Windows::Foundation::Collections::IPropertySet ^props,
	bool write, Platform::String^ key,
	float& value, float default_value)
{
	if (write) {
		props->Insert(key, value);
	}
	else {
		value = default_value;
		auto entry = props->Lookup(key);
		if (entry) {
			auto propVal = dynamic_cast<IPropertyValue^>(entry);
			if (propVal) {
				value = (float)propVal->GetDouble();
			}
		}
	}
};

void App::serialize_swapchain_options(bool write)
{
	auto settings = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
	auto opts = &m_swapchain_opts;

	serialize(settings, write, "overdraw_factor", opts->any_time.overdraw_factor, 8.0f);
	serialize(settings, write, "cpu_draw_ms", opts->any_time.cpu_draw_ms, 8);
	serialize(settings, write, "use_waitable_object", opts->create_time.use_waitable_object, 1);
	serialize(settings, write, "max_frame_latency", opts->create_time.max_frame_latency, 2);
	serialize(settings, write, "swapchain_buffer_count", opts->create_time.swapchain_buffer_count, 3);
	serialize(settings, write, "gpu_frame_count", opts->create_time.gpu_frame_count, 2);
}
