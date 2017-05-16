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
#include "wsi_utils.hpp"

#if 1 // enable this for older windows versions without multimonitor support
#pragma warning(disable:4996) // function marked deprecated
#define COMPILE_MULTIMON_STUBS
#include <MultiMon.h>
#endif

#include <dxgi.h>
#include <comdef.h>

#pragma comment(lib, "winmm.lib")

using namespace wsi;

static HMODULE shcore = (HMODULE)-1;

typedef enum PROCESS_DPI_AWARENESS {
	PROCESS_DPI_UNAWARE = 0,
	PROCESS_SYSTEM_DPI_AWARE = 1,
	PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;

typedef enum MONITOR_DPI_TYPE {
	MDT_EFFECTIVE_DPI = 0,
	MDT_ANGULAR_DPI = 1,
	MDT_RAW_DPI = 2,
	MDT_DEFAULT = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;

typedef HRESULT(STDAPICALLTYPE *SETPROCESSDPIAWARENESSPROC)(_In_ PROCESS_DPI_AWARENESS value);
static SETPROCESSDPIAWARENESSPROC SetProcessDpiAwareness;

typedef HRESULT (STDAPICALLTYPE *GETDPIFORMONITORPROC)(	_In_ HMONITOR hmonitor,
	_In_ MONITOR_DPI_TYPE dpiType, _Out_ UINT *dpiX, _Out_ UINT *dpiY);
static GETDPIFORMONITORPROC GetDpiForMonitor;

static bool init_shcore()
{
	if (shcore == (HMODULE)-1)
	{
		shcore = LoadLibraryA("shcore");
		SetProcessDpiAwareness = (SETPROCESSDPIAWARENESSPROC)GetProcAddress(shcore, "SetProcessDpiAwareness");
		GetDpiForMonitor = (GETDPIFORMONITORPROC)GetProcAddress(shcore, "GetDpiForMonitor");
	}
	return shcore != 0 && shcore != (HMODULE)-1;
}

static DWORD WINAPI thread_startup(LPVOID lpParameter)
{
	THREAD *thread = (THREAD*)lpParameter;

	if (thread->name)
	{
		set_thread_name(GetCurrentThreadId(), thread->name);
	}

	if (!thread->init_proc ||
		thread->init_proc(thread, thread->user_data))
	{
		thread->running = true;
		thread->init_ok = true;
		SetEvent(thread->init_event);
		thread->exit_code = thread->thread_proc(thread, thread->user_data);
	}
	else
	{
		SetEvent(thread->init_event);
		thread->exit_code = -1;
	}
	thread->running = false;
	thread->exited = true;
	return thread->exit_code;
}


void wsi::set_thread_name(DWORD thread_id, const char* thread_name)
{
	enum { MS_VC_EXCEPTION = 0x406D1388 };

	struct THREADNAME_INFO
	{
		DWORD dwType; // Must be 0x1000.
		LPCSTR szName; // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags; // Reserved for future use, must be zero.
	} info;

	info.dwType = 0x1000;
	info.szName = thread_name;
	info.dwThreadID = thread_id;
	info.dwFlags = 0;

	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

bool wsi::create_thread(THREAD *thread, const char *name, int(*thread_proc)(THREAD*, void*), int(*init_proc)(THREAD*, void*), void *user_data)
{
	memset(thread, 0, sizeof(*thread));
	thread->name = name;
	thread->thread_proc = thread_proc;
	thread->init_proc = init_proc;
	thread->user_data = user_data;

	thread->init_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!thread->init_event)
	{
		return false;
	}

	thread->thread_handle = CreateThread(NULL, 0, thread_startup, thread, 0, &thread->thread_id);
	if (!thread->thread_handle)
	{
		return false;
	}

	WaitForSingleObject(thread->init_event, INFINITE);
	CloseHandle(thread->init_event);
	thread->init_event = NULL;

	return (1==thread->init_ok);
}

int wsi::wait_thread(THREAD *thread)
{
	if (thread->thread_handle)
	{
		WaitForSingleObject(thread->thread_handle, INFINITE);
		assert(thread->exited && !thread->running);
		CloseHandle(thread->thread_handle);
		thread->thread_handle = NULL;
	}
	return thread->exit_code;
}

// In some cases, timeBeginPeriod(1) is necessary to get acceptable timing performance.
// Decreasing the timer period can increase power consumption, so it must be used responsibly.
void wsi::enable_timer_boost(bool boost)
{
	static bool currently_boosted = false;
	if (currently_boosted != boost)
	{
		if (!currently_boosted)
		{
			timeBeginPeriod(1);
		}
		else
		{
			timeEndPeriod(1);
		}
	}
}

// https://randomascii.wordpress.com/2012/07/05/when-even-crashing-doesnt-work/
void wsi::disable_exception_swallowing(void)
{
	typedef BOOL(WINAPI *tGetPolicy)(LPDWORD lpFlags);
	typedef BOOL(WINAPI *tSetPolicy)(DWORD dwFlags);
	const DWORD EXCEPTION_SWALLOWING = 0x1;

	HMODULE kernel32 = GetModuleHandleA("kernel32");
	tGetPolicy pGetPolicy = (tGetPolicy)GetProcAddress(kernel32, "GetProcessUserModeExceptionPolicy");
	tSetPolicy pSetPolicy = (tSetPolicy)GetProcAddress(kernel32, "SetProcessUserModeExceptionPolicy");

	if (pGetPolicy && pSetPolicy)
	{
		DWORD dwFlags;
		if (pGetPolicy(&dwFlags))
		{
			// Turn off the filter
			pSetPolicy(dwFlags & ~EXCEPTION_SWALLOWING);
		}
	}
}


void wsi::fit_and_center_rect(RECT& rect, const RECT& container, bool center)
{
	int rect_width = rectangle_width(rect);
	int rect_height = rectangle_height(rect);
	int container_width = rectangle_width(container);
	int container_height = rectangle_height(container);

	rect_width = std::min(rect_width, container_width);
	rect_height = std::min(rect_height, container_height);

	if (center)
	{
		rect.left = container.left + (container_width - rect_width) / 2;
		rect.right = rect.left + rect_width;
		rect.top = container.top + (container_height - rect_height) / 2;
		rect.bottom = rect.top + rect_height;
	}
	else
	{
		rect.left = std::max(rect.left, container.left);
		rect.left = std::min(rect.left, container.right - rect_width);
		rect.top = std::max(rect.top, container.top);
		rect.top = std::min(rect.top, container.bottom - rect_height);
		rect.right = rect.left + rect_width;
		rect.bottom = rect.top + rect_height;
	}
}

bool wsi::get_monitor_rect(HMONITOR monitor, RECT *rect, bool workarea)
{
	MONITORINFOEX info;

	ZeroMemory(&info, sizeof(info));
	info.cbSize = sizeof(info);

	ZeroMemory(rect, sizeof(RECT));

	if (GetMonitorInfo(monitor, (LPMONITORINFO)&info))
	{
		DEVMODE dm;
		ZeroMemory(&dm, sizeof(DEVMODE));
		dm.dmSize = sizeof(DEVMODE);

		// What we are really interested in is the registry settings for the monitor, not the current settings.
		if (!workarea && EnumDisplaySettings(info.szDevice, ENUM_REGISTRY_SETTINGS, &dm))
		{
			// Sometimes the device mode can report 0x0 (e.g. on remote desktop)
			if (dm.dmPelsHeight && dm.dmPelsWidth)
			{
				rect->top = info.rcMonitor.top;
				rect->left = info.rcMonitor.left;
				rect->right = rect->left + dm.dmPelsWidth;
				rect->bottom = rect->top + dm.dmPelsHeight;
			}
			else
			{
				*rect = info.rcMonitor;
			}
		}
		else
		{
			*rect = workarea ? info.rcWork : info.rcMonitor;
		}

		return true;
	}

	return false;
}

bool wsi::get_monitor_dpi(HMONITOR monitor, SIZE *dpi)
{
	if (init_shcore() && GetDpiForMonitor)
	{
		UINT x, y;
		HRESULT hr = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y);

		if (SUCCEEDED(hr))
		{
			dpi->cx = x;
			dpi->cy = y;

			return true;
		}
	}

	// windows default DPI
	dpi->cx = 96;
	dpi->cy = 96;

	return false;
}

wsi::millisec64_t wsi::milliseconds()
{
	static unsigned int last;
	static millisec64_t offset;
	static bool init = false;

	unsigned now = timeGetTime();

	if (!init)
	{
		last = now;
		init = true;
	}
	else if (now < last)
	{
		// wrapped around.
		offset += 1ull << 32;
	}

	last = now;

	return now + offset;
}

bool wsi::enable_dpi_awareness()
{
	static int succeeded = -1;

	if (succeeded == -1)
	{
		succeeded = 0;

		// Try to call the most modern API (SetProcessDpiAwareness)
		if (init_shcore() && SetProcessDpiAwareness)
		{
			SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
			succeeded = 1;
		}

		// Fallback to SetProcessDPIAware()
		if (!succeeded)
		{
			HMODULE user32 = GetModuleHandleA("user32");
			if (user32)
			{
				typedef BOOL(WINAPI *SETPROCESSDPIAWAREPROC)(VOID);
				SETPROCESSDPIAWAREPROC SetProcessDPIAware = (SETPROCESSDPIAWAREPROC)GetProcAddress(user32, "SetProcessDPIAware");
				if (SetProcessDPIAware)
				{
					SetProcessDPIAware();
					succeeded = 1;
				}
			}
		}
	}

	return succeeded == 1;
}

HRESULT wsi::make_dxgi_window_association(HWND hWnd, IUnknown *pD3dDevice)
{
	HRESULT hr;

	IDXGIDevice * pDXGIDevice = 0;
	hr = pD3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void **)&pDXGIDevice);
	if (SUCCEEDED(hr))
	{
		IDXGIAdapter * pDXGIAdapter = 0;
		hr = pDXGIDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&pDXGIAdapter);
		pDXGIDevice->Release();
		if (SUCCEEDED(hr))
		{
			IDXGIFactory * pIDXGIFactory = 0;
			hr = pDXGIAdapter->GetParent(__uuidof(IDXGIFactory), (void **)&pIDXGIFactory);
			pDXGIAdapter->Release();
			if (SUCCEEDED(hr))
			{
				hr = pIDXGIFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
				pIDXGIFactory->Release();
			}
		}
	}

	return hr;
}

