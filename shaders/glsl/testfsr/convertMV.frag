#version 450

layout (location = 0) uniform sampler2D samplerMV;
layout (location = 1) uniform sampler2D samplerColor;
layout (location = 2) uniform sampler2D samplerDepth;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec2 outMV;
layout (location = 1) out vec4 outColor;

void main() 
{
	vec2 mv = texture(samplerMV, inUV).rg;
	vec4 color = texture(samplerColor, inUV).rgb;
	float depth = texture(samplerDepth, inUV).r;
	outMV = vec2(1.0f, 1.0f);
	outColor = vec4(1.0, 1.0, 1.0, 1.0);
}