#include "vertex_shader.hlsl"

float4 pixel_shader(vs_out input) : SV_TARGET
{
	return input.color;
}
