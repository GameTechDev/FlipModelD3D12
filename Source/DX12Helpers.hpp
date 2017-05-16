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
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <d3d11on12.h>
#include <d2d1_3.h>
#include <vector>

#if D3D12_DYNAMIC_LINK

struct D3D12DllExports
{
	BOOL loaded;
	PFN_D3D12_CREATE_DEVICE CreateDevice;
	PFN_D3D12_GET_DEBUG_INTERFACE GetDebugInterface;
	PFN_D3D12_SERIALIZE_ROOT_SIGNATURE SerializeRootSignature;
	PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER CreateRootSignatureDeserializer;
};

extern D3D12DllExports D3D12;
BOOL LoadD3D12Dll(BOOL Load);

#define D3D12CreateDevice D3D12.CreateDevice
#define D3D12GetDebugInterface D3D12.GetDebugInterface
#define D3D12SerializeRootSignature D3D12.SerializeRootSignature
#define D3D12CreateRootSignatureDeserializer D3D12.CreateRootSignatureDeserializer

#endif // D3D12_DYNAMIC_LINK

inline UINT AlignTo(UINT offset, UINT align)
{
	assert(!(align & (align-1)));
	return (offset + (align-1)) &~ (align-1);
}

template<class T, class S>
inline D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress(D3D12_GPU_VIRTUAL_ADDRESS base, T* member, S* structure)
{
	return base + (reinterpret_cast<char*>(member) - reinterpret_cast<char*>(structure));
}

template<class T, class S>
inline D3D12_VERTEX_BUFFER_VIEW MakeVertexBufferView(D3D12_GPU_VIRTUAL_ADDRESS base, T* member, UINT size, S* structure)
{
	D3D12_VERTEX_BUFFER_VIEW view;
	view.BufferLocation = GetGpuAddress(base, member, structure);
	view.SizeInBytes = size;
	view.StrideInBytes = sizeof(T);
	return view;
}

template<class T, class S>
inline D3D12_INDEX_BUFFER_VIEW MakeIndexBufferView(D3D12_GPU_VIRTUAL_ADDRESS base, T* member, UINT size, S* structure, DXGI_FORMAT format)
{
	D3D12_INDEX_BUFFER_VIEW view;
	view.BufferLocation = GetGpuAddress(base, member, structure);
	view.SizeInBytes = size;
	view.Format = format;
	return view;
}

void SetNameV(ID3D12Object *obj, const char *fmt, va_list args);

static void SetName(ID3D12Object *obj, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	SetNameV(obj, fmt, args);
	va_end(args);
}

template<class T>
static void SetName(ComPtr<T>& obj, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	SetNameV(obj.Get(), fmt, args);
	va_end(args);
}

static void SetName(IDXGIObject *obj, const char *name)
{
	obj->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
}

UINT64 WaitForFence(ID3D12Fence *Fence, HANDLE FenceEvent, UINT64 WaitValue);

// Upload Heap: Untyped version
struct UploadHeap
{
	HRESULT Initialize(ID3D12Device* device, UINT64 size);

	ID3D12Resource* Heap() { return mHeap.Get(); }

	// Write-only!
	void* DataWO() { return mHeapWO; }

private:
	ComPtr<ID3D12Resource> mHeap;
	void* mHeapWO = nullptr;
};

// Upload Heap: Structure/typed version
template <typename T>
struct UploadHeapT: UploadHeap
{
	HRESULT Initialize(ID3D12Device *device)
	{
		return UploadHeap::Initialize(device, sizeof(T));
	}

	T* DataWO() { return (T*)UploadHeap::DataWO(); }
};

