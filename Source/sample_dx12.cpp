/////////////////////////////////////////////////////////////////////////////////////////////
// Copyright 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");// you may not use this file except in compliance with the License.// You may obtain a copy of the License at//// http://www.apache.org/licenses/LICENSE-2.0//// Unless required by applicable law or agreed to in writing, software// distributed under the License is distributed on an "AS IS" BASIS,// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.// See the License for the specific language governing permissions and// limitations under the License.
/////////////////////////////////////////////////////////////////////////////////////////////

#include "sample_dx12.hpp"
#include "sample_game.hpp"
#include "sample_math.hpp"
#include "sample_cube.hpp"

#define NOMINMAX
#include <Windows.h>

#include "pixel_shader.h"
#include "vertex_shader.h"

#include <array>
#include <vector>

#include "DX12Helpers.hpp"
#include <d3d11_3.h>
#include <d3d11on12.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <dxgi1_4.h>
#include <DXGIDebug.h>
#include <comdef.h>
#include <wrl.h>

#include "PresentQueueStats.hpp"
#include "EventViz.hpp"

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")

#if !D3D12_DYNAMIC_LINK
#pragma comment(lib, "d3d12.lib")
#endif

inline int ConvertDipsToPixels(float dips, float dpi)
{
	static const float dipsPerInch = 96.0f;
	return int(dips * dpi / dipsPerInch + 0.5f); // Round to nearest integer.
}

enum
{
	MAX_EVIZ_VERTS = 80 * 1024,
};

struct eventviz_aux
{
	const char *name;
	union {
		struct {
			unsigned char r, g, b, a; 
		};
		unsigned int rgba;
	};
};

enum
{
	EVENT_TYPE_PRESENT_CALL,
	EVENT_TYPE_SWAPCHAIN_WAIT,
	EVENT_TYPE_RENDER,
	EVENT_TYPE_FRAME_WAIT,
	EVENT_TYPE_COLOR0,
	EVENT_TYPE_COLOR1,
	EVENT_TYPE_COLOR2,
	EVENT_TYPE_COLOR3,
	EVENT_TYPE_COLOR4,
	EVENT_TYPE_COLOR5,
	EVENT_TYPE_COLOR6,
	EVENT_TYPE_COLOR7,

	EVENT_TYPE_GPU_CLEAR,
	EVENT_TYPE_GPU_DRAW,

	NUM_FRAME_COLORS = 8
};

static const char *EVENT_QUEUE_CPU = "CPU";
static const char *EVENT_QUEUE_PRESENT = "Present";

eventviz_aux event_types[] = {
	{"present call", 0x9A, 0x2E, 0xFE, 0xFF}, // purple
	{"swapchain wait", 0xFF, 0xFF, 0x00, 0xFF}, // yellow
	{"render", 0x00, 0xFF, 0x00, 0xFF}, // green
	{"frame wait", 0x00, 0x00, 0xFF, 0xFF}, // blue

	{ "color_0", 0xff, 0x6B, 0x6C, 0xFF }, // red
	{ "color_1", 0x18, 0xC7, 0xFC, 0xFF }, // blue
	{ "color_2", 0xF3, 0xAB, 0x00, 0xFF }, // brown
	{ "color_3", 0xB3, 0xB1, 0xFF, 0xFF }, // purple
	{ "color_4", 0x00, 0xD1, 0xA5, 0xFF }, // cyan
	{ "color_5", 0xAB, 0xC4, 0x00, 0xFF }, // olive
	{ "color_6", 0xFF, 0x93, 0xEE, 0xFF }, // pink
	{ "color_7", 0x29, 0xD4, 0x22, 0xFF }, // green
	
	{"gpu clear", 0xFF, 0x00, 0x00, 0xFF }, // red
	{"gpu draw", 0x00, 0xFF, 0x00, 0xFF }, // green
};

struct RootParameters {
	enum {
		ProjectionCbuffer,
		Flags,
		Count,
	};

	enum {
		DefaultSampler,
		StaticSamplerCount
	};
};

struct DsvDescriptors {
	enum {
		Main,
		Count
	};
};

enum
{
	PerVertexInputSlot,
	PerInstanceInputSlot
};

struct cbuffer
{
	float4x4 projection;

	cbuffer()
	{
		load_identity(&projection);
	}
};

struct instance_data
{
	float4x4 modelview;
};

struct perspective_cbuffer
{
	union {
		float4x4 projection;
		char _[256];
	};
};

struct ortho_cbuffer
{
	union {
		float4x4 projection;
		char _[256];
	};
};

struct frame_dynamic_data
{
	perspective_cbuffer perspective_cbuf;
	ortho_cbuffer ortho_cbuf;
	instance_data instances[3]; // 0 = HUD instance, 1,2 = cube instances
	color_vertex hud_vertices[4];
	color_vertex eventviz_verts[MAX_EVIZ_VERTS];
};

struct frame_timestamps_struct
{
	UINT64 clear[2];
	UINT64 draw[2];
	UINT64 draw_cubes[2];
	UINT64 draw_eviz[2];
};

typedef TimestampQueryHeapT<frame_timestamps_struct>::QueryScope gpu_timer_scope;

struct frame_data
{
	ComPtr<ID3D12GraphicsCommandList> mCommandList;

	UINT64 gpu_clock_origin;
	UINT64 cpu_clock_origin;

	TimestampQueryHeapT<frame_timestamps_struct> timestamps;
	UINT64 render_id;
	UINT backbuffer_index;

	UploadHeapT<frame_dynamic_data> dynamic;

	D3D12_GPU_VIRTUAL_ADDRESS perspective_cbuf;
	D3D12_GPU_VIRTUAL_ADDRESS ortho_cbuf;
	D3D12_VERTEX_BUFFER_VIEW instances;
	D3D12_VERTEX_BUFFER_VIEW hud_vertices;
	D3D12_VERTEX_BUFFER_VIEW eviz_vertices;
};

struct constant_heap_data
{
	color_vertex cube_vbuf[24];
	short cube_ibuf[36];
};

struct dx12_data
{
	UINT size_changed;
	int screen_width, screen_height; // <= swap_chain_width
	int swap_chain_width, swap_chain_height;
	float screen_x_dips, screen_y_dips;
	float swap_chain_dpi;

	int next_frame_index; // into our own frames array; modulo MAX_FRAMES_TO_BUFFER.
	std::vector<frame_data> frames;
	FrameQueue frame_q;

	UINT64 CommandQueuePerformanceFrequency;

