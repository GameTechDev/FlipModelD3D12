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
#include "sample_cube.hpp"

#define FACE1_COLOR 0,255,0,255
#define FACE2_COLOR 255,128,0,255
#define FACE3_COLOR 255,0,0,255
#define FACE4_COLOR 255,255,0,255
#define FACE5_COLOR 0,0,255,255
#define FACE6_COLOR 255,0,255,255

color_vertex cube_vertices[6][4] =
{
	{
		{ 1.0f, 1.0f, -1.0f, FACE1_COLOR },
		{ -1.0f, 1.0f, -1.0f, FACE1_COLOR },
		{ -1.0f, 1.0f, 1.0f, FACE1_COLOR },
		{ 1.0f, 1.0f, 1.0f, FACE1_COLOR },
	},
	{
		{ 1.0f, -1.0f, 1.0f, FACE2_COLOR },
		{ -1.0f, -1.0f, 1.0f, FACE2_COLOR },
		{ -1.0f, -1.0f, -1.0f, FACE2_COLOR },
		{ 1.0f, -1.0f, -1.0f, FACE2_COLOR },
	},
	{
		{ 1.0f, 1.0f, 1.0f, FACE3_COLOR },
		{ -1.0f, 1.0f, 1.0f, FACE3_COLOR },
		{ -1.0f, -1.0f, 1.0f, FACE3_COLOR },
		{ 1.0f, -1.0f, 1.0f, FACE3_COLOR },
	},
	{
		{ 1.0f, -1.0f, -1.0f, FACE4_COLOR },
		{ -1.0f, -1.0f, -1.0f, FACE4_COLOR },
		{ -1.0f, 1.0f, -1.0f, FACE4_COLOR },
		{ 1.0f, 1.0f, -1.0f, FACE4_COLOR },
	},
	{
		{ -1.0f, 1.0f, 1.0f, FACE5_COLOR },
		{ -1.0f, 1.0f, -1.0f, FACE5_COLOR },
		{ -1.0f, -1.0f, -1.0f, FACE5_COLOR },
		{ -1.0f, -1.0f, 1.0f, FACE5_COLOR },
	},
	{
		{ 1.0f, 1.0f, -1.0f, FACE6_COLOR },
		{ 1.0f, 1.0f, 1.0f, FACE6_COLOR },
		{ 1.0f, -1.0f, 1.0f, FACE6_COLOR },
		{ 1.0f, -1.0f, -1.0f, FACE6_COLOR },
	}
};

#define CUBE_INDS(face) {4*face,4*face+1,4*face+2,4*face,4*face+2,4*face+3}
unsigned short cube_indices[6][6] = {
	CUBE_INDS(0),
	CUBE_INDS(1),
	CUBE_INDS(2),
	CUBE_INDS(3),
	CUBE_INDS(4),
	CUBE_INDS(5),
};