// Untyped version
struct DescriptorArray
{
	struct IndexValue {
		const D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle;
		const D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle;
	};
	UINT Size() const { return mArraySize; }
	IndexValue operator[](UINT i) {
		assert(i >= 0 && i < mArraySize);
		return { CpuHandle(i), GpuHandle(i) };
	}
	D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(UINT i) {
		auto h = mCpuBase;
		h.ptr += i*mElementSize;
		return h;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(UINT i) {
		auto h = mGpuBase;
		h.ptr += i*mElementSize;
		return h;
	}
	ID3D12DescriptorHeap *GetHeap() { return mHeap.Get(); }

protected:
	void Initialize(ID3D12Device *Device, D3D12_DESCRIPTOR_HEAP_TYPE HeapType,
		D3D12_DESCRIPTOR_HEAP_FLAGS HeapFlags, UINT Size);

private:
	ComPtr<ID3D12DescriptorHeap> mHeap;
	UINT mArraySize;
	UINT mElementSize;
	D3D12_CPU_DESCRIPTOR_HANDLE mCpuBase;
	D3D12_GPU_DESCRIPTOR_HANDLE mGpuBase;
};

template<D3D12_DESCRIPTOR_HEAP_TYPE HeapType>
struct DescriptorArrayT : DescriptorArray
{
	void Initialize(ID3D12Device *Device, D3D12_DESCRIPTOR_HEAP_FLAGS HeapFlags, UINT Size)
	{
		DescriptorArray::Initialize(Device, HeapType, HeapFlags, Size);
	}
};

/* How to use FrameQueue:

init:

	FrameQueue frameQueue;
	MyFrameData myFrames[2];
	frameQueue.Initialize(..., &frames, sizeof(MyFrameData), ARRAY_SIZE(myFrames));

render:

	FrameContext *Frame;
	frameQueue.BeginFrame(&frame);
	RenderFrame(Frame, FrameData);
	frameQueue.EndFrame(Frame);
*/

struct FrameQueue
{
	bool Initialize(
		const char *DebugName,
		ID3D12Device *Device, ID3D12CommandQueue *CommandQueue,
		ID3D12PipelineState *InitialPipelineState,
		void *FrameUserData, UINT SizeofStructFrame, UINT FrameCount,
		ID3D11On12Device *Device11On12 = nullptr,
		ID2D1DeviceContext2 *D2DDeviceContext = nullptr);

	bool SetSwapChain(IDXGISwapChain2 *SwapChain, 
		DXGI_FORMAT RenderTargetViewFormat = DXGI_FORMAT_UNKNOWN,
		float dpiX = 0,
		float dpiY = 0);

	struct FrameContext
	{
		friend struct FrameQueue;
		ID3D12Resource *mBackBuffer;
		ID3D11Resource *mWrapped11BackBuffer;
		ID2D1Bitmap1 *mD2DRenderTarget;
		D3D12_CPU_DESCRIPTOR_HANDLE mBackBufferRTV;
		ComPtr<ID3D12CommandAllocator> mCommandAllocator;
		UINT mBackBufferIndex;
		template<class T> T *CastUserDataAs() {
			return static_cast<T*>(mUserData);
		}
		void FlushCommandList() {

		}
	private:
		bool mCommandListOpen;
		void *mUserData;
		UINT64 mFrameFenceId;
	};

	// Handle fencing
	void BeginFrame(FrameContext **Frame);
	void EndFrame(FrameContext *Frame);

	FrameContext *GetFrameContext(int i) {
		return &mFrames[i];
	}

private:
	const char *mDebugName;
	ComPtr<ID3D12Device> mDevice;
	ComPtr<ID3D11On12Device> mDevice11On12;
	ComPtr<ID2D1DeviceContext2> mD2DDeviceContext;
	ComPtr<IDXGISwapChain3> mSwapChain;
	ComPtr<ID3D12CommandQueue> mCommandQueue;

	UINT64 mNextFrameFence;
	ComPtr<ID3D12Fence> mFence;
	WindowsEvent mFenceEvent;

	UINT mNextFrameIndex;
	std::vector<FrameContext> mFrames;

	struct BackBufferResources {
		ComPtr<ID3D12Resource> mBuffer;
		ComPtr<ID3D11Resource> mWrapped11Buffer;
		ComPtr<ID2D1Bitmap1> mD2DRenderTarget;
	};