	UINT64 next_event_id;

	ComPtr<IDXGIFactory4> dxgi_factory;
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D11Device> device11;
	ComPtr<ID3D11On12Device> device11on12;
	ComPtr<ID3D11DeviceContext> device11context;
	ComPtr<ID2D1Device2> deviceD2D;
	ComPtr<ID2D1DeviceContext2> deviceD2Dcontext;
	ComPtr<ID3D12CommandQueue> command_queue;
	ComPtr<IDXGISwapChain3> swap_chain;
	WindowsEvent swap_event;

	ComPtr<ID2D1SolidColorBrush> text_brush;
	ComPtr<IDWriteTextFormat> text_format;

	DescriptorArrayT<D3D12_DESCRIPTOR_HEAP_TYPE_DSV> dsvs;

	ComPtr<ID3D12Resource> depth_buffer;

	ComPtr<ID3D12RootSignature> root_signature;
	ComPtr<ID3D12PipelineState> perspective_pipeline;
	ComPtr<ID3D12PipelineState> ortho_pipeline;
	ComPtr<ID3D12PipelineState> ortho_pipeline_for_lines;

	UploadHeapT<constant_heap_data> constant_heap;
	D3D12_VERTEX_BUFFER_VIEW cube_vbuf;
	D3D12_INDEX_BUFFER_VIEW cube_ibuf;

	ComPtr<ID3D12Fence> fence;
	WindowsEvent fence_event;
	UINT64 current_fence = 0;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissor;
	float4x4 perspective;
	float4x4 ortho;

	UINT64 startup_time;
	EventViz::EventStream eviz;
	PresentQueueStats pqs;
	LatencyStatistics latency_stats;
};

static dx12_data *dx12;
static EventViz::EventStream *eviz;
static PresentQueueStats *pqs;
static LatencyStatistics *latency_stats;
static dx12_swapchain_options swapchain_opts;

UINT64 next_event_id()
{
	return ++dx12->next_event_id;
}

UINT64 gpu_time_to_cpu_time(const frame_data *frame, UINT64 gpu_time)
{
	UINT64 gpu_offset = gpu_time - frame->gpu_clock_origin;
	double gpu_offset_seconds = double(gpu_offset) / dx12->CommandQueuePerformanceFrequency;
	UINT64 cpu_time = frame->cpu_clock_origin + UINT64(gpu_offset_seconds * g_QpcFreq);
	return cpu_time;
}

void eviz_gpu_event(const frame_data *frame, eventviz_aux *type, const UINT64 (&timestamps)[2], UINT64 user_id = 0)
{
	UINT64 start_time = gpu_time_to_cpu_time(frame, timestamps[0]);
	UINT64 end_time = gpu_time_to_cpu_time(frame, timestamps[1]);
	eviz->InsertEvent(EventViz::kGpuQueue, start_time, end_time, type, user_id);
}

static void wait_for_swap_chain(HANDLE waitable, const char *name)
{
	if(WAIT_TIMEOUT == WaitForSingleObjectEx(waitable, 1000, TRUE))
	{
		//__debugbreak();
	}
}

static void wait_for_all()
{
	UINT64 current_fence = ++dx12->current_fence;
	dx12->command_queue->Signal(dx12->fence.Get(), current_fence);
	dx12->fence->SetEventOnCompletion(current_fence, dx12->fence_event.Get());
	WaitForSingleObject(dx12->fence_event.Get(), INFINITE);
}

