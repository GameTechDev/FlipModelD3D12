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
#include "WindowsHelpers.hpp"

#include <comdef.h>
#include <cstring>

#if USING_WSI
#include "wsi.hpp"
#endif

bool hresult_succeeded(HRESULT hresult, const char *message, const char *file, int line)
{
	if (!SUCCEEDED(hresult)) {
		_com_error err(hresult, nullptr);
		char buf[1024];
#ifdef UNICODE
		sprintf_s(buf, "%s\n\tHRESULT Failure %08x: %ws", message, hresult, err.ErrorMessage());
#else
		sprintf_s(buf, "%s\n\tHRESULT Failure %08x: %s", message, hresult, err.ErrorMessage());
#endif
		OutputDebugStringA(buf);
		return false;
	}
	return true;
}

static UINT64 GetQpcFreq()
{
	UINT64 QpcFreq;
	QueryPerformanceFrequency((LARGE_INTEGER*)&QpcFreq);
	return QpcFreq;
}

UINT64 g_QpcFreq = GetQpcFreq();

UINT64 SecondsToQpcTime(double Seconds)
{
	return (UINT64)(g_QpcFreq*Seconds);
}

double QpcTimeToSeconds(UINT64 QpcTime)
{
	return (double)QpcTime / g_QpcFreq;
}

UINT64 QpcNow()
{
	UINT64 Now;
	QueryPerformanceCounter((LARGE_INTEGER*)&Now);
	return Now;
}

void SleepUntil(UINT64 QpcTime)
{
	UINT64 Now = QpcNow();
	if (QpcTime > Now) {
		UINT64 Duration = QpcTime - Now;
		UINT Millis = UINT(1000 * Duration / g_QpcFreq);
		if (Millis > 0) {
			Sleep(Millis);
		}
	}
}
