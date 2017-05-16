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
#include "sample_math.hpp"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

void load_identity(float4x4 *m)
{
	memset(m, 0, sizeof(*m));
	m->_11 = 1.0f;
	m->_22 = 1.0f;
	m->_33 = 1.0f;
	m->_44 = 1.0f;
}

void translate(float4x4 *m, float x, float y, float z)
{
	m->_14 += m->_11*x + m->_12*y + m->_13*z;
	m->_24 += m->_21*x + m->_22*y + m->_23*z;
	m->_34 += m->_31*x + m->_32*y + m->_33*z;
	m->_44 += m->_41*x + m->_42*y + m->_43*z;
}

void mult_matrix(float4x4 *m, const float4x4 *op)
{
	float4x4 temp;
	for (int c = 0; c < 4; ++c)
	{
		for (int r = 0; r < 4; ++r)
		{
			// column major ordering: mat[row, col] = m[col][row]
			// matrix multiplication: out[row r, col c] = dot(row [r] of m, col [c] of op)
			temp.m[c][r] =
				m->m[0][r] * op->m[c][0] +
				m->m[1][r] * op->m[c][1] +
				m->m[2][r] * op->m[c][2] +
				m->m[3][r] * op->m[c][3];
		}
	}
	*m = temp;
}

void rotate(float4x4 *m, float degrees, float x, float y, float z)
{
	float mag = sqrtf(x*x + y*y + z*z);
	if (mag <= 0) return;

	const float _deg2rad = float(M_PI / 180.0f);
	float theta = degrees * _deg2rad;
	float s = sinf(theta);
	float c = cosf(theta);

	float scale = 1.0f / mag;
	x *= scale; y *= scale; z *= scale;

	float xx = x*x, yy = y*y, zz = z*z;
	float xy = x*y, yz = y*z, xz = x*z;
	float xs = x*s, ys = y*s, zs = z*s;
	float d = 1.0f - c;

	float4x4 r;
	r._11 = xx*d + c;
	r._12 = xy*d - zs;
	r._13 = xz*d + ys;
	r._14 = 0.0f;

	r._21 = xy*d + zs;
	r._22 = yy*d + c;
	r._23 = yz*d - xs;
	r._24 = 0.0f;

	r._31 = xz*d - ys;
	r._32 = yz*d + xs;
	r._33 = zz*d + c;
	r._34 = 0.0f;

	r._41 = 0.0f;
	r._42 = 0.0f;
	r._43 = 0.0f;
	r._44 = 1.0f;

	mult_matrix(m, &r);
}

void load_frustum(float4x4 *m,
	float left, float right, float bottom, float top,
	float near, float far, float near_depth, float far_depth)
{
	float r_l, t_b, f_n;
	memset(m, 0, sizeof(*m));

	r_l = right - left;
	m->_11 = 2 * near / r_l;
	m->_13 = (right + left) / r_l;

	t_b = top - bottom;
	m->_22 = 2 * near / t_b;
	m->_23 = (top + bottom) / t_b;

	f_n = far - near;
	m->_33 = (near*near_depth - far*far_depth) / f_n;
	m->_34 = far*near*(near_depth - far_depth) / f_n;

	m->_43 = -1.0f;
}

// simplified ortho:
// left = 0, right = width
// top = 0, bottom = height
// near = 0, far = depth
void load_simple_ortho(float4x4 *m, float width, float height, float depth, float near_depth, float far_depth)
{
	memset(m, 0, sizeof(*m));

	m->_11 = 2.0f / width;
	m->_22 = -2.0f / height;
	m->_33 = (near_depth - far_depth) / depth;

	m->_14 = -1.0f;
	m->_24 = 1.0f;
	m->_34 = near_depth;
	m->_44 = 1.0f;
}

// simplified frustum:
// square, centered on zero
void load_simple_perspective(float4x4 *m, float fov_y, float aspect, float near, float far, float near_depth, float far_depth)
{
	const float _half_deg2rad_ = float(M_PI / 360.0);
	float hh = tanf(fov_y * _half_deg2rad_) * near;
	float hw = hh * aspect;
	load_frustum(m, -hw, hw, -hh, hh, near, far, near_depth, far_depth);
}