static bool initialize_dx12_internal()
{
	bool use_debug_layer = false;
#if defined(_DEBUG)
	use_debug_layer = true;
#endif

	// Enable the D2D debug layer.
	D2D1_FACTORY_OPTIONS d2dFactoryOptions = {};
	if (use_debug_layer) {
		d2dFactoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
	}

	UINT d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	if (use_debug_layer) {
		// Enable the D3D11 debug layer.
		d3d11DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	}

	dx12 = new dx12_data();
	dx12->startup_time = QpcNow();
	eviz = &dx12->eviz;
	pqs = &dx12->pqs;
	latency_stats = &dx12->latency_stats;
	latency_stats->SetHistoryLength(256);

	// Create the dxgi factory
	{
		UINT dxgiFactory2Flags = 0;
		if(use_debug_layer) dxgiFactory2Flags |= DXGI_CREATE_FACTORY_DEBUG;
		CheckHresult(CreateDXGIFactory2(dxgiFactory2Flags, IID_PPV_ARGS(&dx12->dxgi_factory)));
	}

	// Create the device
	{
		auto dxgi_factory = dx12->dxgi_factory.Get();

		if(use_debug_layer)
		{
			ComPtr<ID3D12Debug> debugController;
			CheckHresult(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
			debugController->EnableDebugLayer();
		}

		ComPtr<IDXGIAdapter> chosenAdapter;
#if 0
		// Try to find the first Intel adapter
		for(UINT adapterIdx = 0; ; adapterIdx++)
		{
			ComPtr<IDXGIAdapter> adapter;
			if(dxgi_factory->EnumAdapters(adapterIdx, &adapter) == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}

			DXGI_ADAPTER_DESC adapterDesc;
			CheckHresult(adapter->GetDesc(&adapterDesc));
			if(adapterDesc.VendorId == 0x8086)
			{
				chosenAdapter.Swap(adapter);
				break;
			}
		}
#endif

		//dxgi_factory->EnumWarpAdapter(IID_PPV_ARGS(&chosenAdapter));

		// Just grab the first adapter otherwise
		if(chosenAdapter == NULL)
		{
			CheckHresult(dxgi_factory->EnumAdapters(0, &chosenAdapter));
		}

		DXGI_ADAPTER_DESC chosenAdapterDesc;
		CheckHresult(chosenAdapter->GetDesc(&chosenAdapterDesc));
		OutputDebugStringW(L"Chosen adapter: ");
		OutputDebugStringW(chosenAdapterDesc.Description);
		OutputDebugStringW(L"\n");

		CheckHresult(D3D12CreateDevice(chosenAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dx12->device)));

		ComPtr<ID3D12InfoQueue> infoQueue;

		if (SUCCEEDED(dx12->device.As(&infoQueue)))
		{
			CheckHresult(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
			CheckHresult(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
			CheckHresult(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE));
			CheckHresult(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, FALSE));
			CheckHresult(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_MESSAGE, FALSE));

			D3D12_MESSAGE_ID hide[] =
			{
				D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
				D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
				D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,
			};
			D3D12_INFO_QUEUE_FILTER filter;
			memset(&filter, 0, sizeof(filter));
			filter.DenyList.NumIDs = _countof(hide);
			filter.DenyList.pIDList = hide;
			infoQueue->AddStorageFilterEntries(&filter);
		}

		SetName(dx12->device, "device");
	}

	auto device = dx12->device.Get();

	// Create the descriptor heap(s)
	{
		dx12->dsvs.Initialize(device, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, DsvDescriptors::Count);
	}

	// Create the command queue
	{
		D3D12_COMMAND_QUEUE_DESC queue_desc = {};
		queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		CheckHresult(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&dx12->command_queue)));
		SetName(dx12->command_queue, "command_queue");

		dx12->device->SetStablePowerState(TRUE);
		dx12->command_queue->GetTimestampFrequency(&dx12->CommandQueuePerformanceFrequency);
	}

	// init 11On12
	{
		CheckHresult(D3D11On12CreateDevice(
			device,
			d3d11DeviceFlags,
			nullptr,
			0,
			reinterpret_cast<IUnknown**>(dx12->command_queue.GetAddressOf()),
			1,
			0,
			&dx12->device11,
			&dx12->device11context,
			nullptr
			));

		CheckHresult(dx12->device11.As(&dx12->device11on12));
	}

	{
		// Create D2D/DWrite components;
		ComPtr<ID2D1Factory3> D2DFactory;
		ComPtr<IDWriteFactory> DWriteFactory;
		D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
		CheckHresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &d2dFactoryOptions, &D2DFactory));
		ComPtr<IDXGIDevice> dxgiDevice;
		CheckHresult(dx12->device11on12.As(&dxgiDevice));
		CheckHresult(D2DFactory->CreateDevice(dxgiDevice.Get(), &dx12->deviceD2D));
		CheckHresult(dx12->deviceD2D->CreateDeviceContext(deviceOptions, &dx12->deviceD2Dcontext));
		CheckHresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &DWriteFactory));

		// Create D2D/DWrite objects for rendering text.
		ThrowIfFailed(dx12->deviceD2Dcontext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &dx12->text_brush));
		ThrowIfFailed(DWriteFactory->CreateTextFormat(
			L"Segoe UI",
			NULL,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			20,
			L"en-us",
			&dx12->text_format
			));
		ThrowIfFailed(dx12->text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING));
		ThrowIfFailed(dx12->text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
	}

	// Create the frame fence & event
	{
		CheckHresult(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dx12->fence)));
		SetName(dx12->fence, "fence");
		dx12->fence_event.Initialize();
	}

	// Create the root signature
	{
		CD3DX12_ROOT_PARAMETER parameters[RootParameters::Count];

		auto& cbv_parameter = parameters[RootParameters::ProjectionCbuffer];
		cbv_parameter.InitAsConstantBufferView(0);
		
		auto& flags_parameter = parameters[RootParameters::Flags];
		flags_parameter.InitAsConstants(1, 1);

		ComPtr<ID3DBlob> pOutBlob;
		ComPtr<ID3DBlob> pErrorBlob;
		CD3DX12_ROOT_SIGNATURE_DESC root_sig_desc;
		root_sig_desc.Init(_countof(parameters), parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob);
		if(FAILED(hr)) {
			OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
			CheckHresult(hr);
		}
		CheckHresult(device->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&dx12->root_signature)));
		SetName(dx12->root_signature, "root_signature");
	}

	// Create the pipeline state
	{
		// Define the vertex input layout.

		static const D3D12_INPUT_ELEMENT_DESC layout[] =
		{
			// per vertex data
			{"position", 0, DXGI_FORMAT_R32G32B32_FLOAT,    PerVertexInputSlot, (UINT)offsetof(color_vertex, x),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"color",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     PerVertexInputSlot, (UINT)offsetof(color_vertex, r),        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},

			// per instance data
			{"modelview", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, PerInstanceInputSlot, (UINT)offsetof(instance_data, modelview.m[0]), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
			{"modelview", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, PerInstanceInputSlot, (UINT)offsetof(instance_data, modelview.m[1]), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
			{"modelview", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, PerInstanceInputSlot, (UINT)offsetof(instance_data, modelview.m[2]), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
			{"modelview", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, PerInstanceInputSlot, (UINT)offsetof(instance_data, modelview.m[3]), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
		pipeline_desc.InputLayout = {layout, sizeof(layout)/sizeof(layout[0])};
		pipeline_desc.pRootSignature = dx12->root_signature.Get();
		pipeline_desc.VS = { g_vertex_shader, sizeof(g_vertex_shader) };
		pipeline_desc.PS = { g_pixel_shader, sizeof(g_pixel_shader) };
		pipeline_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		// Premultiplied over blend
		pipeline_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		pipeline_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
		pipeline_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		pipeline_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		pipeline_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;

		pipeline_desc.DepthStencilState.DepthEnable = TRUE;
		pipeline_desc.DepthStencilState.DepthWriteMask= D3D12_DEPTH_WRITE_MASK_ALL;
		pipeline_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		pipeline_desc.DepthStencilState.StencilEnable = FALSE;
		pipeline_desc.SampleMask = UINT_MAX;
		pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipeline_desc.NumRenderTargets = 1;
		pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipeline_desc.SampleDesc.Count = 1;
		pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

		CheckHresult(device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&dx12->perspective_pipeline)));
		SetName(dx12->perspective_pipeline, "perspective_pipeline");

		pipeline_desc.DepthStencilState.DepthEnable = FALSE;
		pipeline_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		CheckHresult(device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&dx12->ortho_pipeline)));
		SetName(dx12->ortho_pipeline, "ortho_pipeline");

		pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		CheckHresult(device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&dx12->ortho_pipeline_for_lines)));
		SetName(dx12->ortho_pipeline_for_lines, "ortho_pipeline_for_lines");
	}

	// create the frames and frame queue
	dx12->frames.resize(swapchain_opts.create_time.gpu_frame_count);
	dx12->frame_q.Initialize(
		"Frames",
		device, dx12->command_queue.Get(), dx12->perspective_pipeline.Get(),
		dx12->frames.data(), sizeof(dx12->frames[0]), (UINT)dx12->frames.size(),
		dx12->device11on12.Get(), dx12->deviceD2Dcontext.Get());
	for(size_t i = 0; i < dx12->frames.size(); ++i)
	{
		auto& frame = dx12->frames[i];
		auto *ctx = dx12->frame_q.GetFrameContext(int(i));
		CheckHresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			ctx->mCommandAllocator.Get(), dx12->perspective_pipeline.Get(),
			IID_PPV_ARGS(&frame.mCommandList)));
		CheckHresult(frame.mCommandList->Close());
		
		CheckHresult(frame.dynamic.Initialize(device));
		auto dynamic = frame.dynamic.DataWO();
		auto gpu_base = frame.dynamic.Heap()->GetGPUVirtualAddress();
		frame.perspective_cbuf = GetGpuAddress(gpu_base, &dynamic->perspective_cbuf, dynamic);
		frame.ortho_cbuf = GetGpuAddress(gpu_base, &dynamic->ortho_cbuf, dynamic);
		frame.instances = MakeVertexBufferView(gpu_base, dynamic->instances, sizeof(dynamic->instances), dynamic);
		frame.hud_vertices = MakeVertexBufferView(gpu_base, dynamic->hud_vertices, sizeof(dynamic->hud_vertices), dynamic);
		frame.eviz_vertices = MakeVertexBufferView(gpu_base, dynamic->eventviz_verts, sizeof(dynamic->eventviz_verts), dynamic);
		SetName(frame.dynamic.Heap(), "frame_%ddynamic_heap", i);

		frame.timestamps.Initialize(device);
	}

	// Create and fill the geometry buffer & constant buffers
	{
		CheckHresult(dx12->constant_heap.Initialize(device));
		auto constant_data = dx12->constant_heap.DataWO();
		SetName(dx12->constant_heap.Heap(), "constant_heap");

		auto gpu_base = dx12->constant_heap.Heap()->GetGPUVirtualAddress();

		dx12->cube_vbuf = MakeVertexBufferView(gpu_base, constant_data->cube_vbuf,
			sizeof(constant_data->cube_vbuf), constant_data);
		dx12->cube_ibuf = MakeIndexBufferView(gpu_base, constant_data->cube_ibuf,
			sizeof(constant_data->cube_ibuf), constant_data, DXGI_FORMAT_R16_UINT);

		memcpy(constant_data->cube_vbuf, cube_vertices, sizeof(cube_vertices));
		memcpy(constant_data->cube_ibuf, cube_indices, sizeof(cube_indices));
	}

	return true;
}

