#include "Tesselator.h"

#include <vector>
#include <memory>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>

#include "minecraft/client/MemoryTracker.h"
#include "minecraft/util/Log.h"
#include "platform/renderer/renderer.h"
#include "platform/stubs.h"

bool Tesselator::TRIANGLE_MODE = false;
bool Tesselator::USE_VBO = false;

thread_local std::unique_ptr<Tesselator> Tesselator::m_tlsInstance;

Tesselator* Tesselator::getInstance() { 
    if (!m_tlsInstance) {
        CreateNewThreadStorage(2 * 1024 * 1024);
    }
    return m_tlsInstance.get(); 
}


void Tesselator::CreateNewThreadStorage(int bytes) {
    Tesselator::m_tlsInstance = std::unique_ptr<Tesselator>(new Tesselator(bytes / 4));
}

Tesselator::Tesselator(int size) {
    vertices = 0;
    hasColor = false;
    hasTexture = false;
    hasTexture2 = false;
    hasNormal = false;
    p = 0;
    count = 0;
    _noColor = false;
    tesselating = false;
    vboMode = false;
    vboId = 0;
    vboCounts = 10;

    u = v = 0;
    col = 0;
    mode = 0;
    xo = yo = zo = 0;
    xoo = yoo = zoo = 0;
    _normal = 0;

    useCompactFormat360 = false;
    mipmapEnable = true;
    useProjectedTexturePixelShader = false;

    this->size = size;
    _array.resize(size);

    vboMode = USE_VBO;
    if (vboMode) {
        vboIds = MemoryTracker::createIntBuffer(vboCounts);
        ARBVertexBufferObject::glGenBuffersARB(vboIds);
    }
}

Tesselator::~Tesselator() {
    if (vboMode && vboIds != nullptr) {
        glDeleteBuffers(vboCounts, (GLuint*)vboIds);
        free(vboIds); 
        vboIds = nullptr;
    }

    if (vboId != 0) {
        glDeleteBuffers(1, (GLuint*)&vboId);
        vboId = 0;
    }
}

Tesselator* Tesselator::getUniqueInstance(int size) {
    return new Tesselator(size);
}

void Tesselator::end() {
    tesselating = false;
    if (vertices > 0) {
        int vertexCount = vertices;

        // Selección automática del formato de vértice (16 bytes vs 32 bytes)
        IPlatformRenderer::eVertexType vertexType = useCompactFormat360
            ? IPlatformRenderer::VERTEX_TYPE_COMPRESSED
            : (useProjectedTexturePixelShader
                ? IPlatformRenderer::VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_TEXGEN
                : IPlatformRenderer::VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1);

        if (!useCompactFormat360 && !hasColor) {
            unsigned int* pColData = (unsigned int*)_array.data();
            pColData += 5;
            for (int i = 0; i < vertices; i++) {
                if ((size_t)(i * 8 + 5) < _array.size()) {
                    *pColData = 0x00000000;
                    pColData += 8;
                }
            }
        }

        IPlatformRenderer::ePrimitiveType primType = (mode == GL_QUADS && TRIANGLE_MODE)
            ? IPlatformRenderer::PRIMITIVE_TYPE_TRIANGLE_LIST
            : (IPlatformRenderer::ePrimitiveType)mode;

        PlatformRenderer.DrawVertices(
            primType, vertexCount,
            _array.data(),
            vertexType,
            useProjectedTexturePixelShader
                ? IPlatformRenderer::PIXEL_SHADER_TYPE_PROJECTION
                : IPlatformRenderer::PIXEL_SHADER_TYPE_STANDARD);
    }

    clear();
}

void Tesselator::clear() {
    vertices = 0;
    p = 0;
    count = 0;
}

void Tesselator::begin() {
    begin(GL_QUADS);
    bounds.reset();
}

void Tesselator::useProjectedTexture(bool enable) {
    useProjectedTexturePixelShader = enable;
}

void Tesselator::useCompactVertices(bool enable) {
    useCompactFormat360 = enable;
}

bool Tesselator::getCompactVertices() { 
    return useCompactFormat360; 
}

bool Tesselator::setMipmapEnable(bool enable) {
    bool prev = mipmapEnable;
    mipmapEnable = enable;
    return prev;
}

void Tesselator::begin(int mode) {
    tesselating = true;
    clear();
    this->mode = mode;
    hasNormal = false;
    hasColor = false;
    hasTexture = false;
    hasTexture2 = false;
    _noColor = false;
}

void Tesselator::tex(float u, float v) {
    hasTexture = true;
    this->u = u;
    this->v = v;
}

void Tesselator::tex2(int tex2) {
    hasTexture2 = true;
    this->_tex2 = tex2;
}

