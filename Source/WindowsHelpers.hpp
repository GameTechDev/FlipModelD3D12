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
#include <wrl.h>
#include <cassert>

using Microsoft::WRL::ComPtr;

bool hresult_succeeded(HRESULT hresult, const char *message, const char *file, int line);

#define ThrowIfFailed(...) \
	if(!hresult_succeeded(__VA_ARGS__, #__VA_ARGS__, __FILE__, __LINE__)) { \
		assert(false); \
		throw; \
	}

#define CheckHresult(...) \
	if(!hresult_succeeded(__VA_ARGS__, #__VA_ARGS__, __FILE__, __LINE__)) { \
		return false; \
	}

#define AssertSucceeded(...) \
	if(!hresult_succeeded(__VA_ARGS__, #__VA_ARGS__, __FILE__, __LINE__)) { \
		assert(false); \
	}

struct WindowsEvent
{
	WindowsEvent(): mEvent(0) {
	}
	~WindowsEvent() {
		if(mEvent) {
			CloseHandle(mEvent);
			mEvent = 0;
		}
	}
	void Initialize() {
		mEvent = CreateEvent(0, 0, 0, 0);
	}
	WindowsEvent& operator = (HANDLE Event) {
		if(mEvent) {
			assert(Event != mEvent);
			CloseHandle(mEvent);
		}
		mEvent = Event;
		return *this;
	}
	operator bool() { return mEvent != 0; }
	HANDLE Get() { return mEvent; }
	HANDLE mEvent;
};

extern UINT64 g_QpcFreq;

UINT64 SecondsToQpcTime(double Seconds);
double QpcTimeToSeconds(UINT64 QpcTime);
UINT64 QpcNow();
void SleepUntil(UINT64 QpcTime);
