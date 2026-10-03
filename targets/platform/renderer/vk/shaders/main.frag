#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 uMVP;
    vec4 uBaseColor;
    vec3 uChunkOffset;
    int  uHasTexture;
} pc;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = vec4(1.0);
    if (pc.uHasTexture != 0) {
        texColor = texture(uTexture, inUV);
    }

    if (texColor.a < 0.05) {
        discard;
    }

    // Swizzle .abgr nativo de Minecraft / Little Endian
    vec4 vertColor = (inColor == vec4(0.0)) ? vec4(1.0) : inColor.abgr;
    outColor = texColor * vertColor * pc.uBaseColor;
}