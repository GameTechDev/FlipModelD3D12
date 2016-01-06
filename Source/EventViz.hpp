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