R"GLSL(
#version 330 core
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform int   uUseTexture;
uniform int   uUseLightmap;
uniform float uAlphaRef;
uniform vec4  uFogColor;
uniform int   uFogEnable;
uniform float uInvGamma;
uniform vec2  uCellSize; 

in  vec2  vUV0;
in  vec2  vUV1;
in  vec4  vColor;
in  float vFogFactor;
out vec4  oColor;

void main() {
    // Decodificación
    float packU = floor(vUV0.x / 100.0);
    float packV = floor(vUV0.y / 100.0);
    vec2 actualUV = vec2(mod(vUV0.x, 100.0), mod(vUV0.y, 100.0));
    vec2 finalUV = actualUV;
    
    // Obtenemos derivadas antes del tiling para no romper el mipmapping
    vec2 dx = dFdx(actualUV);
    vec2 dy = dFdy(actualUV);

    // Si packU y packV son > 0, indica que la celda es Greedy
    if (packU > 0.0 || packV > 0.0) {
        vec2 offset = vec2((packU - 1.0) / 16.0, (packV - 1.0) / 32.0);
        
        vec2 uvCellSize = vec2(1.0 / 16.0, 1.0 / 32.0);
        if (uCellSize.x > 0.0) {
            uvCellSize = vec2(1.0 / 16.0, (uCellSize.x / 16.0) / uCellSize.y);
        }
        
        vec2 localUV = actualUV - offset;
        vec2 fracUV = fract(localUV / uvCellSize);
        fracUV = clamp(fracUV, 0.0001, 0.9999);
        
        finalUV = offset + (fracUV * uvCellSize);
    }

    vec4 texColor = (uUseTexture != 0) ? textureGrad(uTex0, finalUV, dx, dy) : vec4(1.0);
    
    if (texColor.a < uAlphaRef) discard;

    vec4 c = texColor * vColor;

    if (uUseLightmap != 0) {
        c.rgb *= texture(uTex1, vUV1).rgb;
    }

    if (uFogEnable != 0) {
        c.rgb = mix(uFogColor.rgb, c.rgb, vFogFactor);
    }

    c.rgb = pow(c.rgb, vec3(uInvGamma));
    oColor = c;
}
)GLSL"