	std::vector<BackBufferResources> mBackBuffers;
	DescriptorArrayT<D3D12_DESCRIPTOR_HEAP_TYPE_RTV> mRenderTargetViews;
};

struct TimestampQueryHeap
{
	bool Initialize(ID3D12Device *Device, UINT TimestampCount)
	{
		D3D12_QUERY_HEAP_DESC QueryHeapDesc;
		QueryHeapDesc.Count = TimestampCount;
		QueryHeapDesc.NodeMask = 0;
		QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		CheckHresult(Device->CreateQueryHeap(&QueryHeapDesc, IID_PPV_ARGS(&mQueryHeap)));

		UINT BufferSize = sizeof(UINT64)*TimestampCount;
		D3D12_RESOURCE_DESC ReadbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);
		D3D12_HEAP_PROPERTIES ReadbackBufferHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
		CheckHresult(Device->CreateCommittedResource(&ReadbackBufferHeapProps, D3D12_HEAP_FLAG_NONE,
			&ReadbackBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&mReadbackBuffer)));

		return true;
	}

	void QueryTimestampCommand(ID3D12GraphicsCommandList *CommandList, UINT TimestampIndex)
	{
		CommandList->EndQuery(mQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, TimestampIndex);
	}

	void ReadTimestampQueryCommand(ID3D12GraphicsCommandList *CommandList, UINT StartIndex, UINT NumQueries)
	{
		CommandList->ResolveQueryData(mQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
			StartIndex, NumQueries, mReadbackBuffer.Get(), StartIndex*sizeof(UINT64));
	}

	void Readback(void *Dest, UINT Size)
	{
		void *Source;
		D3D12_RANGE Range;
		Range.Begin = 0;
		Range.End = Size;
		ThrowIfFailed(mReadbackBuffer->Map(0, &Range, &Source));
		memcpy(Dest, Source, Size);
		mReadbackBuffer->Unmap(0, NULL);
	}

private:

	ComPtr<ID3D12QueryHeap> mQueryHeap;
	ComPtr<ID3D12Resource> mReadbackBuffer;
};

template<class FrameTimestamps>
struct TimestampQueryHeapT
{
	enum { NumberOfTimestampsPerStruct = sizeof(FrameTimestamps) / sizeof(UINT64) };

	struct QueryScope {
		QueryScope(TimestampQueryHeapT *Parent,
			ID3D12GraphicsCommandList *CommandList,
			const UINT64(&Timestamps)[2])
			: mParent(Parent)
			, mCommandList(CommandList)
			, mTimestamps(Timestamps)
		{
			mParent->QueryTimestampCommand(mCommandList, &mTimestamps[0]);
		}

		~QueryScope()
		{
			mParent->QueryTimestampCommand(mCommandList, &mTimestamps[1]);
		}

		TimestampQueryHeapT *mParent;
		ID3D12GraphicsCommandList *mCommandList;
		const FrameTimestamps *mTimestampStruct;
		const UINT64(&mTimestamps)[2];
	};

	bool Initialize(ID3D12Device *Device)
	{
		return mQueryHeap.Initialize(Device, NumberOfTimestampsPerStruct);
	}

	const FrameTimestamps *GetTimestampWriteStruct()
	{
		return 0;
	}

	const FrameTimestamps *GetTimestampReadStruct(bool refresh)
	{
		if (refresh) {
			mQueryHeap.Readback(&mLastReadback, sizeof(FrameTimestamps));
		}
		return &mLastReadback;
	}

	void QueryTimestampCommand(ID3D12GraphicsCommandList *CommandList, const UINT64 *Timestamp)
	{
		UINT TimestampIndex = (UINT)(Timestamp - (UINT64*)0);
		assert(TimestampIndex*sizeof(UINT64) <= sizeof(FrameTimestamps));
		mQueryHeap.QueryTimestampCommand(CommandList, TimestampIndex);
	}

	void ReadbackCommand(ID3D12GraphicsCommandList *CommandList)
	{
		mQueryHeap.ReadTimestampQueryCommand(CommandList, 0, NumberOfTimestampsPerStruct);
	}

private:
	TimestampQueryHeap mQueryHeap;
	FrameTimestamps mLastReadback;
};
