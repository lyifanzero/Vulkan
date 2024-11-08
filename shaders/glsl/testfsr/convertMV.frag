#version 450

layout (set = 0, binding = 0) uniform UniformBufferObject {
	mat4 vpMatrix;
	mat4 prevVpMatrix;
} ubo;

layout (set = 0, binding = 1) uniform usampler2D samplerMV;
layout (set = 0, binding = 2) uniform sampler2D samplerColor;
layout (set = 0, binding = 3) uniform usampler2D samplerDepth;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec2 outMV;
layout (location = 1) out vec4 outColor;

void main() 
{
	uvec2 mv = texture(samplerMV, inUV).rg;
	vec3 color = texture(samplerColor, inUV).rgb;
	uint depth = texture(samplerDepth, inUV).r;
	outMV = vec2(mv.x, float(depth)/4294967295.0);
	outColor = vec4(color, 1.0);
}