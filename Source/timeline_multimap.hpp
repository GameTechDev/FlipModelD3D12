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

#include <memory>

// Timeline_multimap:
// A multimap that represents a timeline of events.
// The assumption is that events are inserted "roughly" in chronological order, and in batches.
// Under these conditions, the performance will be much better than a regular multimap.
// In order to achieve this extra performance, we accept certain tradeoffs:
// -Improve average case operations at the cost of worst case.
// -Sacrifice stable iterators (pretty much any operation invalidates).
// -Sacrifice stable references (values are not allocated on the heap).
template<
	typename K, // the time index, should be integral
	typename V> // typically std::unique_ptr<some struct>
struct timeline_multimap
{
	struct value_type
	{
		K first;
		V second;
		template<class... Args> value_type(K k, Args&&... args) 
			: first(k), second(std::forward<Args>(args)...) { }
		friend bool operator < (const value_type& lhs, const value_type& rhs) {
			return lhs.first < rhs.first;
		}
		friend bool operator < (const value_type& lhs, const K& rhs) {
			return lhs.first < rhs;
		}
		friend bool operator < (const K& lhs, const value_type& rhs) {
			return lhs < rhs.first;
		}
	};

	typedef typename std::vector<value_type>::iterator iterator;

	bool empty()
	{
		return vals.empty();
	}

	size_t size()
	{
		return vals.size();
	}

	void clear()
	{
		vals.clear();
		unsorted_begin = 0;
	}

	template<class... Args>
	iterator emplace(K k, Args&&... args)
	{
		auto pos = vals.size();
		vals.emplace_back(k, std::forward<Args>(args)...);
		return vals.begin() + pos;
	}

	iterator lower_bound(const K& k)
	{
		sort();
		return std::lower_bound(vals.begin(), vals.end(), k);
	}

	iterator upper_bound(const K& k)
	{
		sort();
		return std::upper_bound(vals.begin(), vals.end(), k);
	}

	iterator begin()
	{
		sort();
		return vals.begin();
	}

	iterator end()
	{
		sort();
		return vals.end();
	}

	iterator erase(iterator first, iterator last) {
		assert(size() == unsorted_begin);
		auto it = vals.erase(first, last);
		unsorted_begin = size();
		return it;
	}

	iterator erase(iterator position) {
		assert(size() == unsorted_begin);
		auto it = vals.erase(position);
		unsorted_begin = size();
		return it;
	}

private:

	void sort()
	{
		auto N = size();
		if (unsorted_begin != N)
		{
			auto first = &vals[0];
			auto mid = first + unsorted_begin;
			auto last = first + N;
			unsorted_begin = N;
			std::sort(mid, last);
			auto merge_start = std::upper_bound(first, mid, *mid);
			std::inplace_merge(merge_start, mid, last);
		}
	}

	std::vector<value_type> vals;
	size_t unsorted_begin = 0; // == size() when sorted
};
