#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV1;       // UVs del Lightmap recibidas del vertex shader

// Push Constants idénticos al Vertex Shader (128 bytes)
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

// Dos Samplers independientes:
layout(set = 0, binding = 0) uniform sampler2D uTexture;  // Set 0: Atlas del terreno / Ítems
layout(set = 1, binding = 0) uniform sampler2D uLightmap; // Set 1: Textura del Lightmap 16x16

layout(location = 0) out vec4 outColor;

void main() {
    // 1. Muestreo de la textura base (Atlas)
    vec4 texColor = vec4(1.0);
    if (pc.uHasTexture != 0) {
        texColor = texture(uTexture, inUV);
    }

    // 2. Alpha Test clásico de Minecraft (0.1)
    // Recorta el borde transparente de hojas, flores, antorchas y hierba alta,
    // permitiendo pasar al agua (alpha ~0.6).
    if (pc.uHasTexture != 0 && texColor.a < 0.1) {
        discard;
    }

    // 3. Multiplicación base: Textura * Color de vértice (Sombreado de caras y tinte de bioma)
    vec4 c = texColor * inColor;

    // 4. ¡LA ILUMINACIÓN REAL!
    // Si el Lightmap está activo, multiplicamos por la muestra de la textura 16x16.
    // En una cueva profunda: inUV1 apunta a negro -> el bloque se oscurece.
    // Cerca de una antorcha: inUV1 apunta a luz cálida -> el bloque se ilumina.
    if (pc.uUseLightmap != 0) {
        vec3 lightColor = texture(uLightmap, inUV1).rgb;
        c.rgb *= lightColor;
    }

    // 5. Corrección Gamma de Minecraft
    // Evita que los tonos oscuros se aplasten y le da el look auténtico de consola
    if (pc.uInvGamma > 0.0) {
        c.rgb = pow(c.rgb, vec3(pc.uInvGamma));
    }

    outColor = c;
}