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

#include "DX12Helpers.hpp"

#if D3D12_DYNAMIC_LINK

HMODULE hModuleD3D12;
D3D12DllExports D3D12;
BOOL LoadD3D12Dll(BOOL Load)
{
	// Already in desired state
	if(D3D12.loaded == Load)
	{
		return TRUE;
	}

	// Unloading
	if(!Load)
	{
		assert(hModuleD3D12);
		FreeLibrary(hModuleD3D12);
		ZeroMemory(&D3D12, sizeof(D3D12));
		return TRUE;
	}

	// Loading
	hModuleD3D12 = LoadLibrary("D3D12");
	if(!hModuleD3D12)
	{
		return FALSE;
	}

	*(FARPROC*)&D3D12.CreateDevice = GetProcAddress(hModuleD3D12, "D3D12CreateDevice");
	*(FARPROC*)&D3D12.GetDebugInterface = GetProcAddress(hModuleD3D12, "D3D12GetDebugInterface");
	*(FARPROC*)&D3D12.SerializeRootSignature = GetProcAddress(hModuleD3D12, "D3D12SerializeRootSignature");
	*(FARPROC*)&D3D12.CreateRootSignatureDeserializer = GetProcAddress(hModuleD3D12, "D3D12CreateRootSignatureDeserializer");

	if(!D3D12.CreateDevice ||
		!D3D12.GetDebugInterface ||
		!D3D12.SerializeRootSignature ||
		!D3D12.CreateRootSignatureDeserializer)
	{
		FreeLibrary(hModuleD3D12);
		ZeroMemory(&D3D12, sizeof(D3D12));
		return FALSE;
	}

	D3D12.loaded = TRUE;

	return TRUE;
}

#endif // #if D3D12_DYNAMIC_LINK

void SetNameV(ID3D12Object *obj, const char *fmt, va_list args)
{
	char buf[256];
	int len = vsnprintf(buf, sizeof(buf), fmt, args);
	std::wstring temp(buf, buf+len);
	obj->SetName(temp.c_str());
}

UINT64 WaitForFence(ID3D12Fence *Fence, HANDLE FenceEvent, UINT64 WaitValue)
{
	UINT64 CompletedValue;
	while((CompletedValue = Fence->GetCompletedValue()) < WaitValue)
	{
		if(FenceEvent)
		{
			WaitForSingleObject(FenceEvent, INFINITE);
		}
		else
		{
			Sleep(1);
		}
	}
	return CompletedValue;
}

HRESULT UploadHeap::Initialize(ID3D12Device* device, UINT64 size)
{
	this->~UploadHeap();

	HRESULT hr;

	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(size),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mHeap));
	if (FAILED(hr)) {
		return hr;
	}

	hr = mHeap->Map(0, nullptr, reinterpret_cast<void**>(&mHeapWO));

	return hr;
}

void DescriptorArray::Initialize(ID3D12Device *Device, D3D12_DESCRIPTOR_HEAP_TYPE HeapType,
	D3D12_DESCRIPTOR_HEAP_FLAGS HeapFlags, UINT Size)
{
	this->~DescriptorArray();

	mArraySize = Size;
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc;
	ZeroMemory(&HeapDesc, sizeof(HeapDesc));
	HeapDesc.NumDescriptors = Size;
	HeapDesc.Type = HeapType;
	HeapDesc.Flags = HeapFlags;
	Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&mHeap));
	mElementSize = Device->GetDescriptorHandleIncrementSize(HeapType);
	mCpuBase = mHeap->GetCPUDescriptorHandleForHeapStart();
	mGpuBase = mHeap->GetGPUDescriptorHandleForHeapStart();
}

bool FrameQueue::Initialize(
	const char *DebugName,
	ID3D12Device *Device, ID3D12CommandQueue *CommandQueue,
	ID3D12PipelineState *InitialPipelineState,
	void *FrameUserData, UINT SizeofStructFrame, UINT FrameCount,
	ID3D11On12Device *Device11On12,
	ID2D1DeviceContext2 *D2DDeviceContext)
{
	this->~FrameQueue();

	mNextFrameFence = 1;
	mNextFrameIndex = 0;

	mDebugName = DebugName;
	mDevice = Device;
	mDevice11On12 = Device11On12;
	mD2DDeviceContext = D2DDeviceContext;
	mCommandQueue = CommandQueue;

	CheckHresult(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
	mFenceEvent.Initialize();

	mFrames.resize(FrameCount);
	for(UINT i = 0; i < FrameCount; ++i)
	{
		FrameContext *Frame = &mFrames[i];
		Frame->mFrameFenceId = 0;
		CheckHresult(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&Frame->mCommandAllocator)));
		SetName(Frame->mCommandAllocator, "%s.Frame%d:CmdAlloc", DebugName, i);
		Frame->mUserData = (char*)FrameUserData + i*SizeofStructFrame;
	}

	return true;
}