bool initialize_dx12(dx12_swapchain_options *opts)
{
#if D3D12_DYNAMIC_LINK
	if(!LoadD3D12Dll(true))
	{
		return false;
	}
#endif

	memcpy(&swapchain_opts, opts, sizeof(dx12_swapchain_options));

	if(!initialize_dx12_internal())
	{
		shutdown_dx12();
		return false;
	}

	return true;
}

void trim_dx12()
{
	if (!dx12)
	{
		return;
	}

	wait_for_all();

	ComPtr<IDXGIDevice3> dxgi_device;
	if (SUCCEEDED(dx12->device11on12.As(&dxgi_device)))
	{
		dxgi_device->Trim();
	}
}

void shutdown_dx12()
{
	if (!dx12)
	{
		return;
	}

	wait_for_all();

	delete dx12;
	dx12 = 0;
}

static void dequeue_presents(dx12_render_stats *out_stats, int from = 0)
{
	float latency = 0;

	auto dequeue_entry = [&latency,from](PresentQueueStats::QueueEntry& e) {
		auto *Data = (EventViz::EventData*)e.UserData;
		eviz->End(Data, e.QueueExitedTime);
		if (!e.Dropped) {
			eviz->Vsync(e.QueueExitedTime);
			double real_latency = 1000 * double(e.QueueExitedTime - e.FrameBeginTime) / g_QpcFreq;
			if (real_latency)
			{
				latency_stats->Sample(real_latency);
				latency = (float)real_latency;
			}
		}
	};

	pqs->RetrieveStats(dx12->swap_chain.Get(), dequeue_entry);

	if (latency)
	{
		out_stats->latency = latency;
		out_stats->minmax_jitter = (float)latency_stats->EvaluateMinMaxMetric();
		out_stats->stddev_jitter = (float)latency_stats->EvaluateStdDevMetric();
	}
}

static void present_dx12(frame_data *frame, UINT64 FrameBeginTime, int vsync, dx12_render_stats *out_stats)
{
	UINT SyncInterval = vsync;
	auto chain = dx12->swap_chain.Get();

	auto present_call = eviz->Start(EventViz::kCpuQueue, &event_types[EVENT_TYPE_PRESENT_CALL]);
	chain->Present(SyncInterval, 0);
	eviz->End(present_call);

	UINT color_index = frame->backbuffer_index % NUM_FRAME_COLORS;

	auto present_entry = eviz->Start(EventViz::kPresentQueue, &event_types[EVENT_TYPE_COLOR0 + color_index], frame->render_id);
	pqs->PostPresent(chain, SyncInterval, FrameBeginTime, present_entry);
	
	dequeue_presents(out_stats);
}

static bool resize_dx12_internal(void *pHWND, void *pCoreWindow, float x_dips, float y_dips, float dpi)
{
	if (!dx12 || !dx12->device)
	{
		assert(false);
		return false;
	}

	if (x_dips <= 0) x_dips = 1;
	if (y_dips <= 0) y_dips = 1;

	dx12->size_changed = TRUE;
	dx12->screen_width = ConvertDipsToPixels(x_dips, dpi);
	dx12->screen_height = ConvertDipsToPixels(y_dips, dpi);

	dx12->screen_x_dips = x_dips;
	dx12->screen_y_dips = y_dips;

	wait_for_all();

	auto device = dx12->device.Get();
	auto dxgi_factory = dx12->dxgi_factory.Get();

	bool create_depth = false;

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.Stereo = FALSE;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.BufferCount = swapchain_opts.create_time.swapchain_buffer_count;
	swap_chain_desc.Scaling = DXGI_SCALING_NONE;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; // unused
	swap_chain_desc.Flags = swapchain_opts.create_time.use_waitable_object ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;

	// Create a new swap chain (initialization, parameters changed, etc)
	if(!dx12->swap_chain)
	{

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)

		HWND hwnd = *(HWND*)pHWND;

		HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfoA(monitor, &mi))
		{
			dx12->swap_chain_width = std::max<int>(dx12->screen_width, mi.rcMonitor.right - mi.rcMonitor.left);
			dx12->swap_chain_height = std::max<int>(dx12->screen_height, mi.rcMonitor.bottom - mi.rcMonitor.top);
		}
		else
		{
			dx12->swap_chain_width = dx12->screen_width;
			dx12->swap_chain_height = dx12->screen_height;
		}

		//wsi::log_message(0, 0, "Creating buffers @ %dx%d", dx12->swap_chain_width, dx12->swap_chain_height);

		swap_chain_desc.Width = dx12->swap_chain_width;
		swap_chain_desc.Height = dx12->swap_chain_height;

		ComPtr<IDXGISwapChain1> base_chain;
		CheckHresult(dxgi_factory->CreateSwapChainForHwnd(
			dx12->command_queue.Get(), hwnd, &swap_chain_desc, NULL, NULL, &base_chain));
		CheckHresult(base_chain.As(&dx12->swap_chain));

		CheckHresult(dxgi_factory->MakeWindowAssociation(
			hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN));
