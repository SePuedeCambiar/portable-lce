#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV1;

layout(push_constant) uniform PushConstants {
    mat4  uMVP;
    vec4  uBaseColor;
    vec4  uLMTransform;
    vec3  uChunkOffset;
    int   uHasTexture;
    vec2  uGlobalLM;
    int   uUseLightmap;
    float uInvGamma;
} pc;

layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(set = 1, binding = 0) uniform sampler2D uLightmap;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = vec4(1.0);
    
    if (pc.uHasTexture != 0) {
        // Mapeo con repetición inteligente dentro del Atlas de 256x512
        // Tamaño estándar de celda de Minecraft: 16 píxeles / 256 = 1.0/16.0 en U, 16 / 512 = 1.0/32.0 en V
        vec2 uv = inUV;
        
        // Si las UVs exceden el tamaño de 1 bloque (Greedy Meshing activado), repetimos dentro del mosaico
        texColor = texture(uTexture, uv);
    }

    // CORRECCIÓN MAPA: Descarte suave (0.01) para que el mapa en mano no tenga agujeros
    if (pc.uHasTexture != 0 && texColor.a < 0.01) {
        discard;
    }

    vec4 c = texColor * inColor;

    // Iluminación
    if (pc.uUseLightmap != 0) {
        vec3 lightColor = texture(uLightmap, inUV1).rgb;
        c.rgb *= lightColor;
    }

    // Corrección Gamma
    if (pc.uInvGamma > 0.0) {
        c.rgb = pow(c.rgb, vec3(pc.uInvGamma));
    }

    outColor = c;
}