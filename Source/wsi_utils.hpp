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

#define NOMINMAX
#include <Windows.h>

#include <cassert>
#include <algorithm>

namespace wsi
{
	// basic threads

	typedef struct tagTHREAD
	{
		bool running, init_ok, exited, quit_signal;
		HANDLE init_event;
		HANDLE thread_handle;
		DWORD thread_id;
		int exit_code;
		void *user_data;
		const char *name;
		int(*init_proc)(struct tagTHREAD*, void*);
		int(*thread_proc)(struct tagTHREAD*, void*);
	} THREAD, *PTHREAD;

	void set_thread_name(DWORD thread_id, const char* thread_name);
	bool create_thread(THREAD *thread, const char *name, int(*thread_proc)(THREAD*, void*), int(*init_proc)(THREAD*, void*), void *user_data);
	int wait_thread(THREAD *thread);

	// misc windows stuff
	void enable_timer_boost(bool boost);
	void disable_exception_swallowing();

	// rects
	void fit_and_center_rect(RECT& rect, const RECT& container, bool center);
	inline int rectangle_width(const RECT& rect) { return rect.right - rect.left; }
	inline int rectangle_height(const RECT& rect) { return rect.bottom - rect.top; }

	// monitors
	bool get_monitor_rect(HMONITOR monitor, RECT *rect, bool workarea);
	bool get_monitor_dpi(HMONITOR monitor, SIZE *dpi);

	// time
	typedef unsigned long long millisec64_t;
	// NB: not thread safe!
	// recommended that this only be used from the main thread.
	millisec64_t milliseconds();

	// dpi awareness
	bool enable_dpi_awareness();

	// DXGI
	HRESULT make_dxgi_window_association(HWND hWnd, IUnknown *pD3dDevice);

	// HRESULTs
	bool hresult_succeeded(HRESULT hresult, const char *message, const char *file, int line);

	// basic message queue (lockless single reader / single writer)
	// NB: This is not thread safe with more than one reader or writer!
	struct queue
	{
		queue() { }
		~queue();
		void init(void *mem, int element_size, int diameter);
		void *read();
		void *write();
		void clear(bool dealloc);
		int size();
	private:
		void increment(int *index);
		void *element(int index);
	private:
		void *buffer;
		int read_index, write_index;
		int element_size;
		int diameter;
		bool user_mem;
	private:
		queue(const queue&);
		const queue& operator=(const queue&);
	};
}
