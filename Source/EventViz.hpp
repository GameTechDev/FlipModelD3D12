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

#include "WindowsHelpers.hpp"
#include <vector>
#include <deque>
#include <memory>

#include "timeline_multimap.hpp"

namespace EventViz
{
	extern const char *kVsyncQueue;
	extern const char *kPresentQueue;
	extern const char *kGpuQueue;
	extern const char *kCpuQueue;

	struct EventData
	{
		const char *Queue;
		const void *UserData;
		UINT64 UserID;
		UINT64 Start;
		UINT64 End;
	};

	typedef timeline_multimap<UINT64, std::unique_ptr<EventData>> EventMapT;
	typedef std::vector<EventData*> EventSet;
	typedef std::pair<int, int> VectorRange;
	typedef std::vector<VectorRange> Partition;

	struct EventStream
	{
		EventStream();
		~EventStream();

		void Pause(bool paused) { this->paused = paused; }

		void Trim(UINT64 MaxStartTime);
		void TrimToLastNSeconds(UINT Seconds);
		void TrimToLastNVsyncs(UINT Vsyncs);

		EventData *Start(const char *Queue, const void *UserData = 0, UINT64 UserID = 0, UINT64 Time = 0);
		void End(EventData *Data, UINT64 Time = 0);
		void InsertEvent(const char *Queue, UINT64 StartTime, UINT64 EndTime, const void *UserData = 0, UINT64 UserID = 0);
		void Vsync(UINT64 Time = 0);

		UINT GetVsyncCount();

		EventMapT AllEvents; // keyed on/sorted by start time.
		std::deque<EventData*> Vsyncs;
		bool paused;
	};

	struct Line {
		float X0, Y0, X1, Y1;
	};

	struct FloatRect {
		float Left, Top, Right, Bottom;
	};

	struct Rectangle {
		float Left, Top, Right, Bottom;
		EventData *Event;
		int Flags;
		int Sequence;

		enum FlagBits {
			Primary = 1,
			Dropped = 2,
		};
	};

	struct EventVisualization {
		std::vector<Line> Lines;
		std::vector<Rectangle> Rectangles;
	};

	void CreateVisualization(EventStream& Stream, UINT FirstVsync,
		UINT LastVsync, FloatRect Screen, EventVisualization& Visualization);
}
