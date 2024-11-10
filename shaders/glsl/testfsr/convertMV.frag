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
	uvec2 mv = texture(samplerMV, inUV).xy;
    vec3 color = texture(samplerColor, inUV).xyz;
    float depth = float(texture(samplerDepth, inUV).x) / 16777215.0f;

    vec2 pos = vec2(2.0 * inUV.xy - 1.0);

    if (mv == uvec2(0, 0)) {
        vec4 currentClipPos = vec4(pos, depth, 1.0);
        vec4 worldPos = inverse(ubo.vpMatrix) * currentClipPos;
        vec4 prevClipPos = ubo.prevVpMatrix * worldPos;
        prevClipPos /= prevClipPos.w;

        vec2 mv_static = (currentClipPos.xy - prevClipPos.xy) * 0.2495000064373016357421875 + 0.49999237060546875;
        mv = uvec2(mv_static * 65535.0 + vec2(0.5));
    } 
    outMV = vec2(mv) * vec2(1.525902189314365386962890625e-05 * 4.008016109466552734375) - vec2(2.0039775371551513671875);

    outMV = vec2(float(mv.x), float(depth) / 4294967296.0);
    outColor = vec4(color, 1.0);
    gl_FragDepth = depth;
}