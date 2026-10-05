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

    // Solo descartar si es realmente un recorte transparente (como hojas o cristal)
    if (pc.uHasTexture != 0 && texColor.a < 0.05) {
        discard;
    }

    // Si la textura es negra o no tiene luz pero el vértice tiene color, usamos el color del bloque
    vec4 c = texColor * inColor;
    if (c.rgb == vec3(0.0) && inColor.rgb != vec3(0.0)) {
        c.rgb = inColor.rgb;
    }

    outColor = vec4(c.rgb, 1.0);
}
