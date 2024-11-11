#version 450

layout (set = 0, binding = 0) uniform UniformBufferObject {
	vec4 vpMatrixVec[5];
	vec4 vpMatrixVecPrev[5];
} ubo;

layout (set = 0, binding = 1) uniform usampler2D samplerMV;
layout (set = 0, binding = 2) uniform sampler2D samplerColor;
layout (set = 0, binding = 3) uniform usampler2D samplerDepth;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec2 outMV;
layout (location = 1) out vec4 outColor;

#define vpMatrix (mat4(ubo.vpMatrixVec[0].xyzw, ubo.vpMatrixVec[1].xyzw, ubo.vpMatrixVec[2].xyzw, ubo.vpMatrixVec[3].xyzw))
#define vpMatrixPrev (mat4(ubo.vpMatrixVecPrev[0].xyzw, ubo.vpMatrixVecPrev[1].xyzw, ubo.vpMatrixVecPrev[2].xyzw, ubo.vpMatrixVecPrev[3].xyzw))

void main() 
{
	uvec2 mv = texture(samplerMV, inUV).xy;
    vec3 color = texture(samplerColor, inUV).xyz;
    float depth = float(texture(samplerDepth, inUV).x) / 16777215.0f;

    vec2 screenPos = vec2(2.0 * inUV.x - 1.0, 1.0 - 2.0 * inUV.y);

    mat4 viewTranslation = mat4(1.0);
    viewTranslation[3].xyz = ubo.vpMatrixVec[4].xyz;
    mat4 vp = vpMatrix * viewTranslation;

    mat4 viewTranslationPrev = mat4(1.0);
    viewTranslationPrev[3].xyz = ubo.vpMatrixVecPrev[4].xyz;
    mat4 vpPrev = vpMatrixPrev * viewTranslationPrev;

    if (mv == uvec2(0, 0)) {
        vec4 currentClipPos = vec4(screenPos, depth, 1.0);
        
        vec4 worldPos = inverse(vp) * currentClipPos;
        vec4 prevClipPos = vpPrev * worldPos;
        prevClipPos /= prevClipPos.w;

        outMV = (currentClipPos.xy - prevClipPos.xy) * vec2(-0.5, 0.5);
    } else {
        vec2 mv_n = vec2(mv) * vec2(1.525902189314365386962890625e-05 * 4.008016109466552734375) - vec2(2.0039775371551513671875);
        outMV = mv_n * vec2(-0.5, 0.5);
    }

    outColor = vec4(color, 1.0);
    gl_FragDepth = depth;
}