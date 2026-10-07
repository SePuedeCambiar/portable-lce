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

    // Descarte estándar de Alpha Test de Minecraft (0.1)
    // Descarta el fondo invisible de la hierba alta, caña de azúcar, flores y hojas,
    // permitiendo que el agua translúcida (que tiene alpha ~0.6) pase sin ser recortada.
    if (pc.uHasTexture != 0 && texColor.a < 0.1) {
        discard;
    }

    outColor = texColor * inColor;
}