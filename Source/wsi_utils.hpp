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
