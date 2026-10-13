#version 450

// Atributos de entrada de 32 bytes (coinciden exactamente con Tesselator / Chunk)
layout(location = 0) in vec3  inPos;      // Bytes 0..11  (Posición X, Y, Z)
layout(location = 1) in vec2  inUV;       // Bytes 12..19 (Coordenadas UV del Atlas)
layout(location = 2) in vec4  inColor;    // Bytes 20..23 (Color / Sombreado de caras)
layout(location = 3) in vec3  inNormal;   // Bytes 24..27 (Normales para entidades)
layout(location = 4) in ivec2 inLMraw;    // Bytes 28..31 (¡Luz cruda de bloque y cielo!)

// Push Constants perfectamente alineados a 128 bytes (límite universal de Vulkan)
layout(push_constant) uniform PushConstants {
    mat4  uMVP;           // Offset 0   (64 bytes)
    vec4  uBaseColor;     // Offset 64  (16 bytes)
    vec4  uLMTransform;   // Offset 80  (16 bytes) - Escala y offset de medio texel (8/256)
    vec3  uChunkOffset;   // Offset 96  (12 bytes)
    int   uHasTexture;    // Offset 108 (4 bytes)
    vec2  uGlobalLM;      // Offset 112 (8 bytes)  - Luz global para entidades/items
    int   uUseLightmap;   // Offset 120 (4 bytes)  - 1 si está activo, 0 si no
    float uInvGamma;      // Offset 124 (4 bytes)  - Corrección gamma
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec2 outUV1;     // Coordenadas calculadas para el Lightmap 16x16

void main() {
    // 1. Posición en el mundo y espacio de recorte
    vec4 worldPos = vec4(inPos + pc.uChunkOffset, 1.0);
    gl_Position   = pc.uMVP * worldPos;

    outUV = inUV;

    // 2. Color y Sombreado Direccional
    // Si el vértice no tiene color (vec4(0.0)), usa uBaseColor.
    // Si tiene color, aplicamos el swizzle .abgr Little-Endian nativo de Minecraft.
    if (inColor == vec4(0.0)) {
        outColor = pc.uBaseColor;
    } else {
        outColor = inColor.abgr;
    }

    // 3. Cálculo de Coordenadas del Lightmap (100% fiel al motor original)
    vec2 lmRawF = vec2(inLMraw);

    // Si los valores son > 2.0 (como 0..240 enviados por Tesselator), normalizamos a 0.0..1.0 dividiendo por 255.0
    vec2 normalizedLM = (lmRawF.x > 2.0 || lmRawF.y > 2.0) ? (lmRawF / 255.0) : lmRawF;

    // Si el valor es el centinela -512 (menor a -0.5), se trata de una entidad o ítem sin luz propia:
    // usamos la luz global enviada por StateSetVertexTextureUV (pc.uGlobalLM)
    vec2 lm = (lmRawF.x < -0.5) ? pc.uGlobalLM : normalizedLM;

    // Aplicamos la corrección de medio texel (8/256) para muestrear el centro exacto del píxel de luz
    outUV1 = lm * pc.uLMTransform.xy + pc.uLMTransform.zw;
}