bool FrameQueue::SetSwapChain(
	IDXGISwapChain2 *SwapChain,
	DXGI_FORMAT RenderTargetViewFormat,
	float dpiX, float dpiY)
{
	if (!SwapChain) {
		mBackBuffers.clear();
		return true;
	}

	DXGI_SWAP_CHAIN_DESC1 Desc1;
	CheckHresult(SwapChain->GetDesc1(&Desc1));
	UINT ChainLength = Desc1.BufferCount;
	assert(ChainLength);

	CheckHresult(SwapChain->QueryInterface(mSwapChain.ReleaseAndGetAddressOf()));
	mBackBuffers.resize(ChainLength);
	mRenderTargetViews.Initialize(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_FLAG_NONE, ChainLength);
	D3D12_RENDER_TARGET_VIEW_DESC RtvDesc = {};
	RtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	RtvDesc.Format = RenderTargetViewFormat == DXGI_FORMAT_UNKNOWN ? Desc1.Format : RenderTargetViewFormat;

	D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
		dpiX,
		dpiY
		);

	for(UINT i = 0; i < ChainLength; ++i)
	{
		auto& BufferResources = mBackBuffers[i];

		CheckHresult(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&BufferResources.mBuffer)));
		SetName(BufferResources.mBuffer.Get(), "%s.BackBuffer%d", mDebugName, i);
		mDevice->CreateRenderTargetView(BufferResources.mBuffer.Get(), &RtvDesc, mRenderTargetViews[i].CpuHandle);
		if (mDevice11On12)
		{
			D3D11_RESOURCE_FLAGS d3d11Flags = { D3D11_BIND_RENDER_TARGET };
			CheckHresult(mDevice11On12->CreateWrappedResource(
				BufferResources.mBuffer.Get(), &d3d11Flags, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT,
				IID_PPV_ARGS(&BufferResources.mWrapped11Buffer)));
			ComPtr<IDXGISurface> surface;
			CheckHresult(BufferResources.mWrapped11Buffer.As(&surface));
			CheckHresult(mD2DDeviceContext->CreateBitmapFromDxgiSurface(
				surface.Get(),
				&bitmapProperties,
				&BufferResources.mD2DRenderTarget
				));
		}
	}

	return true;
}

void FrameQueue::BeginFrame(FrameContext **OutFrame)
{
	assert(mSwapChain);

	// Get/Increment the fence counter
	UINT64 FrameFence = mNextFrameFence;
	mNextFrameFence = mNextFrameFence + 1;

	// Get/Increment the frame ring-buffer index
	UINT FrameIndex = mNextFrameIndex;
	mNextFrameIndex = (mNextFrameIndex + 1) % (UINT)mFrames.size();

	// Wait for the last frame occupying this slot to be complete
	FrameContext *Frame = &mFrames[FrameIndex];
	WaitForFence(mFence.Get(), mFenceEvent.Get(), Frame->mFrameFenceId);
	Frame->mFrameFenceId = FrameFence;

	// Associate the frame with the swap chain backbuffer & RTV.
	UINT BackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();
	auto& Resources = mBackBuffers[BackBufferIndex];
	Frame->mBackBufferIndex = BackBufferIndex;
	Frame->mBackBuffer = Resources.mBuffer.Get();
	Frame->mWrapped11BackBuffer = Resources.mWrapped11Buffer.Get();
	Frame->mD2DRenderTarget = Resources.mD2DRenderTarget.Get();
	Frame->mBackBufferRTV = mRenderTargetViews[BackBufferIndex].CpuHandle;

	// Reset the command allocator and list
	ThrowIfFailed(Frame->mCommandAllocator->Reset());

	*OutFrame = Frame;
}

void FrameQueue::EndFrame(FrameContext *Frame)
{
	// Signal that the frame is complete
	ThrowIfFailed(mFence->SetEventOnCompletion(Frame->mFrameFenceId, mFenceEvent.Get()));
	ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), Frame->mFrameFenceId));
}