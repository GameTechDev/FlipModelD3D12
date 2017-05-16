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

#include "wsi_utils.hpp"

namespace wsi
{
	// global variables

	extern HWND screen_window;
	extern bool keyboard_state[256];

	// enums

	enum {
		WM_SETMODE = WM_USER + 1,
		WM_SETPREFS,
		WM_DESTROYSELF,
		WM_SHOWCURSOR,
		WM_RELCURSOR,
		WM_SETTITLE,
		WM_TOGGLEFULLSCREEN,
		WM_TOGGLEONTOP,
		WM_TOGGLEGRABMODE,
		WM_TOGGLEVSYNC
	};

	enum WindowStatus {
		Hidden, // Minimized, etc.
		Background, // Another app has focus
		Normal
	};

	enum PowerStatus {
		PluggedIn,
		OnBattery
	};

	// Fullscreen:
	// resolution == desktop: use borderless if allowed.
	// resolution > desktop: must change resolution (stretch only).
	// resolution < desktop: must change resolution, can stretch or center.

	struct ScreenMode
	{
		ScreenMode() {
			ZeroMemory(this, sizeof(*this));
			dm.dmSize = sizeof(dm);
		}

		DEVMODE dm;
		unsigned min_width, min_height; // when resizable

		// flags which control which kinds of window manipulations are permissible.
		bool allow_windowed;
		bool allow_fullscreen; // i.e., exclusive mode. 
		bool allow_borderless; // i.e., use borderless when applicable.
		bool allow_resize;
	};

	// This is the user-state associated with the mode,
	// which you might choose to persist across sessions.
	struct ScreenModePrefs
	{
		ScreenModePrefs() { ZeroMemory(this, sizeof(*this)); }

		bool windowed;
		bool top;
		bool clip_cursor;
		bool vsync;
		WINDOWPLACEMENT placement;
	};

	enum ScreenStateFields
	{
		QuitRaisedBit,
		RedrawRaisedBit,
		PrefsBit,
		DevmodeBit,
		DimensionsBit,
		DpiBit,
		StatusBit,
		PowerBit,
		CursorClippedBit,

		NUMBER_OF_SCREEN_STATE_FIELDS,

		QuitRaised = 1 << QuitRaisedBit,
		RedrawRaised = 1 << RedrawRaisedBit,
		Prefs = 1 << PrefsBit,
		Dimensions = 1 << DimensionsBit,
		Dpi = 1 << DpiBit,
		Devmode = 1 << DevmodeBit,
		Status = 1 << StatusBit,
		Power = 1 << PowerBit,
		CursorClipped = 1 << CursorClippedBit
	};

	struct ScreenState
	{
		unsigned changed_fields;
		ScreenModePrefs prefs;
		DEVMODE dm; // actual, from ENUM_CURRENT_SETTINGS
		SIZE dimensions; // actual, current values for the client area.
		SIZE dpi;
		WindowStatus window_status;
		PowerStatus power_status;
		bool cursor_clipped_now;
		bool cursor_relative_now;
	};

	enum WindowMessageType
	{
		Character,
		Keystroke,
		Mouse
	};

	enum ClickType
	{
		MouseMove,
		MouseDelta,
		MouseDown,
		MouseUp,
		MouseWheel
	};

	enum ModkeyFlags
	{
		modControl=1,
		modAlt=2,
		modShift=4,
		modRepeat=8,
		modLMouse=16,
		modRMouse=32,
		modMMouse=64
	};

	struct WindowMessage
	{
		WindowMessageType type;

		WindowMessage() { }
		WindowMessage(WindowMessageType type)
		{
			memset(this, 0, sizeof(*this));
			this->type = type;
		}

		union
		{
			struct {
				unsigned char code;
				char padding;
				unsigned short modkeys;
			} character;

			struct {
				unsigned char code;
				char padding;
				unsigned short modkeys;
			} keystroke;

			struct {
				unsigned char button;
				char type;
				unsigned short modkeys;
				short x, y;
			} mouse;
		};
	};

	// If a custom WndProc is provided, call wsi::WndProc instead of DefWndProc.
	LRESULT WINAPI WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	
	bool initialize(const TCHAR *title, HICON icon, WNDPROC wndproc, const char *logfile_name = 0);
	void shutdown();

	void log_message(const char *file, int line, const char *format, ...);

	bool should_render();
	void update(const ScreenState *main_thread_state);
	void limit_fps(int max_fps);

	void set_mode(const ScreenMode& mode, const ScreenModePrefs *prefs = 0, bool recreate_window = false); // set mode and prefs
	void set_prefs(const ScreenModePrefs *prefs); // set prefs only

	// The "state queue" contains all information where only the latest update is important.
	bool read_state(ScreenState *state, unsigned remove_mask);

	// The "message queue" contanis all information where sequence is important.
	// This is a regular queue, finite in size, so messages will get lost if it becomes overfull.
	bool read_message(WindowMessage *message);

	bool get_mouse_position(int *x, int *y); // coords relative to the game window; returns false if the cursor is not in the game window
	void set_mouse_position(int x, int y); // coords relative to the game window.

	void set_title(const TCHAR *title);
	inline void show_cursor(bool show) { PostMessage(screen_window, WM_SHOWCURSOR, (WPARAM)show, 0); }
	inline void enable_relative_cursor(bool enable) { PostMessage(screen_window, WM_RELCURSOR, (WPARAM)enable, 0); }

	// convenience functions for toggling preferences
	inline void toggle_fullscreen() { PostMessage(screen_window, WM_TOGGLEFULLSCREEN, 0, 0); }
	inline void toggle_on_top() { PostMessage(screen_window, WM_TOGGLEONTOP, 0, 0); }
	inline void toggle_clip() { PostMessage(screen_window, WM_TOGGLEGRABMODE, 0, 0); }
	inline void toggle_vsync() { PostMessage(screen_window, WM_TOGGLEVSYNC, 0, 0); }
}
