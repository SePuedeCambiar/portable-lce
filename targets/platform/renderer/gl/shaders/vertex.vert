R"GLSL(
#version 330 core
layout(location=0) in vec3  aPos;
layout(location=1) in vec2  aUV0;
layout(location=2) in vec4  aColor;
layout(location=3) in vec3  aNormal;
layout(location=4) in ivec2 aLMraw;

uniform mat4  uMVP;
uniform mat4  uMV;
uniform mat3  uNormalMatrix;
uniform float uNormalSign;
uniform mat4  uTexMat0;
uniform vec4  uBaseColor;
uniform int   uLighting;
uniform vec3  uLight0Dir;
uniform vec3  uLight1Dir;
uniform vec3  uLightDiffuse;
uniform vec3  uLightAmbient;
uniform vec3  uChunkOffset;
uniform int   uFogMode;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform vec4  uLMTransform;
uniform vec2  uGlobalLM;
uniform int   uGreedyMode; // NUEVO: Determina si el vértice está compactado (1) o estándar (0)

out vec2  vUV0;
out vec2  vUV1;
out vec4  vColor;
out float vFogFactor;

void main() {
    vec3 decompressedPos;
    vec2 decompressedUV;
    vec4 decompressedColor;

    if (uGreedyMode == 1) {
        // --- DESCOMPRESIÓN DE VÉRTICES DE 16 BYTES EN GPU (Estilo Sodium) ---
        decompressedPos = aPos / 1024.0;
        decompressedUV  = aUV0 / 8192.0;

        // Desempaquetar Color BGR565 de 16 bits nativo de consola
        // aColor.x contiene el short con offset de consola (-32768)
        int packedColorVal = int(aColor.x) + 32768;
        float b = float(packedColorVal & 0x1F) / 31.0;
        float g = float((packedColorVal >> 5) & 0x3F) / 63.0;
        float r = float((packedColorVal >> 11) & 0x1F) / 31.0;
        
        // Empaquetamos en el orden raw esperado por la lógica .abgr de Minecraft
        decompressedColor = vec4(1.0, b, g, r);
    } else {
        // --- FORMATO ESTÁNDAR DE 32 BYTES ---
        decompressedPos = aPos;
        decompressedUV  = aUV0;
        decompressedColor = aColor;
    }

    vec4 aPos4   = vec4(decompressedPos + uChunkOffset, 1.0);
    vec4 eyePos  = uMV  * aPos4;
    gl_Position  = uMVP * aPos4;
    
    // Extracción segura del empaquetado Greedy
    float packU = floor((decompressedUV.x + 0.001) / 10.0);
    float packV = floor((decompressedUV.y + 0.001) / 10.0);
    
    if (packU > 0.0 && packV > 0.0) {
        vUV0 = decompressedUV; 
    } else {
        vUV0 = (uTexMat0 * vec4(decompressedUV, 0.0, 1.0)).xy; 
    }

    // Mapa de luz estándar 100% nativo
    vec2 aLMrawF = vec2(aLMraw);
    vec2 normalizedLM = (aLMrawF.x > 2.0 || aLMrawF.y > 2.0) ? (aLMrawF / 255.0) : aLMrawF;
    vec2 lm = (aLMrawF.x < -0.5) ? uGlobalLM : normalizedLM;
    vUV1 = lm * uLMTransform.xy + uLMTransform.zw;

    bool sentinel = (decompressedColor == vec4(0.0));
    vec4 col = sentinel ? uBaseColor : decompressedColor.abgr;
    if (uLighting == 1) {
        // La iluminación dinámica solo se aplica a vértices estándar con normales
        vec3 n = normalize(uNormalMatrix * aNormal) * uNormalSign;
        float d0 = max(dot(n, uLight0Dir), 0.0);
        float d1 = max(dot(n, uLight1Dir), 0.0);
        vColor = vec4(col.rgb * (uLightAmbient + uLightDiffuse * clamp(d0 + d1, 0.0, 1.0)), col.a);
    } else {
        vColor = col;
    }

    float eDist = length(eyePos.xyz);
    if      (uFogMode == 1) vFogFactor = clamp((uFogEnd - eDist) / max(uFogEnd - uFogStart, 1e-4), 0.0, 1.0);
    else if (uFogMode == 2) vFogFactor = clamp(exp(-uFogDensity * eDist), 0.0, 1.0);
    else if (uFogMode == 3) { float d = uFogDensity * eDist; vFogFactor = clamp(exp(-d*d), 0.0, 1.0); }
    else                    vFogFactor = 1.0;
}

)GLSL"