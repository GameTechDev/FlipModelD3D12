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
