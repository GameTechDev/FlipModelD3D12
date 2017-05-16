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
cbuffer dx11_cbuffer : register(b0)
{
	float4x4 projection;
};

cbuffer flags_cbuffer : register(b1)
{
	uint flags;
};

struct vs_in
{
	float3 position : position;
	float4 color : color;
	float4x4 modelview : modelview; // per-instance
};

struct vs_out
{
	float4 position: SV_Position;
	float4 color : color;
};

vs_out vertex_shader( vs_in input )
{
	vs_out output;
	
	float4 viewspace = mul(input.modelview, float4(input.position, 1.0));
	float4 clipspace = mul(projection, viewspace);
	output.position = clipspace;

	output.color = input.color;

	return output;
}
