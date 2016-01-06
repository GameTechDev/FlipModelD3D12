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