#else
		// TODO: monitor size possible on UWP?
		swap_chain_desc.Width = dx12->swap_chain_width = dx12->screen_width;
		swap_chain_desc.Height = dx12->swap_chain_height = dx12->screen_height;

		ComPtr<IDXGISwapChain1> base_chain;
		CheckHresult(
			dxgi_factory->CreateSwapChainForCoreWindow(dx12->command_queue.Get(),
				reinterpret_cast<IUnknown*>(pCoreWindow), &swap_chain_desc, nullptr, &base_chain));
		CheckHresult(base_chain.As(&dx12->swap_chain));

#endif

		SetName(dx12->swap_chain.Get(), "swap_chain");

		if (swap_chain_desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
			CheckHresult(dx12->swap_chain->SetMaximumFrameLatency(
				std::max(1,swapchain_opts.create_time.max_frame_latency)));
			dx12->swap_event = dx12->swap_chain->GetFrameLatencyWaitableObject();
		}

		dx12->frame_q.SetSwapChain(dx12->swap_chain.Get(), swap_chain_desc.Format, dpi, dpi);
		create_depth = true;
	}
	// Resize the existing swap chain (window resized)
	else if (dx12->swap_chain_width < dx12->screen_width ||
		dx12->swap_chain_height < dx12->screen_height ||
		dx12->swap_chain_dpi != dpi)
	{
		int old_width = dx12->swap_chain_width, old_height = dx12->swap_chain_height;
		dx12->swap_chain_width = std::max<int>(dx12->screen_width, dx12->swap_chain_width);
		dx12->swap_chain_height = std::max<int>(dx12->screen_height, dx12->swap_chain_height);
		//wsi::log_message(0, 0, "Resizing buffers: %dx%d -> %dx%d", old_width, old_height, dx12->swap_chain_width, dx12->swap_chain_height);

		dx12->frame_q.SetSwapChain(0);
		dx12->deviceD2Dcontext->SetTarget(nullptr);
		dx12->device11context->Flush();

		CheckHresult(dx12->swap_chain->ResizeBuffers(
			swapchain_opts.create_time.swapchain_buffer_count,
			dx12->swap_chain_width,
			dx12->swap_chain_height,
			swap_chain_desc.Format,
			swap_chain_desc.Flags));
		dx12->frame_q.SetSwapChain(dx12->swap_chain.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, dpi, dpi);
		create_depth = true;
	}

	dx12->swap_chain_dpi = dpi;
	//dx12->swap_chain->SetSourceSize(width, height); // doesn't work with DXGI_SCALING_NONE ??

	// Create Depth & DSVs
	if(create_depth)
	{
		D3D12_RESOURCE_DESC depth_desc{};
		depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depth_desc.Width = dx12->swap_chain_width;
		depth_desc.Height = dx12->swap_chain_height;
		depth_desc.DepthOrArraySize = 1;
		depth_desc.MipLevels = 1;
		depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
		depth_desc.SampleDesc.Count = 1;
		depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE clear_depth{};
		clear_depth.Format = DXGI_FORMAT_D32_FLOAT;
		clear_depth.DepthStencil.Depth = 1.0f;

		CheckHresult(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depth_desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clear_depth,
			IID_PPV_ARGS(&dx12->depth_buffer)));
		SetName(dx12->depth_buffer, "depth_buffer");

		device->CreateDepthStencilView(
			dx12->depth_buffer.Get(),
			NULL,                              // use default desc
			dx12->dsvs[DsvDescriptors::Main].CpuHandle);
	}
	
	// Create viewport, scissor, perspective and ortho

#if 0
	dx12->screen_width = 1024;
	dx12->screen_height = 768;
#endif

	D3D12_VIEWPORT viewport {};
	D3D12_RECT scissor {};

	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f; 
	viewport.Width = (FLOAT)dx12->screen_width;
	viewport.Height = (FLOAT)dx12->screen_height;

	scissor.right = dx12->screen_width;
	scissor.bottom = dx12->screen_height;

	dx12->viewport = viewport;
	dx12->scissor = scissor;
	load_simple_perspective(&dx12->perspective, 45.0f, viewport.Width / viewport.Height, 0.1f, 100.0f, viewport.MinDepth, viewport.MaxDepth);
	load_simple_ortho(&dx12->ortho, dx12->screen_x_dips, dx12->screen_y_dips, 100.0f, viewport.MinDepth, viewport.MaxDepth);

	return true;
}

// two vertices
static void build_eviz_line(color_vertex *v, const EventViz::Line& line)
{
	memset(v, 0, 2 * sizeof(color_vertex));

	UINT color = 0xff000000; // black, full alpha;

	auto X0 = float(line.X0);
	auto Y0 = float(line.Y0);
	auto X1 = float(line.X1);
	auto Y1 = float(line.Y1);

	v[0].x = X0;
	v[0].y = Y0;
	v[0].rgba = color;

	v[1].x = X1;
	v[1].y = Y1;
	v[1].rgba = color;
}

static int build_line_joints(color_vertex *v, const EventViz::Line& line)
{
	memset(v, 0, 12 * sizeof(color_vertex));

	UINT black = 0xff000000; // black, full alpha;

	if (line.X0 == line.X1) {
		return 0;
	}

	float x[2] = { float(line.X0), float(line.X1) };
	float y[2] = { float(line.Y0), float(line.Y1) };

	for (int i = 0; i < 2; ++i)
	{
		float left = x[i] - 1.5f;
		float right = x[i] + 1.5f;
		float top = y[i] - 1.5f;
		float bottom = y[i] + 1.5f;

		v[0].x = left;
		v[0].y = bottom;
		v[0].rgba = black;

		v[1].x = left;
		v[1].y = top;
		v[1].rgba = black;

		v[2].x = right;
		v[2].y = bottom;
		v[2].rgba = black;

		v[3].x = left;
		v[3].y = top;
		v[3].rgba = black;

		v[4].x = right;
		v[4].y = bottom;
		v[4].rgba = black;

		v[5].x = right;
		v[5].y = top;
		v[5].rgba = black;

		v += 6;
	}

	return 4;
}

