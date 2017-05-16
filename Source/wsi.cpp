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
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "wsi.hpp"

#include <ctime>
#include <memory>
#include <vector>
#include <string>

#define MAIN_WINDOW_CLASS TEXT("WSIWCLASS")

using namespace wsi;

HWND wsi::screen_window;
bool wsi::keyboard_state[256];

enum { MESSAGE_Q_MAX_LENGTH = 1024 };
static wsi::queue message_Q;
static bool window_class_registered;

static HMENU system_menu;
static THREAD window_thread;
static ScreenState shared_state; static CRITICAL_SECTION state_mutex;
static ScreenState current_state, last_syncd_state;
static ScreenMode current_mode; // only read/written by the UI thread.
static HANDLE loop_event;
static ScreenState last_render_state; // the state the render thread used for the last update
static HANDLE render_requested_event;
static TCHAR title_string[256];

static bool current_params_changed;
static bool in_sendmessage; // someone on the main thread is SendMessage()ing our window, so don't wait on the main thread or we'll deadlock.
static bool in_setwindowplacement; // i.e., our code called SetWindowPlacement somewhere down on the stack
static bool cursor_clip_pending;
static bool window_in_modal_loop;
static bool mouse_captured;
static char mouse_down_flags;

// relative cursor mode state
struct 
{
	int orig_x, orig_y; // original mouse position before entering relative mode
	int mousemove_count;
	int recenter_pending;
} relcursor_state;

// NB: modkeys is intetionally implemented in a partially asynchronous fashion;
// this avoids the possibility of a mod key being stuck due to missing a KEYUP message,
// but the mod bits might not always be *precisely* in sync.
static unsigned short modkeys;

static FILE *log_file;

static ScreenModePrefs default_prefs;

static bool create_window();
static void destroy_window();
static bool send_message(const WindowMessage& message);
static bool set_display_mode(const ScreenMode *mode, bool exclusive_fullscreen_mode);
static void apply_prefs_GUI();
static void apply_mode_GUI();
void clip_cursor(bool clip, bool center);

static struct {
	WNDPROC wp;
	HICON icon;
	LPCTSTR title;
} cw_params;

struct applymode_params {
	const ScreenMode *mode;
	const ScreenModePrefs *prefs;
};

static WINDOWPLACEMENT *get_windowed_placement(void)
{
	WINDOWPLACEMENT *placement = &current_state.prefs.placement;
	return placement->length ? placement : NULL;
}

static void save_windowed_placement(void)
{
	WINDOWPLACEMENT *placement = &current_state.prefs.placement;
	placement->length = sizeof(*placement);
	if (!GetWindowPlacement(screen_window, placement))
	{
		placement->length = 0;
	}
}

static bool is_windowed_now(void)
{
	return screen_window && ((GetWindowLongPtr(screen_window, GWL_STYLE) & WS_CAPTION) != 0);
}

static bool is_true_fullscreen_now(void)
{
	return screen_window && ((GetWindowLongPtr(screen_window, GWL_STYLE) & (WS_POPUP | WS_CAPTION)) == WS_POPUP);
}

static SIZE get_client_size()
{
	RECT client;
	ZeroMemory(&client, sizeof(client));
	GetClientRect(screen_window, &client);
	SIZE size = { rectangle_width(client), rectangle_height(client) };
	return size;
}

static SIZE get_border_size(DWORD style, DWORD ex_style, BOOL menu)
{
	const int dummy_size = 1024; // could use 0, but I'm paranoid about windows possibly special-casing very small windows.
	RECT dummy = { 0, 0, dummy_size, dummy_size };
	SIZE size = { 0, 0 };

	if (AdjustWindowRectEx(&dummy, style, menu, ex_style))
	{
		size.cx = rectangle_width(dummy) - dummy_size;
		size.cy = rectangle_height(dummy) - dummy_size;
	}

	return size;
}

static SIZE get_border_size(HWND hWnd, BOOL menu)
{
	DWORD style = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE);
	DWORD exstyle = (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE);
	return get_border_size(style, exstyle, menu);
}

