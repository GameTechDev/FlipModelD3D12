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
#include <dxgi1_4.h>
#include <vector>
#include <cassert>
#include <algorithm>
#include <deque>

struct PresentQueueStats
{
	struct QueueEntry
	{
		UINT64 FrameBeginTime;
		UINT64 QueueEnteredTime;
		UINT64 PreRenderEstimatedSyncTime;
		UINT64 PresentTimeEstimatedSyncTime;
		UINT64 QueueExitedTime;
		void *UserData;
		UINT PresentID;
		BOOL Dropped;
	};

	PresentQueueStats()
	{
		memset(this, 0, sizeof(*this));
	}

	template<class DequeueEntry>
	HRESULT RetrieveStats(
		IDXGISwapChain1 *pSwapChain, 
		DequeueEntry dequeue)
	{
		HRESULT hr;

		DXGI_FRAME_STATISTICS stats = { 0 };
		while (SUCCEEDED(hr = pSwapChain->GetFrameStatistics(&stats)) &&
			(stats.PresentCount > LastRetrievedID))
		{
			//assert(stats.PresentCount - LastRetrievedID < MAX_QUEUE_LENGTH);

			UpdateEntry(stats);

			for (UINT i = LastRetrievedID + 1; i <= stats.PresentCount; ++i)
			{
				UINT EntryIndex = i % MAX_QUEUE_LENGTH;
				auto& Entry = Entries[EntryIndex];
				QueueEntry e = Entry;

				if (Entry.PresentID != i) {
					// We overflowed the queue
					continue;
				}

				if (Entry.QueueExitedTime < Entry.QueueEnteredTime) {
					// Something got very, very confused.
					// This seems to happen when hitting f12 to force enter the debugger.
					continue;
				}

				e.Dropped = Entry.QueueExitedTime == TIME_STILL_IN_QUEUE;
				dequeue(e);
			}

			LastRetrievedID = stats.PresentCount;
		}

		return hr;
	}

	// Call this function after you call pSwapChain->Present()
	HRESULT PostPresent(
		IDXGISwapChain1 *pSwapChain,
		UINT SyncInterval,
		UINT64 FrameBeginTime,
		void *UserData)
	{
		HRESULT hr;

		UINT64 QpcTime = QpcNow();
		UINT PresentID;

		hr = pSwapChain->GetLastPresentCount(&PresentID);
		if (FAILED(hr)) return hr;

		NewEntry(PresentID, FrameBeginTime, QpcTime, UserData);

		return hr;
	}

private:

	enum : UINT {
		MAX_QUEUE_LENGTH = 32,
		INTERVAL_DURATION_WINDOW_SIZE = 7,
	};

	enum : UINT64 {
		TIME_STILL_IN_QUEUE = ~(0ULL),
	};

	UINT LastNewID;
	UINT LastUpdatedID;
	UINT LastRetrievedID;
	QueueEntry Entries[MAX_QUEUE_LENGTH];
	UINT DurationHistoryWriteIndex;

	void NewEntry(UINT PresentID,
		UINT64 FrameBeginTime,
		UINT64 QpcTime,
		void *UserData)
	{
		UINT EntryIndex = PresentID % MAX_QUEUE_LENGTH;

		auto& Entry = Entries[EntryIndex];
		Entry.FrameBeginTime = FrameBeginTime;
		Entry.PresentID = PresentID;
		Entry.UserData = UserData;
		Entry.QueueEnteredTime = QpcTime;
		Entry.QueueExitedTime = TIME_STILL_IN_QUEUE;

		LastNewID = PresentID;
	}

	void UpdateEntry(DXGI_FRAME_STATISTICS& stats)
	{
		UINT PresentID = stats.PresentCount;
		UINT EntryIndex = PresentID % MAX_QUEUE_LENGTH;

		auto& Entry = Entries[EntryIndex];
		if (Entry.PresentID == PresentID)
		{
			Entry.QueueExitedTime = stats.SyncQPCTime.QuadPart;
			if (PresentID > 0)
			{
				UINT PreviousPresentID = PresentID - 1;
				UINT PreviousEntryIndex = PreviousPresentID % MAX_QUEUE_LENGTH;
				auto& PreviousEntry = Entries[PreviousEntryIndex];
				if (PreviousEntry.PresentID == PreviousPresentID &&
					PreviousEntry.QueueExitedTime != TIME_STILL_IN_QUEUE)
				{
					assert(Entry.QueueExitedTime >= PreviousEntry.QueueExitedTime);
				}
			}
			LastUpdatedID = PresentID;
		}
	}
};

struct LatencyStatistics
{
	void Sample(double Latency)
	{
		mLatencyHistory.push_back(Latency);
		if (mLatencyHistory.size() > mMaxHistoryLength) {
			mLatencyHistory.pop_front();
		}
	}

	void SetHistoryLength(UINT Length)
	{
		mMaxHistoryLength = Length;
	}

	// absolute temporal distortion: Max(latency) - Min(latency)
	double EvaluateMinMaxMetric()
	{
		if (mLatencyHistory.empty()) {
			return 0;
		}

		auto minmax = std::minmax_element(std::begin(mLatencyHistory), std::end(mLatencyHistory));
		return (*minmax.second) - (*minmax.first);
	}

	// std. dev of latency
	double EvaluateStdDevMetric()
	{
		if (mLatencyHistory.empty()) {
			return 0;
		}

		auto n = mLatencyHistory.size();
		double sum = 0;
		double sq_sum = 0;

		for (double L : mLatencyHistory) {
			sum += L;
			sq_sum += L*L;
		}
		
		double mean = sum / n;
		double variance = sq_sum / n - mean * mean;
		return sqrt(variance);
	}

private:

	std::deque<double> mLatencyHistory;
	UINT mMaxHistoryLength;
};