static int build_eviz_quad(color_vertex *v, const EventViz::Rectangle& r)
{
	memset(v, 0, 6*sizeof(color_vertex));

	auto data = (eventviz_aux *)r.Event->UserData;

	UINT rgba = data ? data->rgba : 0xff000000;
	UINT left_color = rgba;
	//UINT right_color = rgba;
	//UINT right_color = ((rgba >> 1) & 0x007f7f7f) | 0xff000000; // half brightness
	UINT right_color = (((rgba >> 3) & 0x001f1f1f) * 7) | 0xff000000; // 7/8th brightness

	float left = float(r.Left);
	float top = float(r.Top);
	float right = float(r.Right);
	float bottom = float(r.Bottom);

	if (r.Flags & EventViz::Rectangle::Dropped)
	{
		// +
		// | \
		// +---+
		// | / 
		// +

		float mid = (top + bottom) / 2;

		// bottom half
		v[0].x = left;
		v[0].y = bottom;
		v[0].rgba = left_color;
		v[1].x = left;
		v[1].y = top;
		v[1].rgba = left_color;
		v[2].x = right;
		v[2].y = mid;
		v[2].rgba = right_color;

		return 1;
	}
	else
	{
		// 1---3
		// | \ |
		// 0---2

		v[0].x = left;
		v[0].y = bottom;
		v[0].rgba = left_color;

		v[1].x = left;
		v[1].y = top;
		v[1].rgba = left_color;

		v[2].x = right;
		v[2].y = bottom;
		v[2].rgba = right_color;

		// ---------------------

		v[3].x = left;
		v[3].y = top;
		v[3].rgba = left_color;

		v[4].x = right;
		v[4].y = bottom;
		v[4].rgba = right_color;

		v[5].x = right;
		v[5].y = top;
		v[5].rgba = right_color;

		return 2;
	}
}

static int build_eviz_quad_outlines(color_vertex *v, const EventViz::Rectangle& r)
{
	memset(v, 0, 8 * sizeof(color_vertex));

	UINT black = 0xff000000;

	float x[2] = { float(r.Left), float(r.Right) };
	float y[2] = { float(r.Top), float(r.Bottom) };

	if (r.Flags & EventViz::Rectangle::Dropped)
	{
		v[0 + 0].x = x[0];
		v[0 + 0].y = y[0];
		v[0 + 0].rgba = black;
		v[0 + 1].x = x[0];
		v[0 + 1].y = y[1];
		v[0 + 1].rgba = black;
		v[2 + 0].x = x[0];
		v[2 + 0].y = y[0];
		v[2 + 0].rgba = black;
		v[2 + 1].x = x[1];
		v[2 + 1].y = (y[0] + y[1]) / 2;
		v[2 + 1].rgba = black;
		v[4 + 0].x = x[0];
		v[4 + 0].y = y[1];
		v[4 + 0].rgba = black;
		v[4 + 1].x = x[1];
		v[4 + 1].y = (y[0] + y[1]) / 2;
		v[4 + 1].rgba = black;
		return 3;
	}
	else
	{
		for (int segment = 0; segment < 4; ++segment) {
			int xi0 = (0b0110 >> segment) & 1;
			int xi1 = (0b0011 >> segment) & 1;
			int yi0 = (0b1100 >> segment) & 1;
			int yi1 = (0b0110 >> segment) & 1;
			v[2 * segment + 0].x = x[xi0];
			v[2 * segment + 0].y = y[yi0];
			v[2 * segment + 0].rgba = black;
			v[2 * segment + 1].x = x[xi1];
			v[2 * segment + 1].y = y[yi1];
			v[2 * segment + 1].rgba = black;
		}
		return 4;
	}
}

// returns the number of primitives to draw (TRIANGLE STRIP)
static UINT build_eviz_display(
	color_vertex (&write) [MAX_EVIZ_VERTS],
	UINT *eviz_tri_start, UINT *eviz_tri_count,
	UINT *eviz_line_start, UINT *eviz_line_count,
	UINT NUM_VSYNCS_TO_DISPLAY)
{
	EventViz::FloatRect screen;
	screen.Left = 0;
	screen.Right = dx12->screen_x_dips;
	screen.Top = 0;
	screen.Bottom = dx12->screen_y_dips;

	UINT vertex_count = 0;

	UINT SLOP_VSYNCS = 0;
	NUM_VSYNCS_TO_DISPLAY += SLOP_VSYNCS;

	UINT vsync_count = eviz->GetVsyncCount();
	UINT first_vsync = vsync_count < NUM_VSYNCS_TO_DISPLAY ? 0 : vsync_count - NUM_VSYNCS_TO_DISPLAY;
	UINT last_vsync = vsync_count < 1 ? 0 : vsync_count - 1 - SLOP_VSYNCS;

	EventViz::EventVisualization visualization;
	EventViz::CreateVisualization(*eviz, first_vsync, last_vsync, screen, visualization);

	*eviz_tri_start = vertex_count;
	*eviz_tri_count = 0;

	auto nr = visualization.Rectangles.size();
	auto rs = visualization.Rectangles.data();

	auto nl = visualization.Lines.size();
	auto ls = visualization.Lines.data();

	// rectangles
	for (size_t i = 0; i < nr; ++i)
	{
		if (vertex_count + 6 >= MAX_EVIZ_VERTS) {
			goto overflow;
		}
		int tris = build_eviz_quad(&write[vertex_count], rs[i]);
		vertex_count += 3 * tris;
		*eviz_tri_count += tris;
	}

	// line joints
	for (size_t i = 0; i < nl; ++i)
	{
		if (vertex_count + 12 > MAX_EVIZ_VERTS) {
			goto overflow;
		}
		int tris = build_line_joints(&write[vertex_count], ls[i]);
		vertex_count += 3*tris;
		*eviz_tri_count += tris;
	}

	*eviz_line_start = vertex_count;
	*eviz_line_count = 0;

	// rectangle outlines
	for (size_t i = 0; i < nr; ++i)
	{
		if (vertex_count + 8 >= MAX_EVIZ_VERTS) {
			goto overflow;
		}
		int lines = build_eviz_quad_outlines(&write[vertex_count], rs[i]);
		vertex_count += 2*lines;
		*eviz_line_count += lines;
	}

	// lines
	for (size_t i = 0; i < nl; ++i)
	{
		if (vertex_count + 2 >= MAX_EVIZ_VERTS) {
			goto overflow;
		}
		build_eviz_line(&write[vertex_count], ls[i]);
		vertex_count += 2;
		*eviz_line_count += 1;
	}

overflow:

	return vertex_count;
}