void Tesselator::color(float r, float g, float b) {
    color((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

void Tesselator::color(float r, float g, float b, float a) {
    color((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
}

void Tesselator::color(int r, int g, int b) { 
    color(r, g, b, 255); 
}

void Tesselator::color(int r, int g, int b, int a) {
    if (_noColor) return;

    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    a = std::clamp(a, 0, 255);

    hasColor = true;
    col = (r << 24) | (g << 16) | (b << 8) | a;
}

void Tesselator::color(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    color((int)r, (int)g, (int)b);
}

// ============================================================================
// VÉRTICE PRINCIPAL: SOPORTE DUAL (16 BYTES / 32 BYTES)
// ============================================================================
void Tesselator::vertexUV(float x, float y, float z, float u, float v) {
    count++;
    float uu = mipmapEnable ? u : (u + 1.0f);

    // --- PROTECCIÓN CONTRA DESBORDAMIENTO (Auto-crecimiento de RAM) ---
    if (p + 16 >= (int)_array.size()) {
        size_t newSize = std::max((size_t)_array.size() * 2, (size_t)p + 64);
        _array.resize(newSize);
        this->size = (int)newSize;
    }
    // ------------------------------------------------------------------

    if (useCompactFormat360) {
        // --- FORMATO COMPACTO DE 16 BYTES (Estilo Sodium / Xbox 360) ---
        // Empaquetamos directamente en memoria contigua alineada
        int16_t* p16 = reinterpret_cast<int16_t*>(&_array[p]);

        // 1. Posición Local (Offset 0..5, 6 bytes): Escalado x1024.0
        p16[0] = static_cast<int16_t>(std::round((x + xo) * 1024.0f));
        p16[1] = static_cast<int16_t>(std::round((y + yo) * 1024.0f));
        p16[2] = static_cast<int16_t>(std::round((z + zo) * 1024.0f));

        // 2. Color BGR565 (Offset 6..7, 2 bytes)
        if (hasColor) {
            uint8_t r = static_cast<uint8_t>((col >> 24) & 0xFF);
            uint8_t g = static_cast<uint8_t>((col >> 16) & 0xFF);
            uint8_t b = static_cast<uint8_t>((col >> 8) & 0xFF);
            uint16_t bgr565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            p16[3] = static_cast<int16_t>(static_cast<int32_t>(bgr565) - 32768);
        } else {
            // Blanco completo (0xFFFF en BGR565 con offset de consola)
            p16[3] = static_cast<int16_t>(65535 - 32768);
        }

        // 3. Coordenadas UV (Offset 8..11, 4 bytes): Escalado x8192.0
        p16[4] = static_cast<int16_t>(std::round(uu * 8192.0f));
        p16[5] = static_cast<int16_t>(std::round(v * 8192.0f));

        // 4. Lightmap Coordinates (Offset 12..15, 4 bytes)
        if (hasTexture2) {
            p16[6] = static_cast<int16_t>((_tex2 & 0xffff) + 8);
            p16[7] = static_cast<int16_t>(((_tex2 >> 16) & 0xffff) + 8);
        } else {
            p16[6] = -512;
            p16[7] = -512;
        }

        p += 4; // 4 palabras de 32 bits = 16 bytes exactos
    } else {
        // --- FORMATO ESTÁNDAR DE 32 BYTES (Legacy / Fallback) ---
        float* fdata = reinterpret_cast<float*>(&_array[p]);
        fdata[0] = x + xo;
        fdata[1] = y + yo;
        fdata[2] = z + zo;
        fdata[3] = uu;
        fdata[4] = v;

        _array[p + 5] = hasColor ? col : 0;
        _array[p + 6] = _normal;

        if (hasTexture2) {
            int16_t* pShort = reinterpret_cast<int16_t*>(&_array[p + 7]);
            pShort[0] = static_cast<int16_t>((_tex2 & 0xffff) + 8);
            pShort[1] = static_cast<int16_t>(((_tex2 >> 16) & 0xffff) + 8);
        } else {
            *reinterpret_cast<uint32_t*>(&_array[p + 7]) = 0xfe00fe00;
        }

        p += 8; // 8 palabras de 32 bits = 32 bytes
    }

    vertices++;

    // Despacho de seguridad si el buffer dinámico se llena
    int strideWords = useCompactFormat360 ? 4 : 8;
    if (vertices % 4 == 0 && p >= size - (strideWords * 4)) {
        end();
        tesselating = true;
    }
}

void Tesselator::vertex(float x, float y, float z) {
    vertexUV(x, y, z, u, v);
}

void Tesselator::color(int c) {
    color((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

void Tesselator::color(int c, int alpha) {
    color((c >> 16) & 255, (c >> 8) & 255, c & 255, alpha);
}

void Tesselator::noColor() { 
    _noColor = true; 
}

void Tesselator::normal(float x, float y, float z) {
    hasNormal = true;
    int8_t xx = static_cast<int8_t>(x * 127.0f);
    int8_t yy = static_cast<int8_t>(y * 127.0f);
    int8_t zz = static_cast<int8_t>(z * 127.0f);
    _normal = (static_cast<uint8_t>(xx)) |
              (static_cast<uint8_t>(yy) << 8) |
              (static_cast<uint8_t>(zz) << 16);
}

void Tesselator::offset(float xo, float yo, float zo) {
    this->xo = xo; this->yo = yo; this->zo = zo;
    this->xoo = xo; this->yoo = yo; this->zoo = zo;
}

void Tesselator::addOffset(float x, float y, float z) {
    xo += x; yo += y; zo += z;
}

bool Tesselator::hasMaxVertices() { 
    return false; 
}

void Tesselator::vertexGreedy(float x, float y, float z, float u, float v, float uOffset, float vOffset) {
    vertexUV(x, y, z, u + (uOffset * 10.0f), v + (vOffset * 10.0f));
}