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
#include "EventViz.hpp"
#include <algorithm>
#include <map>

namespace EventViz { 

const char *kVsyncQueue = "Vsync";
const char *kPresentQueue = "Present";
const char *kGpuQueue = "GPU";
const char *kCpuQueue = "CPU";

template<class T>
inline bool IntervalsIntersect(
	T a, T b, T c, T d)
{
	assert(a <= b);
	assert(c <= d);

	// ([a,b] & [c,d]) not empty
	return !(b < c || a > d);
}

EventStream::EventStream()
{

}

EventStream::~EventStream()
{

}

void EventStream::Trim(UINT64 MaxStartTime)
{
	// Trim Vsyncs
	{
		auto Pred = [=](const EventData *e, const UINT64 Val) { return e->Start < Val; };
		auto Begin = Vsyncs.begin();
		auto End = std::lower_bound(Begin, Vsyncs.end(), MaxStartTime, Pred); // first element NOT < MaxStartTime
		Vsyncs.erase(Begin, End); // erase [Begin, End)
	}

	// Trim Events
	{
		auto Begin = AllEvents.begin();
		auto End = AllEvents.lower_bound(MaxStartTime); // first element NOT < MaxStartTime
		AllEvents.erase(Begin, End); // erase [Begin, End)
	}
}

void EventStream::TrimToLastNSeconds(UINT Seconds)
{
	UINT64 MaxStartTime = QpcNow() - Seconds*g_QpcFreq;
	Trim(MaxStartTime);
}

void EventStream::TrimToLastNVsyncs(UINT N)
{
	UINT VsyncCount = GetVsyncCount();
	if (VsyncCount <= N) {
		return;
	}
	Trim(Vsyncs[VsyncCount - N]->Start);
}

EventData *EventStream::Start(const char *Queue, const void *UserData, UINT64 UserID, UINT64 Time)
{
	if (paused) return NULL;

	assert(Queue);

	Time = Time ? Time : QpcNow();

	auto Event = new EventData{ Queue, UserData, UserID, Time, 0 };
	AllEvents.emplace(Time, Event);

	return Event;
}

void EventStream::End(EventData *Data, UINT64 Time)
{
	if (!Data || paused) return;

	Data->End = Time ? Time : QpcNow();
}

void EventStream::InsertEvent(const char *Queue, UINT64 StartTime, UINT64 EndTime, const void *UserData, UINT64 UserID)
{
	if (paused) return;

	assert(StartTime && EndTime >= StartTime);

	auto Event = new EventData{ Queue, UserData, UserID, StartTime, EndTime };
	AllEvents.emplace(StartTime, Event);
}

void EventStream::Vsync(UINT64 Time)
{
	if (paused) return;

	auto Event = EventStream::Start(kVsyncQueue, 0, 0, Time);
	EventStream::End(Event, Time);
	Vsyncs.push_back(Event);
}

UINT EventStream::GetVsyncCount()
{
	return (UINT)Vsyncs.size();
}



/// ----------------------------------------------------------------------
///                             Drawing code
/// ----------------------------------------------------------------------

enum {
	kQueueLineHeight = 33,
	kPaddingPixels = 33,
};

void JoinTwoRectangles(
	std::vector<EventViz::Line>& Lines,
	const EventViz::Rectangle& R1,
	const EventViz::Rectangle& R2)
{
	float CX0 = (R1.Left + R1.Right) / 2;
	float CY0 = (R1.Top + R1.Bottom) / 2;
	float CX1 = (R2.Left + R2.Right) / 2;
	float CY1 = (R2.Top + R2.Bottom) / 2;

	if (1)
	{
		// Direct line between rectangle centers
		Lines.push_back({ CX0, CY0, CX1, CY1 });
	}
	else
	{
		// Z-shaped line with 3 segments
		float CY = (CY0 + CY1) / 2;
		Lines.push_back({ CX0, CY0, CX0, CY  });
		Lines.push_back({ CX0, CY,  CX1, CY  });
		Lines.push_back({ CX1, CY,  CX1, CY1 });
	}
}

void ConnectTheDots(
	EventViz::EventVisualization& Visualization,
	size_t Begin1, size_t End1,
	size_t Begin2, size_t End2,
	bool PrimaryOnly)
{
	auto Rectangles = Visualization.Rectangles.data();
	for (size_t i = Begin1; i < End1; ++i) {
		for (size_t j = std::max(i, Begin2); j < End2; ++j) {
			auto& R1 = Rectangles[i];
			auto& R2 = Rectangles[j];
			if (R1.Event->UserID == R2.Event->UserID &&
				(!PrimaryOnly || (R1.Flags & R2.Flags & Rectangle::Primary)) &&
				(!(R1.Sequence | R2.Sequence) || abs(R1.Sequence-R2.Sequence) == 1))
			{
				// Create lines between the two primary rectangles for this event
				JoinTwoRectangles(Visualization.Lines, R1, R2);
			}
		}
	}
}

void LayoutLinearQueue(std::vector<EventViz::Rectangle>& Rectangles,
	EventViz::EventSet Events, UINT64 StartTime, UINT64 EndTime, float TimeToPixels, FloatRect Rect)
{
	size_t EventCount = Events.size();
	auto EventPtrs = Events.data();
	for (size_t i = 0; i < EventCount; ++i)
	{
		EventData *Event = EventPtrs[i];
		float X0 = Rect.Left + TimeToPixels * INT64(Event->Start - StartTime);
		float X1 = Rect.Left + TimeToPixels * INT64(Event->End   - StartTime);
		Rectangles.push_back( { X0, Rect.Top, X1, Rect.Bottom, Event, true } );
	}
}

void ComputeStackedQueueColumns(
	const std::vector<EventViz::EventData*>& Events, // should be sorted by start time
	const std::vector<UINT64>& VsyncTimes, // should be sorted by time
	std::vector<std::vector<EventData*>>& Stacks,
	UINT *MaxStackSize)
{
	// N * (log(K)+C)
	// Each stack is in chronological (start time) order

	// Scatter each event into every interval that it intersects

	*MaxStackSize = 0;

	if (VsyncTimes.size() < 2) {
		return;
	}

	int K = (int)VsyncTimes.size();
	auto Vsyncs = VsyncTimes.data();

	Stacks.resize(K-1);

	UINT BiggestStack = 0;

	int EventCount = (int)Events.size();
	for (int i = 0; i < EventCount; ++i)
	{
		auto Event = Events[i];
		auto StartTime = Event->Start;
		auto EndTime = Event->End == UINT64_MAX ? Event->Start : Event->End;
		auto FirstVsyncPtr = std::lower_bound(Vsyncs, Vsyncs + K, StartTime); // First Vsync that is >= StartTime
		auto LastVsyncPtr = std::upper_bound(Vsyncs, Vsyncs + K, EndTime); // First Vysnc that is > EndTime
		
		int FirstVsyncIndex = std::max(0, int(FirstVsyncPtr - Vsyncs - 1));
		int LastVsyncIndex = std::min(K-1, int(LastVsyncPtr - Vsyncs));

		for (int i = FirstVsyncIndex; i < LastVsyncIndex; ++i)
		{
			if (Vsyncs[i] >= EndTime) {
				continue;
			}
			assert(IntervalsIntersect(StartTime, EndTime, Vsyncs[i], Vsyncs[i + 1]));
			Stacks[i].push_back(Event);
			UINT StackSize = (UINT)Stacks[i].size();
			BiggestStack = std::max(BiggestStack, StackSize);
		}
	}

	*MaxStackSize = BiggestStack;
}

void LayoutStackedQueue(
	std::map<UINT64, int>& SequenceCounter,
	std::vector<EventViz::Rectangle>& Rectangles,
	const std::vector<std::vector<EventData*>>& Stacks,
	const std::vector<UINT64>& VsyncTimes,
	UINT64 StartTime, UINT64 EndTime,
	float TimeToPixels, FloatRect Rect, UINT Rows)
{
	// for each interval:
	// find all entries overlapping the interval
	// draw them from bottom to top, least recent to most recent

	size_t K = Stacks.size();
	if (!K) return;

	assert(VsyncTimes.size() == K + 1);

	for (size_t i = 0; i < K; ++i)
	{
		auto& Stack = Stacks[i];
		// from bottom to top
		UINT64 IntervalStart = VsyncTimes[i];
		UINT64 IntervalEnd = VsyncTimes[i + 1];
		UINT StackSize = (UINT)Stack.size();
		UINT Lift = Rows - StackSize;
		for (UINT j = 0; j < StackSize; ++j) {
			// The END of the stack contains the most recent item, which needs to go at the bottom.
			auto Event = Stack[StackSize-1-j];
			auto BlockStart = std::max(Event->Start, IntervalStart);
			auto BlockEnd = std::min(Event->End, IntervalEnd);
			float Y0 = Rect.Bottom-(j+1+Lift)*kQueueLineHeight;
			float Y1 = Rect.Bottom -(j+Lift)*kQueueLineHeight;
			float X0 = Rect.Left + TimeToPixels * INT64(BlockStart - StartTime);
			float X1 = Rect.Left + TimeToPixels * INT64(BlockEnd - StartTime);
			int Flags = 0;

			auto& SequenceIndex = SequenceCounter[Event->UserID];
			SequenceIndex += 1;
#if 0
			// last rectangle before presenting is Primary
			if (Event->End <= IntervalEnd && Event->End > IntervalStart) {
				Flags |= Rectangle::Primary;
			}
#else
			// first rectangle entering the queue is Primary
			if (Event->Start >= IntervalStart && Event->Start <= IntervalEnd) {
				Flags |= Rectangle::Primary;
			}
#endif
			if (Event->End == UINT64_MAX) {
				Flags |= Rectangle::Dropped;
			}
			Rectangles.push_back({ X0, Y0, X1, Y1, Event, Flags, SequenceIndex });
		}
	}
}

void LayoutDisplayRectangles(
	std::map<UINT64, int>& SequenceCounter,
	std::vector<EventViz::Rectangle>& Rectangles,
	const std::vector<std::vector<EventData*>>& PresentStacks,
	const std::vector<UINT64>& VsyncTimes,
	UINT64 StartTime, UINT64 EndTime,
	float TimeToPixels, FloatRect Rect)
{
	size_t K = PresentStacks.size();
	if (!K) return;

	assert(VsyncTimes.size() == K + 1);

	EventData *Display = 0;
	int Flags = 0;

	for (size_t i = 0; i < K; ++i)
	{
		if (Display)
		{
			UINT64 IntervalStart = VsyncTimes[i];
			UINT64 IntervalEnd = VsyncTimes[i + 1];
			float Y0 = Rect.Top;
			float Y1 = Rect.Bottom;
			float X0 = Rect.Left + TimeToPixels * INT64(IntervalStart - StartTime);
			float X1 = Rect.Left + TimeToPixels * INT64(IntervalEnd - StartTime);
			auto& SequenceIndex = SequenceCounter[Display->UserID];
			SequenceIndex += 1;
			Rectangles.push_back({ X0, Y0, X1, Y1, Display, Flags, SequenceIndex });
			Flags = 0;
		}

		auto& Stack = PresentStacks[i];
		if (!Stack.empty())
		{
			Display = Stack[0];
			Flags = Rectangle::Primary;
		}
	}
}

void CreateVisualization(EventStream& Stream, UINT FirstVsync, UINT LastVsync, FloatRect ScreenRectInDips, EventVisualization& Visualization)
{
	// We have three types of visualization:

	// Linear queues, Stacked queues, and Vsyncs.

	// The CPU & GPU queue are linear; The Present queue is stacked.
	// Vsyncs are represented as vertical lines.

	UINT VsyncCount = Stream.GetVsyncCount();
	if (Stream.AllEvents.empty() ||
		LastVsync <= FirstVsync ||
		FirstVsync >= VsyncCount ||
		LastVsync >= VsyncCount)
	{
		return;
	}

	UINT64 MaxSearchRadius = g_QpcFreq/4; // 1/4 second

	UINT64 StartTime = Stream.Vsyncs[FirstVsync]->Start;
	UINT64 EndTime = Stream.Vsyncs[LastVsync]->Start;

	auto First = Stream.AllEvents.lower_bound(StartTime - MaxSearchRadius);
	auto Last = Stream.AllEvents.upper_bound(EndTime + MaxSearchRadius);

	// FIXME: make these static?
	EventSet PresentQ, GpuQ, CpuQ;
	std::vector<UINT64> VsyncTimes;
	PresentQ.reserve(1000);
	GpuQ.reserve(1000);
	CpuQ.reserve(1000);
	for (auto it = First; it != Last; ++it)
	{
		EventData *Data = it->second.get();
		if (Data->End &&
			IntervalsIntersect(
				StartTime, EndTime,
				Data->Start, Data->End))
		{
			if (Data->Queue == kPresentQueue)
			{
				PresentQ.push_back(Data);
			}
			else if (Data->Queue == kGpuQueue)
			{
				GpuQ.push_back(Data);
			}
			else if (Data->Queue == kCpuQueue)
			{
				CpuQ.push_back(Data);
			}
			else if (Data->Queue == kVsyncQueue)
			{
				VsyncTimes.push_back(Data->Start);
			}
		}
	}

	Visualization.Lines.clear();
	Visualization.Rectangles.clear();

	// Layout from the bottom up: CpuQ, GpuQ, PresentQ
	float y = ScreenRectInDips.Bottom;
	FloatRect Rect;

	Rect.Left = ScreenRectInDips.Left + kPaddingPixels;
	Rect.Right = ScreenRectInDips.Right - kPaddingPixels;

	UINT64 TimeWidth = EndTime - StartTime + 1;
	float PixelWidth = Rect.Right - Rect.Left + 1;
	float TimeToPixels = PixelWidth / TimeWidth;

	std::map<UINT64, int> SequenceCounter;

	// CpuQ
	size_t CpuRectBegin, CpuRectEnd;
	{
		Rect.Top = y - kQueueLineHeight;
		Rect.Bottom = y;
		CpuRectBegin = Visualization.Rectangles.size();
		LayoutLinearQueue(Visualization.Rectangles, CpuQ, StartTime, EndTime, TimeToPixels, Rect);
		CpuRectEnd = Visualization.Rectangles.size();
		y -= kQueueLineHeight + kPaddingPixels;
	}

	// GpuQ
	size_t GpuRectBegin, GpuRectEnd;
	{
		Rect.Top = y - kQueueLineHeight;
		Rect.Bottom = y;
		GpuRectBegin = Visualization.Rectangles.size();
		LayoutLinearQueue(Visualization.Rectangles, GpuQ, StartTime, EndTime, TimeToPixels, Rect);
		GpuRectEnd = Visualization.Rectangles.size();
		y -= kQueueLineHeight + kPaddingPixels;
	}

	// PresentQ
	size_t PresentRectBegin, PresentRectEnd;
	std::vector<std::vector<EventData*>> PresentStacks;
	{
		UINT Rows;
		ComputeStackedQueueColumns(PresentQ, VsyncTimes, PresentStacks, &Rows);
		Rect.Top = y - Rows*kQueueLineHeight;
		Rect.Bottom = y;
		PresentRectBegin = Visualization.Rectangles.size();
		LayoutStackedQueue(SequenceCounter, Visualization.Rectangles, PresentStacks, VsyncTimes, StartTime, EndTime, TimeToPixels, Rect, Rows);
		PresentRectEnd = Visualization.Rectangles.size();
		y = Rect.Top - kPaddingPixels;
	}
	
	// DisplayQ:
	size_t DisplayRectBegin, DisplayRectEnd;
	{
		Rect.Top = y - kQueueLineHeight;
		Rect.Bottom = y;
		DisplayRectBegin = Visualization.Rectangles.size();
		LayoutDisplayRectangles(SequenceCounter, Visualization.Rectangles, PresentStacks, VsyncTimes, StartTime, EndTime, TimeToPixels, Rect);
		DisplayRectEnd = Visualization.Rectangles.size();
	}

	ConnectTheDots(Visualization, CpuRectBegin, CpuRectEnd, GpuRectBegin, GpuRectEnd, true);
	//ConnectTheDots(Visualization, GpuRectBegin, GpuRectEnd, PresentRectBegin, PresentRectEnd, true);

	ConnectTheDots(Visualization, CpuRectBegin, CpuRectEnd, PresentRectBegin, PresentRectEnd, true);
	ConnectTheDots(Visualization, PresentRectBegin, PresentRectEnd, PresentRectBegin, PresentRectEnd, false);
	ConnectTheDots(Visualization, PresentRectBegin, PresentRectEnd, DisplayRectBegin, DisplayRectEnd, false);

	// Vsyncs
	{
		float Y0 = Rect.Top;
		float Y1 = ScreenRectInDips.Bottom;
		for (size_t i = 0; i < VsyncTimes.size(); ++i)
		{
			float X0 = Rect.Left + TimeToPixels * (VsyncTimes[i] - StartTime);
			Visualization.Lines.push_back({ X0, Y0, X0, Y1 });
		}
	}
}

}