static void build_frame(
	ID3D12GraphicsCommandList *command_list,
	D3D12_CPU_DESCRIPTOR_HANDLE render_target_view,
	frame_data& frame,
	game_data *game, float fractional_ticks)
{
	auto& timestamp_heap = frame.timestamps;
	auto timestamps = timestamp_heap.GetTimestampWriteStruct();

	gpu_timer_scope scope(&timestamp_heap, command_list, timestamps->draw);

	const float scale = 1.0f / WORLD_ONE;

	// Build the fram einstance data
	auto data = frame.dynamic.DataWO();

	load_identity(&data->instances[0].modelview);

	for (int i = 0; i < 2; ++i)
	{
		cube_object& cube = game->cubes[i];

		float rotation = (cube.rotation + game->cube_rotation_speed*fractional_ticks)*(360.0f / FULL_CIRCLE);
		float x = (cube.position.x + cube.velocity.x*fractional_ticks)*scale;
		float y = (cube.position.y + cube.velocity.y*fractional_ticks)*scale;
		float z = (cube.position.z + cube.velocity.z*fractional_ticks)*scale;

		float4x4 modelview;
		load_identity(&modelview);
		translate(&modelview, 0.0f, -2.5f, -10.0f); // camera
		translate(&modelview, x, y, z); // object position;
		rotate(&modelview, rotation, 1.0f, 1.0f, 1.0f);

		// Write the instance data
		data->instances[1+i].modelview = modelview;
	}

	UINT eviz_tri_start, eviz_tri_count;
	UINT eviz_line_start, eviz_line_count;

	build_eviz_display(data->eventviz_verts,
		&eviz_tri_start, &eviz_tri_count,
		&eviz_line_start, &eviz_line_count, 16);

	data->perspective_cbuf.projection = dx12->perspective;
	data->ortho_cbuf.projection = dx12->ortho;

	const auto depth_stencil_view = dx12->dsvs[DsvDescriptors::Main].CpuHandle;

	// Clear buffers, setup targets, viewports, scissor
	//const FLOAT clear_color[4] = {1.0f, 0.75f, 0.0f, 0.0f};
	const FLOAT clear_color[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
	D3D12_RECT rect {};
	rect.right = dx12->screen_width;
	rect.bottom = dx12->screen_height;

#if 1
	{
		gpu_timer_scope scope(&timestamp_heap, command_list, timestamps->clear);
		command_list->ClearDepthStencilView(depth_stencil_view, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, 0);
		command_list->ClearRenderTargetView(render_target_view, clear_color, 1, &rect);
	}
#endif

	command_list->OMSetRenderTargets(1, &render_target_view, FALSE, &depth_stencil_view);
	command_list->RSSetViewports(1, &dx12->viewport);
	command_list->RSSetScissorRects(1, &dx12->scissor);

	// Main content: Setup state
	command_list->SetGraphicsRootSignature(dx12->root_signature.Get());
	command_list->SetGraphicsRootConstantBufferView(RootParameters::ProjectionCbuffer, frame.perspective_cbuf);
	command_list->SetPipelineState(dx12->perspective_pipeline.Get());
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	command_list->IASetVertexBuffers(PerVertexInputSlot, 1, &dx12->cube_vbuf);
	command_list->IASetIndexBuffer(&dx12->cube_ibuf);
	command_list->IASetVertexBuffers(PerInstanceInputSlot, 1, &frame.instances);

	// Main content: Draw
	{
		gpu_timer_scope scope(&timestamp_heap, command_list, timestamps->draw_cubes);
		command_list->SetGraphicsRoot32BitConstant(RootParameters::Flags, 0, 0);
		int cube_count = (int)pow(2.0, swapchain_opts.any_time.overdraw_factor);
		for(int i = 0; i < cube_count; ++i)
		{
			command_list->DrawIndexedInstanced(36, 2, 0, 0, 1);
		}
	}

	// Timeline Viz: setup state
	command_list->OMSetRenderTargets(1, &render_target_view, FALSE, NULL);
	command_list->SetPipelineState(dx12->ortho_pipeline.Get());
	command_list->SetGraphicsRootConstantBufferView(RootParameters::ProjectionCbuffer, frame.ortho_cbuf);

	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	if (eviz_tri_count || eviz_line_count)
	{
		gpu_timer_scope scope(&timestamp_heap, command_list, timestamps->draw_eviz);
		command_list->SetGraphicsRoot32BitConstant(RootParameters::Flags, 0, 0);
		command_list->IASetVertexBuffers(PerVertexInputSlot, 1, &frame.eviz_vertices);

		if (eviz_tri_count)
		{
			command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			command_list->DrawInstanced(eviz_tri_count*3, 1, eviz_tri_start, 0);
		}

		if (eviz_line_count)
		{
			command_list->SetPipelineState(dx12->ortho_pipeline_for_lines.Get());
			command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
			command_list->DrawInstanced(eviz_line_count*2, 1, eviz_line_start, 0);
		}
	}
}

static void render_d2d(FrameQueue::FrameContext *ctx, const WCHAR *text)
{
	D2D1_RECT_F textRect = D2D1::RectF(0, 0, (float)dx12->screen_width, (float)dx12->screen_height);

	auto device11on12 = dx12->device11on12.Get();
	auto device11context = dx12->device11context.Get();
	auto d2dcontext = dx12->deviceD2Dcontext.Get();
	auto format = dx12->text_format.Get();
	auto brush = dx12->text_brush.Get();

	// Acquire our wrapped render target resource for the current back buffer.
	ID3D11Resource *resourcesToAcquire[] = { ctx->mWrapped11BackBuffer };
	device11on12->AcquireWrappedResources(resourcesToAcquire, _countof(resourcesToAcquire));

	// Render text directly to the back buffer.
	d2dcontext->SetDpi(dx12->swap_chain_dpi, dx12->swap_chain_dpi);
	d2dcontext->SetTarget(ctx->mD2DRenderTarget);
	d2dcontext->BeginDraw();
	d2dcontext->SetTransform(D2D1::Matrix3x2F::Identity());
	d2dcontext->DrawText(
		text,
		(UINT32)wcslen(text),
		format,
		&textRect,
		brush
		);
	ThrowIfFailed(d2dcontext->EndDraw());

	// Release our wrapped render target resource. Releasing 
	// transitions the back buffer resource to the state specified
	// as the OutState when the wrapped resource was created.
	device11on12->ReleaseWrappedResources(resourcesToAcquire, _countof(resourcesToAcquire));

	// Flush to submit the 11 command list to the shared command queue.
	device11context->Flush();
}

void render_game_dx12(wchar_t *hud_text, game_data *game, float fractional_ticks, int vsync_interval, dx12_render_stats *stats)
{
	if (stats)
	{
		ZeroMemory(stats, sizeof(*stats));
	}

	if (dx12->swap_event)
	{
		auto chain_wait_event = eviz->Start(EventViz::kCpuQueue, &event_types[EVENT_TYPE_SWAPCHAIN_WAIT]);
		wait_for_swap_chain(dx12->swap_event.Get(), "layer 0");
		eviz->End(chain_wait_event);
	}

	eviz->TrimToLastNVsyncs(256);

	FrameQueue::FrameContext *ctx;

	{
		auto frame_wait_event = eviz->Start(EventViz::kCpuQueue, &event_types[EVENT_TYPE_FRAME_WAIT]);
		dx12->frame_q.BeginFrame(&ctx);
		eviz->End(frame_wait_event);
	}

	auto CpuFrameStart = QpcNow();
	auto frame = ctx->CastUserDataAs<frame_data>();
	auto command_list = frame->mCommandList.Get();
	auto *timestamps = frame->timestamps.GetTimestampReadStruct(true);

	ThrowIfFailed(command_list->Reset(ctx->mCommandAllocator.Get(), dx12->perspective_pipeline.Get()));

	{
		auto TransitionPresentToRenderTarget = CD3DX12_RESOURCE_BARRIER::
			Transition(ctx->mBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		command_list->ResourceBarrier(1, &TransitionPresentToRenderTarget);
	}

	UINT color_index = frame->backbuffer_index % NUM_FRAME_COLORS;
	if (frame->cpu_clock_origin && timestamps->draw[0]) {
		eviz_gpu_event(frame, &event_types[EVENT_TYPE_COLOR0 + color_index], timestamps->draw, frame->render_id);
	}

#if 0
	auto print_duration = [](const char *section, const UINT64(&timestamps)[2]) {
		auto duration = timestamps[1] - timestamps[0];
		double millisecs = 1000.0*duration / dx12->CommandQueuePerformanceFrequency;
		char buf[256];
		sprintf(buf, "%s: %.2lfms\n", section, millisecs);
		OutputDebugString(buf);
	};

	print_duration("clear", timestamps->clear);
	print_duration("draw_all", timestamps->draw);
	print_duration("draw_cubes", timestamps->draw_cubes);
	print_duration("draw_eviz", timestamps->draw_eviz);
#endif

	dequeue_presents(stats, 1);

	{
		dx12->command_queue->GetClockCalibration(&frame->gpu_clock_origin, &frame->cpu_clock_origin);
		frame->render_id = next_event_id();
		frame->backbuffer_index = ctx->mBackBufferIndex;

		color_index = frame->backbuffer_index % NUM_FRAME_COLORS;

		auto render_event = eviz->Start(EventViz::kCpuQueue, &event_types[EVENT_TYPE_COLOR0 + color_index], frame->render_id);
		auto command_list = frame->mCommandList.Get();

		auto start = QpcNow();
		int draw_ms = swapchain_opts.any_time.cpu_draw_ms;
		if (swapchain_opts.inject.cpu_hiccup_count > 0) {
			draw_ms += swapchain_opts.inject.cpu_hiccup_size;
		}
		auto target = start + g_QpcFreq*draw_ms / 1000;
		build_frame(command_list, ctx->mBackBufferRTV, *frame, game, fractional_ticks);
		frame->timestamps.ReadbackCommand(command_list);
		while (QpcNow() < target) {
			;
		}

		// Close & Execute the command list
		ThrowIfFailed(command_list->Close());
		dx12->command_queue->ExecuteCommandLists(1, CommandListCast(&command_list));

		eviz->End(render_event);

		// render D2D (this also transitions the backbuffer to PRESENT)
		render_d2d(ctx, hud_text);
	}
	
	auto CpuFrameEnd = QpcNow();
	dx12->frame_q.EndFrame(ctx);

	if (stats)
	{
		stats->cpu_frame_time = float(double(CpuFrameEnd - CpuFrameStart) / g_QpcFreq);
		stats->gpu_frame_time = float(double(timestamps->draw[1] - timestamps->draw[0]) / dx12->CommandQueuePerformanceFrequency);
	}

	present_dx12(frame, CpuFrameStart, vsync_interval, stats);
}

bool set_swapchain_options_dx12(void *pHWND, void *pCoreWindow, float x_dips, float y_dips, float dpi, dx12_swapchain_options *opts)
{
	bool recreate = false;

	if (!dx12)
	{
		recreate = true;
	}
	else
	{
		if (!x_dips) x_dips = dx12->screen_x_dips;
		if (!y_dips) y_dips = dx12->screen_y_dips;

		if (dx12->device && FAILED(dx12->device->GetDeviceRemovedReason()))
		{
			recreate = true;
		}
		else if (memcmp(&swapchain_opts.create_time, &opts->create_time,
			sizeof(dx12_swapchain_options::create_time)))
		{
			recreate = true;
		}
	}

	if (recreate)
	{
		shutdown_dx12();
		if (!initialize_dx12(opts)) {
			return false;
		}
		return resize_dx12_internal(pHWND, pCoreWindow, x_dips, y_dips, dpi);
	}
	else if (x_dips != dx12->screen_x_dips ||
		y_dips != dx12->screen_y_dips ||
		dpi != dx12->swap_chain_dpi)
	{
		return resize_dx12_internal(pHWND, pCoreWindow, x_dips, y_dips, dpi);
	}
	else
	{
		memcpy(&swapchain_opts, opts, sizeof(dx12_swapchain_options));

		opts->inject.cpu_hiccup_count = std::max(opts->inject.cpu_hiccup_count - 1, 0);
	}

	return true;
}

void pause_eviz_dx12(bool pause)
{
	dx12->eviz.Pause(pause);
}