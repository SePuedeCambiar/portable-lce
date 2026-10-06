#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 uMVP;
    vec4 uBaseColor;
    vec3 uChunkOffset;
    int  uHasTexture;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

void main() {
    vec4 worldPos = vec4(inPos + pc.uChunkOffset, 1.0);
    gl_Position = pc.uMVP * worldPos;

    outUV = inUV;

    // Si el vértice no tiene color (0,0,0,0), usamos uBaseColor.
    // Si tiene color, desempaquetamos Little-Endian .abgr de Minecraft
    if (inColor == vec4(0.0)) {
        outColor = pc.uBaseColor;
    } else {
        outColor = inColor.abgr;
    }
}