void set_window_pos(HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags, bool nosendchanging = true)
{
	if(nosendchanging)
	{
		uFlags |= SWP_NOSENDCHANGING;
	}

	SetWindowPos(screen_window, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static void sync_state(int raise_flags = 0)
{
	if (raise_flags || memcmp(&current_state, &last_syncd_state, sizeof(ScreenState)))
	{
		int changed_fields = raise_flags;

		if (memcmp(&current_state.prefs, &last_syncd_state.prefs, sizeof(ScreenModePrefs)))
		{
			changed_fields |= Prefs;
		}

		if (memcmp(&current_state.dimensions, &last_syncd_state.dimensions, sizeof(SIZE)))
		{
			changed_fields |= Dimensions;
		}

		if (memcmp(&current_state.dpi, &last_syncd_state.dpi, sizeof(SIZE)))
		{
			changed_fields |= Dpi;
		}

		if (memcmp(&current_state.dm, &last_syncd_state.dm, sizeof(DEVMODE)))
		{
			changed_fields |= Devmode;
		}

		if (current_state.window_status != last_syncd_state.window_status)
		{
			changed_fields |= Status;
		}

		if (current_state.power_status != last_syncd_state.power_status)
		{
			changed_fields |= Power;
		}

		if (current_state.cursor_clipped_now != last_syncd_state.cursor_clipped_now)
		{
			changed_fields |= CursorClipped;
		}

		if (changed_fields)
		{
			EnterCriticalSection(&state_mutex);
			{
				int prev_changed_set = shared_state.changed_fields;
				shared_state = current_state;
				shared_state.changed_fields = prev_changed_set | changed_fields;
			}
			LeaveCriticalSection(&state_mutex);
		}

		last_syncd_state = current_state;
	}
}

inline void set_window_status(WindowStatus status)
{
	current_state.window_status = status;

	// handle display switching for true fullscreen
	if (is_true_fullscreen_now())
	{
		if (current_state.window_status == Normal)
		{
			apply_prefs_GUI();
		}
		else 
		{
			// minimize and restore the regular desktop mode
			ShowWindow(screen_window, SW_MINIMIZE);
			set_display_mode(NULL, false);
		}
	}

	// handle cursor clipping
	if (current_state.window_status == Normal)
	{
		// clip later
		cursor_clip_pending = current_state.prefs.clip_cursor;
	}
	else
	{
		// unclip
		clip_cursor(false, false);
	}

	sync_state();
}

inline void set_power_status(PowerStatus power)
{
	current_state.power_status = power;
	sync_state();
}

static void set_always_on_top(
	bool on_top)
{
	bool already_on_top = (GetWindowLongPtr(screen_window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	if (on_top != already_on_top)
	{
		set_window_pos(on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	}
}

void clip_cursor(bool clip, bool center)
{
	RECT rect;

	if (GetClientRect(screen_window, &rect))
	{
		MapWindowPoints(screen_window, NULL, (LPPOINT)&rect, 2);

		if (center)
		{
			POINT cursor;

			// If the cursor is outside the window, center it in the window.
			if (GetCursorPos(&cursor) && !PtInRect(&rect, cursor))
			{
				SetCursorPos((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
			}
		}

		ClipCursor(clip ? &rect : NULL);
	}

	current_state.cursor_clipped_now = clip;
	sync_state();
}

void show_cursor_GUI(bool show)
{
	if(show) while(ShowCursor(TRUE) < 0);
	else     while(ShowCursor(FALSE) >= 0);
}

void enable_relative_cursor_GUI(bool enable)
{
	if(current_state.cursor_relative_now != enable)
	{
		current_state.cursor_relative_now = enable;
		relcursor_state.mousemove_count = 0;

		if(enable)
		{
			get_mouse_position(&relcursor_state.orig_x, &relcursor_state.orig_y);
			clip_cursor(true, true);
		}
		else
		{
			set_mouse_position(relcursor_state.orig_x, relcursor_state.orig_y);
			clip_cursor(current_state.prefs.clip_cursor, false);
		}

		show_cursor_GUI(!enable);
	}
}

static void toggle_always_on_top_GUI(
	void)
{
	if (is_windowed_now())
	{
		current_state.prefs.top = !current_state.prefs.top;
		set_always_on_top(current_state.prefs.top);
		sync_state();
	}
}

static void toggle_windowed_mode_GUI(
	void)
{
	if (current_mode.allow_windowed && (current_mode.allow_borderless || current_mode.allow_fullscreen))
	{
		current_state.prefs.windowed = !is_windowed_now();
		apply_mode_GUI();
	}
}

static void toggle_grab_mode_GUI(
	void)
{
	if (current_mode.allow_windowed)
	{
		current_state.prefs.clip_cursor = !current_state.prefs.clip_cursor;
		clip_cursor(current_state.prefs.clip_cursor, FALSE);
		sync_state();
	}
}

static void toggle_vsync_GUI()
{
	current_state.prefs.vsync = !current_state.prefs.vsync;
	sync_state();
}

static void update_prefs_GUI(const ScreenModePrefs *updated_prefs)
{
	current_state.prefs.top = updated_prefs->top;
	current_state.prefs.vsync = updated_prefs->vsync;
	if (current_state.prefs.windowed != updated_prefs->windowed)
	{
		current_state.prefs.windowed = updated_prefs->windowed;
		apply_mode_GUI();
	}
	if (is_windowed_now())
	{
		set_always_on_top(current_state.prefs.top);
	}
	sync_state();
}

static void apply_prefs_GUI()
{
	bool on_top = false;
	bool clipped = false;
	bool centered = false;

	bool windowed_now = is_windowed_now();
	bool true_fullscreen_now = is_true_fullscreen_now();

	set_display_mode(&current_mode, true_fullscreen_now);

	switch (current_state.window_status)
	{
	case Normal:
		on_top = current_state.prefs.top && windowed_now || true_fullscreen_now;
		clipped = current_state.prefs.clip_cursor;
		centered = !windowed_now;
		break;
	case Background:
	case Hidden:
		on_top = current_state.prefs.top && windowed_now;
		break;
	}

	if (clipped && windowed_now)
	{
		// In windowed mode, mark clips as pending instead of applying immediately,
		// to allow interaction with the window frame.
		cursor_clip_pending = true;
	}
	else
	{
		clip_cursor(clipped, centered);
	}

	set_always_on_top(on_top);
}

static void apply_mode_GUI()
{
	const DWORD basic_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE;
	const DWORD windowed_style = WS_POPUPWINDOW | WS_CAPTION | WS_MINIMIZEBOX;
	const DWORD resizable_style = WS_OVERLAPPEDWINDOW;
	const DWORD fullscreen_style = WS_POPUP;
	const DWORD windowed_fullscreen_style = 0;

	DWORD style = basic_style;
	WINDOWPLACEMENT *placement = NULL;

	ScreenMode& mode = current_mode;
	ScreenModePrefs& prefs = current_state.prefs;

	int mode_width = mode.dm.dmPelsWidth;
	int mode_height = mode.dm.dmPelsHeight;
	
	RECT rect = { 0, 0, mode_width, mode_height };

	HMONITOR monitor = MonitorFromWindow(screen_window, MONITOR_DEFAULTTONEAREST);
	RECT monitor_rect;
	RECT workarea;

	wsi::get_monitor_rect(monitor, &monitor_rect, FALSE);
	wsi::get_monitor_rect(monitor, &workarea, TRUE);
	wsi::get_monitor_dpi(monitor, &current_state.dpi);

	bool windowed = prefs.windowed;

	if (rectangle_width(workarea) <= mode_width &&
		rectangle_height(workarea) <= mode_height)
	{
		// work area is smaller than the window in at least one dimension
		windowed = false;
	}

	if (!set_display_mode(&mode, !windowed && !mode.allow_borderless && mode.allow_fullscreen))
	{
		// If we got here, it probably means that ChangeDisplaySettings failed for some reason.
		// In this case, fall back to windowed mode:
		prefs.windowed = windowed = true; // Alter the preference - if we kept it, it would probably fail again next time.
	}

	// Compute the desired style and placement.
	if (windowed)
	{
		// Windowed mode.

		placement = get_windowed_placement();

		style |= mode.allow_resize ? resizable_style : windowed_style;
		AdjustWindowRectEx(&rect, style, FALSE, 0);

		int width = rectangle_width(rect);
		int height = rectangle_height(rect);

		if (!placement)
		{
			// We don't have a saved placement yet, so start the window in the middle of the work area.
			if (mode.allow_resize)
			{
				// Resizing is allowed, so resize to account for DPI
				rect.right = rect.left + MulDiv(width, current_state.dpi.cx, 96);
				rect.bottom = rect.top + MulDiv(height, current_state.dpi.cy, 96);
			}
			fit_and_center_rect(rect, workarea, TRUE);
		}
		else if (current_params_changed || !mode.allow_resize)
		{
			// Resolution changed so shrink or expand our placement while keeping the same center.
			int center_x = (placement->rcNormalPosition.left + placement->rcNormalPosition.right) / 2;
			int center_y = (placement->rcNormalPosition.top + placement->rcNormalPosition.bottom) / 2;
			placement->rcNormalPosition.left = center_x - width / 2;
			placement->rcNormalPosition.right = placement->rcNormalPosition.left + width;
			placement->rcNormalPosition.top = center_y - height / 2;
			placement->rcNormalPosition.bottom = placement->rcNormalPosition.top + height;
			// Make sure we don't go off the screen. Some docs say that SetWindowPlacement() handles this automatically -- it doesn't.
			fit_and_center_rect(placement->rcNormalPosition, workarea, FALSE);
		}
		else
		{
			// reuse the existing placement data
		}
	}
	else if (mode.allow_borderless)
	{
		// Fullscreen windowed mode => cover our active monitor.
		style |= windowed_fullscreen_style;
		rect = monitor_rect;
	}
	else
	{
		// True fullscreen on primary monitor.
		style |= fullscreen_style;
	}

	// Make sure we're not iconic, or else resizing won't work.
	if (IsIconic(screen_window))
	{
		ShowWindow(screen_window, SW_RESTORE);
	}

	{
		// This hack fixes the taskbar covering the game window in Windowed Fullscreen mode.
		// We remove the WS_VISIBLE bit, then we set the window visible again (below with SWP_SHOWWINDOW).
		// Apparently this causes Windows to re-evaluate whether or not the window is "fullscreen",
		// and specifically, whether the taskbar should be behind our window or not.
		style &= ~WS_VISIBLE;
	}

	// Move and resize the window appropriately.
	if (windowed)
	{
		// In windowed mode, we need to first move/resize the window, then apply the style as a separate step,
		// otherwise the above hack (removing the visible flag) can cause the old window rectangle to not be redrawn.

		// Update: actually, we need to change the style first and then move/resize second,
		// becase SetWindowPlacement needs the correct style set to calculate the client rectangle.

		SetWindowLongPtr(screen_window, GWL_STYLE, style);
		set_window_pos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

		if (placement)
		{
			if (placement->showCmd == SW_MAXIMIZE && !mode.allow_resize)
			{
				placement->showCmd = SW_SHOW;
			}
			in_setwindowplacement = true;
			SetWindowPlacement(screen_window, placement);
			in_setwindowplacement = false;
		}
		else
		{
			set_window_pos(0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
				SWP_NOZORDER | SWP_NOOWNERZORDER, false);
		}
	}
	else
	{
		// In fullscreen mode, we can safely do it all in one step, because the new window rect (the entire screen) is guaranteed to cover the old window rect.
		SetWindowLongPtr(screen_window, GWL_STYLE, style);
		set_window_pos(HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW, false);
	}

	current_params_changed = false;

	// Bring us into the foreground
	SetForegroundWindow(screen_window);

	// Apply window prefs
	apply_prefs_GUI();

	// Sync the window status and dimensions
	{
		current_state.window_status = Normal;

		if (current_mode.allow_resize)
		{
			current_state.dimensions = get_client_size();
		}
		else
		{
			current_state.dimensions.cx = mode_width;
			current_state.dimensions.cy = mode_height;
		}

		sync_state();
	}
}

static int window_thread_init(THREAD *, void *)
{
	HINSTANCE hinstance = GetModuleHandle(NULL);

	// Register the window class
	if(!window_class_registered)
	{
		WNDCLASS wndclass;
		memset(&wndclass, 0, sizeof(wndclass));
		wndclass.lpszClassName = MAIN_WINDOW_CLASS;
		wndclass.hInstance = hinstance;
		wndclass.lpfnWndProc = cw_params.wp;
		wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
		wndclass.hIcon = cw_params.icon;
		wndclass.lpszMenuName = NULL;
		wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wndclass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;

		if(!RegisterClass(&wndclass))
		{
			return false;
		}

		window_class_registered = true;
	}

	// Create the window
	{
		POINT pos = { 0, 0 };
		GetCursorPos(&pos); // create the window at the cursor position, so it starts on the correct monitor.
		screen_window = CreateWindowEx(WS_EX_APPWINDOW, MAIN_WINDOW_CLASS, cw_params.title,
			WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE | WS_POPUPWINDOW,
			pos.x, pos.y, 0, 0, NULL, NULL, hinstance, NULL);

		if(!screen_window)
		{
			return false;
		}
	}

	return true;
}

static void handle_close()
{
	sync_state(QuitRaised);
}

static void handle_system_keystrokes(WindowMessage& message)
{
	// ctrl+K: vsync
	// ctrl+G: grab
	// ctrl+A: on top
	// alt+enter: fullscreen
	// alt+f4: quit
	if (message.keystroke.modkeys & wsi::modRepeat)
		return;
	if (message.keystroke.modkeys == wsi::modControl)
	{
		switch (message.keystroke.code)
		{
		case 'G': wsi::toggle_clip(); break;
		case 'K': wsi::toggle_vsync(); break;
		case 'A': wsi::toggle_on_top(); break;
		}
	}
	else if (message.keystroke.modkeys == wsi::modAlt)
	{
		switch (message.keystroke.code)
		{
		case VK_RETURN: wsi::toggle_fullscreen(); break;
		case VK_F4: handle_close(); break;
		}
	}
}

static void handle_window_resized(int new_width, int new_height, bool synchronize)
{
	SIZE border = get_border_size(screen_window, FALSE);
	int client_width = std::max<int>(new_width - border.cx, current_mode.min_width);
	int client_height = std::max<int>(new_height - border.cy, current_mode.min_height);

	if (current_state.dimensions.cx != client_width ||
		current_state.dimensions.cy != client_height)
	{
		if (in_sendmessage)
		{
			// don't deadlock
			synchronize = false;
		}

		if (synchronize)
		{
			// FIXME: need feedback from Microsoft about how to really do this right.
#if 1
			// Sync method 1. Attempt to have the "next" frame presented at the new size.

			// 1. Wait till the render thread is starting a new main loop iteration -
			//    this may or may not present a frame.
			SetEvent(render_requested_event);
			WaitForSingleObject(loop_event, 1000);

			// 2. Send the resize signal
			current_state.dimensions.cx = client_width;
			current_state.dimensions.cy = client_height;
			sync_state();

			// 3. Wait for the render thread to begin a new main loop iteration -
			//    this presents one last frame at the old size
			SetEvent(render_requested_event);
			WaitForSingleObject(loop_event, 1000);

			// 4. Now the next present will be done at the correct size, so continue.
#else
			// Sync method 2: Wait until a frame has already been presented at the new size.
			current_state.dimensions.cx = client_width;
			current_state.dimensions.cy = client_height;
			sync_state();

			for (;;)
			{
				bool dimensions_correct;
				EnterCriticalSection(&state_mutex);
				dimensions_correct =
					client_width == last_render_state.dimensions.cx &&
					client_height == last_render_state.dimensions.cy;
				LeaveCriticalSection(&state_mutex);
				if(dimensions_correct) break;
				SetEvent(render_requested_event);
				WaitForSingleObject(loop_event, 1000);
			}
#endif
		}
	}
}

static void handle_wm_sizing(LPRECT rect, WPARAM mode)
{
	SIZE border = get_border_size(screen_window, FALSE);
	int desired_width = std::max<int>(rectangle_width(*rect) - border.cx, current_mode.min_width);
	int desired_height = std::max<int>(rectangle_height(*rect) - border.cy, current_mode.min_height);

	if (current_mode.allow_resize)
	{
		
	}
	else
	{
		desired_width = current_state.dimensions.cx;
		desired_height = current_state.dimensions.cy;
	}

	desired_width += border.cx;
	desired_height += border.cy;

	switch (mode)
	{
	case WMSZ_LEFT:
	case WMSZ_TOPLEFT:
	case WMSZ_BOTTOMLEFT:
		rect->left = rect->right - desired_width;
		break;
	case WMSZ_RIGHT:
	case WMSZ_TOPRIGHT:
	case WMSZ_BOTTOMRIGHT:
		rect->right = rect->left + desired_width;
		break;
	}

	switch (mode)
	{
	case WMSZ_TOP:
	case WMSZ_TOPLEFT:
	case WMSZ_TOPRIGHT:
		rect->top = rect->bottom - desired_height;
		break;
	case WMSZ_BOTTOM:
	case WMSZ_BOTTOMLEFT:
	case WMSZ_BOTTOMRIGHT:
		rect->bottom = rect->top + desired_height;
		break;
	}
}


LRESULT WINAPI wsi::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	enum {
#define MAKE_COMMAND(c) ((c)<<4)
		FIRST_COMMAND = 1337,
		CMD_ON_TOP = MAKE_COMMAND(FIRST_COMMAND),
		CMD_TOGGLE_GRAB = MAKE_COMMAND(FIRST_COMMAND + 1),
		CMD_TOGGLE_VSYNC = MAKE_COMMAND(FIRST_COMMAND + 2),
		CMD_TOGGLE_FS = MAKE_COMMAND(FIRST_COMMAND + 3),
#undef MAKE_COMMAND
	};

	// Handle system messages
	switch (uMsg)
	{
	case WM_CREATE:
		system_menu = GetSystemMenu(hWnd, FALSE);
		InsertMenuA(system_menu, 0, 0, CMD_ON_TOP, "On Top\tCtrl+A");
		InsertMenuA(system_menu, 0, 0, CMD_TOGGLE_GRAB, "Grab Cursor\tCtrl+G");
		InsertMenuA(system_menu, 0, 0, CMD_TOGGLE_VSYNC, "Vsync\tCtrl+K");
		InsertMenuA(system_menu, 0, 0, CMD_TOGGLE_FS, "Fullscreen\tAlt+Enter");
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_CLOSE:
		handle_close();
		return 0;
	case WM_INITMENUPOPUP:
		if ((HMENU)wParam == system_menu)
		{
			CheckMenuItem(system_menu, CMD_ON_TOP, MF_BYCOMMAND | (current_state.prefs.top ? MF_CHECKED : 0));
			CheckMenuItem(system_menu, CMD_TOGGLE_GRAB, MF_BYCOMMAND | (current_state.prefs.clip_cursor ? MF_CHECKED : 0));
			CheckMenuItem(system_menu, CMD_TOGGLE_VSYNC, MF_BYCOMMAND | (current_state.prefs.vsync ? MF_CHECKED : 0));
			CheckMenuItem(system_menu, CMD_TOGGLE_FS, MF_BYCOMMAND | (!is_windowed_now() ? MF_CHECKED : 0));
			return 0;
		}
		break;
	case WM_ACTIVATE:
		{
			int minimized = HIWORD(wParam);
			int activated = LOWORD(wParam);
			set_window_status(minimized ? Hidden : activated == FALSE ? Background : Normal);
		}
		return 0;
	case WM_MOUSEACTIVATE:
		if (LOWORD(lParam) == HTCLIENT)
		{
			clip_cursor(current_state.prefs.clip_cursor, false);
			return MA_ACTIVATEANDEAT;
		}
		return MA_ACTIVATE;
	case WM_SYSKEYUP:
	case WM_KEYUP:
		if (wParam < 256)
		{
			int vkey = (int)wParam;
			if (vkey == VK_CONTROL) modkeys &= ~modControl;
			else if (vkey == VK_MENU) modkeys &= ~modAlt;
			else if (vkey == VK_SHIFT) modkeys &= ~modShift;
		}
		return 0;
	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
		if (wParam < 256)
		{
			int vkey = (int)wParam;
			if (vkey == VK_CONTROL) modkeys |= modControl;
			else if (vkey == VK_MENU) modkeys |= modAlt;
			else if (vkey == VK_SHIFT) modkeys |= modShift;

			WindowMessage message(Keystroke);
			message.keystroke.code = (char)vkey;
			message.keystroke.modkeys = modkeys;
			if (lParam & (1 << 30)) message.keystroke.modkeys |= modRepeat;

			keyboard_state[vkey] = TRUE; // keep cached state in sync with input messages
			handle_system_keystrokes(message);
			send_message(message);
		}
		return 0;
	case WM_CHAR:
		if (wParam < 256)
		{
			WindowMessage message(Character);
			message.character.code = (char)wParam;
			message.character.modkeys = modkeys;
			send_message(message);
		}
	return 0;
	case WM_SYSCHAR:
		// prevent alt+space from bringing up the window menu
		return 0;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		{
			WindowMessage message(Mouse);

			short x = short(LOWORD(lParam));
			short y = short(HIWORD(lParam));

			char button = 0;
			if (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP) {
				button = 1;
			} else if (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP) {
				button = 2;
			}

			char type = MouseMove;
			if (uMsg == WM_MOUSEWHEEL)
			{
				type = MouseWheel;
				y = GET_WHEEL_DELTA_WPARAM(wParam);
			}
			else if(uMsg == WM_MOUSEHWHEEL)
			{
				type = MouseWheel;
				x = GET_WHEEL_DELTA_WPARAM(wParam);
			}
			else if (uMsg == WM_MOUSEMOVE && current_state.cursor_relative_now)
			{
				type = MouseDelta;
				int center_x = current_state.dimensions.cx / 2;
				int center_y = current_state.dimensions.cy / 2;
				// convert to deltas
				x = short(x - center_x);
				y = short(y - center_y);

				// if the cursor isn't centered, recenter at the next opportunity
				relcursor_state.recenter_pending = x || y;

				// keep track of the movement count
				relcursor_state.mousemove_count += 1;
			}
			else if (uMsg != WM_MOUSEMOVE)
			{
				if (uMsg == WM_LBUTTONDOWN ||
					uMsg == WM_RBUTTONDOWN ||
					uMsg == WM_MBUTTONDOWN)
				{
					type = MouseDown;
					mouse_down_flags |= 1<<button;
					if(!mouse_captured && mouse_down_flags) {
						SetCapture(hWnd);
						mouse_captured = true;
					}
				}
				else
				{
					type = MouseUp;
					mouse_down_flags &= ~(1<<button);
					if(mouse_captured && !mouse_down_flags) {
						ReleaseCapture();
						mouse_captured = false;
					}
				}
			}

			unsigned short mouse_mods = 0;
			if (wParam & MK_LBUTTON) mouse_mods |= modLMouse;
			if (wParam & MK_RBUTTON) mouse_mods |= modRMouse;
			if (wParam & MK_SHIFT) mouse_mods |= modShift;
			if (wParam & MK_CONTROL) mouse_mods |= modControl;
			if (wParam & MK_MBUTTON) mouse_mods |= modMMouse;

			message.mouse.button = button;
			message.mouse.type = type;
			message.mouse.modkeys = modkeys | mouse_mods;
			message.mouse.x = x;
			message.mouse.y = y;

			bool cursor_over_client =
				!window_in_modal_loop &&
				(current_state.cursor_relative_now ||
				 (message.mouse.x >= 0 && message.mouse.x  <= current_state.dimensions.cx &&
				  message.mouse.y >= 0 && message.mouse.y <= current_state.dimensions.cy));

			if(cursor_over_client   ||
			   uMsg == WM_LBUTTONUP ||
			   uMsg == WM_RBUTTONUP ||
			   uMsg == WM_MBUTTONUP ||
			   uMsg == WM_MOUSEWHEEL ||
			   uMsg == WM_MOUSEHWHEEL)
			{
				if(type == MouseDelta && (
					relcursor_state.mousemove_count <= 1 ||
					x == 0 && y == 0))
				{
					// ignore zero-deltas and the first movement after entering relative mode
				}
				else
				{
					send_message(message);
				}
			}

			if (cursor_clip_pending &&
				(message.mouse.type != MouseUp))
			{
				clip_cursor(true, false);
				cursor_clip_pending = false;
			}
		}
		return 0;
	case WM_SYSCOMMAND:
		{
			// TODO: make the commands user-configurable?
			int command = wParam & 0xfff0;
			switch (command)
			{
			case CMD_ON_TOP:
				toggle_always_on_top_GUI();
				return 0;
			case SC_MAXIMIZE:
				if (!current_mode.allow_resize)
				{
					toggle_windowed_mode_GUI();
					return 0;
				}
				break;
			case SC_CLOSE:
				handle_close();
				return 0;
			case CMD_TOGGLE_FS:
				toggle_windowed_mode_GUI();
				return 0;
			case CMD_TOGGLE_GRAB:
				toggle_grab_mode_GUI();
				return 0;
			case CMD_TOGGLE_VSYNC:
				toggle_vsync_GUI();
				return 0;
			}

			// let windows do its thing
			window_in_modal_loop = true;
			LRESULT result = DefWindowProc(hWnd, WM_SYSCOMMAND, wParam, lParam);
			window_in_modal_loop = false;

			cursor_clip_pending = current_state.prefs.clip_cursor;

			return result;
		}
	case WM_ERASEBKGND:
		return TRUE;
	case WM_PAINT:
		{
			RECT rect;
			if (GetUpdateRect(hWnd, &rect, FALSE))
			{
				current_state.changed_fields |= RedrawRaised;
				sync_state();
			}
			// no return value; fallthrough to the default behavior (which validates the region)
		}
		break;
	case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO info = (LPMINMAXINFO)lParam;
			RECT rect;

			if (GetWindowRect(hWnd, &rect))
			{
				if (!current_mode.allow_resize)
				{
					// Resizing isn't allowed!
					info->ptMaxPosition.x = rect.left;
					info->ptMaxPosition.y = rect.top;
					info->ptMaxSize.x = rectangle_width(rect);
					info->ptMaxSize.y = rectangle_height(rect);
				}

				{
					SIZE border = get_border_size(hWnd, FALSE);
					info->ptMinTrackSize.x = current_mode.min_width + border.cx;
					info->ptMinTrackSize.y = current_mode.min_height + border.cy;
				}
			}
		}
		return 0;
	case WM_WINDOWPOSCHANGING:
		{
			// Call DefWindowProc first to validate the new dimensions.
			LRESULT result = DefWindowProc(hWnd, uMsg, wParam, lParam);
			if (current_mode.allow_resize &&
				!in_setwindowplacement)
			{
				LPWINDOWPOS wp = (LPWINDOWPOS)lParam;
				if (!(wp->flags & SWP_NOSIZE) && !IsIconic(hWnd))
				{
					handle_window_resized(wp->cx, wp->cy, true);
				}
			}
			return result;
		}
	case WM_WINDOWPOSCHANGED:
		{
			LPWINDOWPOS wp = (LPWINDOWPOS)lParam;
			if ((~wp->flags) & (SWP_NOMOVE | SWP_NOSIZE))
			{
				// On XP and earlier a window can actually be active while minimized - if it's
				// the only one in the taskbar. So make sure we're paused while minimized.
				BOOL minimized = IsIconic(hWnd);
				WindowStatus desired_status = minimized ? Hidden : Normal;
				if (current_state.window_status != desired_status)
				{
					set_window_status(desired_status);
				}
	
				if (!minimized)
				{
					if (is_windowed_now())
					{
						save_windowed_placement();
					}

					if (current_mode.allow_resize)
					{
						handle_window_resized(wp->cx, wp->cy, true);
					}
				}
			}
		}
		// NOTE: WM_MOVE and WM_SIZE will not be generated if we do not pass WM_WINDOWPOSCHANGED to DefWindowProc.
		return 0;
	case WM_SIZING:
		handle_wm_sizing((LPRECT)lParam, wParam);
		return TRUE;
	case WM_POWERBROADCAST:
		{
			SYSTEM_POWER_STATUS power_status;
			if (GetSystemPowerStatus(&power_status))
			{
				set_power_status(power_status.ACLineStatus == 0 ? OnBattery : PluggedIn);
			}
		}
		return TRUE;
	case 0x02E0: //WM_DPICHANGED:
		{
			current_state.dpi.cx = LOWORD(wParam);
			current_state.dpi.cy = HIWORD(wParam);
			if (current_mode.allow_resize && is_windowed_now())
			{
				LPRECT new_rect = (LPRECT)lParam;
				set_window_pos(NULL, new_rect->left, new_rect->top,
					rectangle_width(*new_rect), rectangle_height(*new_rect),
					SWP_NOZORDER);
			}
			sync_state();
		}
		return 0;
	}

	// Handle user messages
	switch (uMsg)
	{
	case WM_SETMODE:
		current_mode = *(((applymode_params*)lParam)->mode);
		current_state.prefs = *(((applymode_params*)lParam)->prefs);
		current_params_changed = true;
		apply_mode_GUI();
		return 0;
	case WM_SETPREFS:
		update_prefs_GUI((const ScreenModePrefs*)lParam);
		return 0;
	case WM_TOGGLEFULLSCREEN:
		toggle_windowed_mode_GUI();
		break;
	case WM_TOGGLEONTOP:
		toggle_always_on_top_GUI();
		break;
	case WM_TOGGLEGRABMODE:
		toggle_grab_mode_GUI();
		break;
	case WM_TOGGLEVSYNC:
		toggle_vsync_GUI();
		break;
	case WM_DESTROYSELF:
		DestroyWindow(hWnd);
		return 0;
	case WM_SHOWCURSOR:
		// Because ShowCursor must be called in the GUI thread to work...
		show_cursor_GUI(wParam!=0);
		return 0;
	case WM_RELCURSOR:
		enable_relative_cursor_GUI(wParam!=0);
		return 0;
	case WM_SETTITLE:
		SetWindowText(hWnd, title_string);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static int window_thread_proc(THREAD *, void *)
{
	MSG msg;
	int result;

	while ((result = GetMessage(&msg, NULL, 0, 0)) != 0)
	{
		if (result < 0)
		{
			// An error happened
			return -1;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		// If necessary, reset the cursor position to the center of the window.
		// This can't be done inside WM_MOUSEMOVE (SetCursorPos fails)
		if (current_state.cursor_relative_now && relcursor_state.recenter_pending)
		{
			int center_x = current_state.dimensions.cx / 2;
			int center_y = current_state.dimensions.cy / 2;
			set_mouse_position(center_x, center_y);
			relcursor_state.recenter_pending = false;
		}
	}

	return 0;
}

static bool create_window()
{
	return create_thread(&window_thread, "Window Thread", &window_thread_proc, &window_thread_init, NULL);
}

static void send_message(UINT Msg, WPARAM wParam, LPARAM lParam)
{
	in_sendmessage = true;
	SendMessage(screen_window, Msg, wParam, lParam);
	in_sendmessage = false;
}

static void destroy_window()
{
	send_message(WM_DESTROYSELF, 0, 0);
	wait_thread(&window_thread);
}

bool wsi::initialize(const TCHAR *title, HICON icon, WNDPROC wndproc, const char *logfile_name)
{
	enable_dpi_awareness();
	disable_exception_swallowing();

	default_prefs.windowed = true;

	loop_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	render_requested_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&state_mutex);

	message_Q.init(NULL, sizeof(WindowMessage), MESSAGE_Q_MAX_LENGTH);

	if (logfile_name)
	{
		log_file = fopen(logfile_name, "w");

		time_t rawtime;
		time(&rawtime);
		tm timeinfo;
		localtime_s(&timeinfo, &rawtime);
		log_message(NULL, 0, "%s started on %s", title, asctime(&timeinfo));
	}

	if (!title) title = TEXT("WSI Window");
	if (!icon) icon = LoadIcon(NULL, IDI_APPLICATION);
	if (!wndproc) wndproc = WndProc;

	cw_params.title = title;
	cw_params.icon = icon;
	cw_params.wp = wndproc;

	return create_window();
}

void wsi::set_title(const TCHAR *title)
{
#ifdef UNICODE
	wcscpy_s(title_string, title);
#else
	strcpy_s(title_string, title);
#endif
	PostMessage(screen_window, WM_SETTITLE, 0, 0);
}

void wsi::shutdown()
{
	destroy_window();

	CloseHandle(loop_event);
	CloseHandle(render_requested_event);
	DeleteCriticalSection(&state_mutex);

	wsi::enable_timer_boost(false);

	message_Q.clear(true);

	if (log_file)
	{
		log_message(NULL, 0, "Quitting ...");
		fclose(log_file);
	}
}

void wsi::log_message(const char *file, int line, const char *format, ...)
{
	if (log_file)
	{
		if (file)
		{
			fprintf(log_file, "%s(%d): ", file, line);
		}
		va_list args;
		va_start(args, format);
		vfprintf(log_file, format, args);
		va_end(args);
		fputc('\n', log_file);
		fflush(log_file);
	}
}

bool wsi::should_render()
{
	return current_state.window_status != wsi::Hidden;
}

static void refresh_keyboard_state()
{
	for (int i = 0; i < 256; ++i)
	{
		if (i == VK_NUMLOCK ||
			i == VK_CAPITAL ||
			i == VK_NUMLOCK)
		{
			// "toggle" like capslock keys only work with GetKeyState
			keyboard_state[i] = (GetKeyState(i) & 1) != 0;
		}
		else
		{
			keyboard_state[i] = (GetAsyncKeyState(i) & 0x8001) != 0;
		}
	}

	modkeys =
		(keyboard_state[VK_CONTROL] ? modControl : 0) |
		(keyboard_state[VK_SHIFT] ? modShift : 0) |
		(keyboard_state[VK_MENU] ? modAlt : 0);
}

void wsi::update(const ScreenState *main_thread_state)
{
	refresh_keyboard_state();

	EnterCriticalSection(&state_mutex);
	memcpy(&last_render_state, main_thread_state, sizeof(ScreenState));
	LeaveCriticalSection(&state_mutex);

	SetEvent(loop_event);
}

void wsi::limit_fps(int max_fps)
{
	static int last_maxfps = 0;
	static unsigned current_frame = 0;
	static wsi::millisec64_t interval_start;

	bool interrupted = false;

	if (max_fps > 0)
	{
		// Wait until it's time to render the next frame
		for (;;)
		{
			wsi::millisec64_t now = wsi::milliseconds();
			unsigned elapsed_ms = unsigned(now - interval_start);
			unsigned target_frame = max_fps*elapsed_ms / 1000;

			if (max_fps != last_maxfps ||
				elapsed_ms > 1000)
			{
				// Begin a new time interval
				interval_start = now;
				current_frame = 0;
				last_maxfps = max_fps;
			}

			if (current_frame <= target_frame)
			{
				// We're either behind or on schedule - no sleep is required
				break;
			}

			// Sleep for 1ms, or until we get a render signal
			if (WAIT_OBJECT_0 == WaitForSingleObject(render_requested_event, 1))
			{
				// Got a render signal, stop limiting fps
				interrupted = true;
				break;
			}
		}
	}

	if (!interrupted)
	{
		// Increment the frame index
		current_frame += 1;
	}
}

bool wsi::read_state(ScreenState *state_copy, unsigned remove_mask)
{
	EnterCriticalSection(&state_mutex);
		*state_copy = shared_state;
		shared_state.changed_fields &= ~remove_mask;
	LeaveCriticalSection(&state_mutex);

	return 0 != (state_copy->changed_fields & remove_mask);
}

void wsi::set_mode(const ScreenMode& mode, const ScreenModePrefs *prefs, bool recreate_window)
{
	applymode_params params;
	params.mode = &mode;
	params.prefs = prefs ? prefs : &default_prefs;

	if (recreate_window)
	{
		destroy_window();
		create_window();
	}

	send_message(WM_SETMODE, 0, (LPARAM)&params);

	return;
}

void wsi::set_prefs(const ScreenModePrefs *prefs)
{
	send_message(WM_SETPREFS, 0, (LPARAM)prefs);
}

static bool send_message(const WindowMessage& message)
{
	void *write = message_Q.write();
	if (write)
	{
		memcpy(write, &message, sizeof(WindowMessage));
		return true;
	}
	return false;
}

bool wsi::read_message(WindowMessage *message)
{
	void *read = message_Q.read();
	if (read)
	{
		memcpy(message, read, sizeof(WindowMessage));
		return true;
	}
	return false;
}

bool wsi::get_mouse_position(int *x, int *y)
{
	bool success = false;
	POINT point;
	ZeroMemory(&point, sizeof(point));

	if (GetCursorPos(&point))
	{
		ScreenToClient(screen_window, &point);
		success = true;
	}
	*x = point.x;
	*y = point.y;

	return success && !window_in_modal_loop;
}

void wsi::set_mouse_position(int x, int y)
{
	POINT point = { x, y };
	if(ClientToScreen(screen_window, &point))
	{
		SetCursorPos(point.x, point.y);
	}
}

bool enum_display_settings(LPTSTR device, DEVMODE *dm, int mode_index)
{
	bool success;

	ZeroMemory(dm, sizeof(DEVMODE));
	dm->dmSize = sizeof(DEVMODE);

	success = (0 != EnumDisplaySettings(device, mode_index, dm));

	if (success && !(dm->dmPelsWidth && dm->dmPelsHeight))
	{
		success = false;
	}

	if (!success && mode_index == ENUM_REGISTRY_SETTINGS)
	{
		// Fallback to GetSystemMetrics for getting the desktop size.
		dm->dmPelsWidth = GetSystemMetrics(SM_CXSCREEN);
		dm->dmPelsHeight = GetSystemMetrics(SM_CYSCREEN);
		success = dm->dmPelsWidth && dm->dmPelsHeight;
	}

	if (!success)
	{
		ZeroMemory(dm, sizeof(DEVMODE));
		dm->dmSize = sizeof(DEVMODE);
	}

	return success;
}

static bool devmodes_close_enough(const DEVMODE& a, const DEVMODE& b)
{
	return
		a.dmBitsPerPel == b.dmBitsPerPel &&
		a.dmDisplayFrequency == b.dmDisplayFrequency &&
		a.dmPelsWidth == b.dmPelsWidth &&
		a.dmPelsHeight == b.dmPelsHeight &&
		a.dmDisplayFixedOutput == b.dmDisplayFixedOutput;
}

static bool set_display_mode(const ScreenMode *mode, bool exclusive_fullscreen_mode)
{
	if (!mode) assert(!exclusive_fullscreen_mode);

	bool desire_registry_settings = !exclusive_fullscreen_mode || !mode;

	DEVMODE &dm_current = current_state.dm, dm_registry;

	LONG result = DISP_CHANGE_FAILED;

	if (enum_display_settings(NULL, &dm_current, ENUM_CURRENT_SETTINGS) &&
		enum_display_settings(NULL, &dm_registry, ENUM_REGISTRY_SETTINGS))
	{
		bool using_registry_settings = devmodes_close_enough(dm_current, dm_registry);
		bool using_mode_settings = mode && devmodes_close_enough(dm_current, mode->dm);

		if (using_registry_settings && desire_registry_settings ||
			using_mode_settings && !desire_registry_settings)
		{
			// nothing to do
			result = DISP_CHANGE_SUCCESSFUL;
		}
		else if (desire_registry_settings)
		{
			// restore registry settings
			result = ChangeDisplaySettings(NULL, 0);
		}
		else
		{
			DEVMODE target_mode = mode->dm; // ChangeDisplaySettings wants a non-const object for some reason
			result = ChangeDisplaySettings(&target_mode, CDS_RESET | CDS_FULLSCREEN);
			if (result != DISP_CHANGE_SUCCESSFUL)
			{
				const DWORD essential_flags = DM_PELSWIDTH | DM_PELSHEIGHT;
				if ((target_mode.dmDisplayFlags & ~essential_flags) == essential_flags)
				{
					// try again with just the essential flags
					target_mode.dmDisplayFlags = essential_flags;
					result = ChangeDisplaySettings(&target_mode, CDS_RESET | CDS_FULLSCREEN);
				}

				if (result != DISP_CHANGE_SUCCESSFUL)
				{
					// fall back to desktop
					ChangeDisplaySettings(NULL, 0);
				}
			}
		}
	}

	sync_state();

	return (DISP_CHANGE_SUCCESSFUL == result);
}