bool wsi::hresult_succeeded(HRESULT hresult, const char *message, const char *file, int line)
{
	if(!SUCCEEDED(hresult)) {
		_com_error err(hresult);
		wsi::log_message(file, line, "%s\n\tHRESULT Failure %08x: %s", message, hresult, err.ErrorMessage());
		return false;
	}
	return true;
}

void wsi::queue::init(void *mem, int q_element_size, int q_diameter)
{
	this->element_size = q_element_size;
	this->diameter = q_diameter;
	this->read_index = this->write_index = 0;

	if (mem)
	{
		buffer = mem;
		user_mem = true;
	}
	else
	{
		buffer = calloc(diameter, element_size);
		user_mem = false;
	}
}

wsi::queue::~queue()
{
	if (!user_mem)
	{
		free(buffer);
	}
}

void *wsi::queue::read()
{
	if (size() > 0)
	{
		void *src = element(read_index);
		increment(&read_index);
		return src;
	}
	return 0;
}

void *wsi::queue::write()
{
	if (size() < diameter - 1)
	{
		void *dest = element(write_index);
		increment(&write_index);
		return dest;
	}
	return 0;
}

void wsi::queue::clear(bool dealloc)
{
	if (dealloc)
	{
		free(buffer);
		memset(this, 0, sizeof(queue));
	}
	else
	{
		read_index = write_index = 0;
	}
}

int wsi::queue::size()
{
	int d = write_index - read_index;
	return d >= 0 ? d : diameter + d;
}

void wsi::queue::increment(int *index)
{
	int j = *index + 1;
	*index = j >= diameter ? 0 : j;
}

void *wsi::queue::element(int index)
{
	return (char*)buffer + index*element_size;
}
