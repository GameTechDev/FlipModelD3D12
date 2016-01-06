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
