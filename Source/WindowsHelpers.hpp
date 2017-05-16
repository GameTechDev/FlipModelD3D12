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
