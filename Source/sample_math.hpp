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

struct float4x4
{
	// "column major" layout matches OpenGL
	union {
		float m[4][4];
		struct {
			float _11, _21, _31, _41;
			float _12, _22, _32, _42;
			float _13, _23, _33, _43;
			float _14, _24, _34, _44;
		};
	};
};

void load_identity(float4x4 *m);
void mult_matrix(float4x4 *m, const float4x4 *op);
void translate(float4x4 *m, float x, float y, float z);
void rotate(float4x4 *m, float degrees, float x, float y, float z);

// NOTE: the near_depth and far_depth parameters allow building projection matrices compatible with both GL and DX:
// DX's depth range is [0,1] where GL's is [-1,1]
void load_frustum(float4x4 *m, float left, float right, float bottom, float top, float near, float far, float near_depth, float far_depth);
void load_simple_ortho(float4x4 *m, float width, float height, float depth, float near_depth, float far_depth);
void load_simple_perspective(float4x4 *m, float fov_y, float aspect, float near, float far, float near_depth, float far_depth);
