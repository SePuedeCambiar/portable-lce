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

// ============================================================================
// SET 2: DATOS GLOBALES DE ESCENA (NIEBLA Y CÁMARA)
// ============================================================================
layout(std140, set = 2, binding = 0) uniform SceneData {
    mat4  uViewProj;
    vec4  uFogColor;
    vec4  uFogParams;     // X = Start, Y = End, Z = Density, W = Mode
    vec4  uSunDirection;
    vec4  uSkyColor;
    int   uFogEnabled;
    float uGameTime;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = vec4(1.0);
    
    if (pc.uHasTexture != 0) {
        // Mapeo con repetición inteligente dentro del Atlas de 256x512
        vec2 uv = inUV;
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

    // ========================================================================
    // CÁLCULO DE NIEBLA DE MINECRAFT
    // ========================================================================
    if (ubo.uFogParams.y > ubo.uFogParams.x) {    // En proyecciones de perspectiva, 1.0 / gl_FragCoord.w es la distancia lineal a la cámara
        float dist = 1.0 / gl_FragCoord.w;

        // Factor lineal: 1.0 = color original, 0.0 = color de niebla puro
        float fogFactor = clamp((ubo.uFogParams.y - dist) / (ubo.uFogParams.y - ubo.uFogParams.x), 0.0, 1.0);

        // Mezclar el color final con la niebla
        c.rgb = mix(ubo.uFogColor.rgb, c.rgb, fogFactor);
    }

    outColor = c;
}