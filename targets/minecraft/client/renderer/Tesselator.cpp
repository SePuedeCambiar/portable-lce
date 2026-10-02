#include "Tesselator.h"

#include <vector>
#include <memory>
#include <cstring>
#include <cstdint>
#include <algorithm>

#include "minecraft/client/MemoryTracker.h"
#include "minecraft/util/Log.h"
#include "platform/renderer/renderer.h"
#include "platform/stubs.h"

bool Tesselator::TRIANGLE_MODE = false;
bool Tesselator::USE_VBO = false;

thread_local std::unique_ptr<Tesselator> Tesselator::m_tlsInstance;

Tesselator* Tesselator::getInstance() { 
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
        if (!hasColor) {
            unsigned int* pColData = (unsigned int*)_array.data();
            pColData += 5;
            for (int i = 0; i < vertices; i++) {
                *pColData = 0x00000000;
                pColData += 8;
            }
        }
        
        int vertexCount = vertices;
        if (mode == GL_QUADS && TRIANGLE_MODE) {
            PlatformRenderer.DrawVertices(
                IPlatformRenderer::PRIMITIVE_TYPE_TRIANGLE_LIST, vertices,
                _array.data(),
                IPlatformRenderer::VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1,
                useProjectedTexturePixelShader
                    ? IPlatformRenderer::PIXEL_SHADER_TYPE_PROJECTION
                    : IPlatformRenderer::PIXEL_SHADER_TYPE_STANDARD);
        } else {
            PlatformRenderer.DrawVertices(
                (IPlatformRenderer::ePrimitiveType)mode, vertexCount,
                _array.data(),
                useProjectedTexturePixelShader
                    ? IPlatformRenderer::VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_TEXGEN
                    : IPlatformRenderer::VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1,
                useProjectedTexturePixelShader
                    ? IPlatformRenderer::PIXEL_SHADER_TYPE_PROJECTION
                    : IPlatformRenderer::PIXEL_SHADER_TYPE_STANDARD);
        }
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

bool Tesselator::getCompactVertices() { return useCompactFormat360; }

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

void Tesselator::color(int r, int g, int b) { color(r, g, b, 255); }

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

void Tesselator::vertexUV(float x, float y, float z, float u, float v) {
    count++;
    float uu = mipmapEnable ? u : (u + 1.0f);

    float* fdata = reinterpret_cast<float*>(&_array[p]);
    fdata[0] = x + xo;
    fdata[1] = y + yo;
    fdata[2] = z + zo;
    fdata[3] = uu;
    fdata[4] = v;

    _array[p + 5] = hasColor ? col : 0;
    _array[p + 6] = _normal;

    // Corrección para Entidades, Protagonista y Cofres:
    if (hasTexture2) {
        int16_t* pShort = reinterpret_cast<int16_t*>(&_array[p + 7]);
        pShort[0] = static_cast<int16_t>((_tex2 & 0xffff) + 8);
        pShort[1] = static_cast<int16_t>(((_tex2 >> 16) & 0xffff) + 8);
    } else {
        // Centinela (-512, -512): Activa la luz global en el shader para entidades
        *reinterpret_cast<uint32_t*>(&_array[p + 7]) = 0xfe00fe00;
    }

    p += 8;
    vertices++;

    if (vertices % 4 == 0 && p >= size - 32) {
        end();
        tesselating = true;
    }
}

void Tesselator::vertex(float x, float y, float z) {
    count++;
    float uu = mipmapEnable ? u : (u + 1.0f);

    float* fdata = reinterpret_cast<float*>(&_array[p]);
    fdata[0] = x + xo;
    fdata[1] = y + yo;
    fdata[2] = z + zo;
    fdata[3] = uu;
    fdata[4] = v;

    _array[p + 5] = hasColor ? col : 0;
    _array[p + 6] = _normal;

    // Corrección para Entidades, Protagonista y Cofres:
    if (hasTexture2) {
        int16_t* pShort = reinterpret_cast<int16_t*>(&_array[p + 7]);
        pShort[0] = static_cast<int16_t>((_tex2 & 0xffff) + 8);
        pShort[1] = static_cast<int16_t>(((_tex2 >> 16) & 0xffff) + 8);
    } else {
        // Centinela (-512, -512): Activa la luz global en el shader para entidades
        *reinterpret_cast<uint32_t*>(&_array[p + 7]) = 0xfe00fe00;
    }

    p += 8;
    vertices++;

    if (vertices % 4 == 0 && p >= size - 32) {
        end();
        tesselating = true;
    }
}

void Tesselator::color(int c) {
    color((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

void Tesselator::color(int c, int alpha) {
    color((c >> 16) & 255, (c >> 8) & 255, c & 255, alpha);
}

void Tesselator::noColor() { _noColor = true; }

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

bool Tesselator::hasMaxVertices() { return false; }

void Tesselator::vertexGreedy(float x, float y, float z, float u, float v, float uOffset, float vOffset) {
    vertexUV(x, y, z, u + (uOffset * 10.0f), v + (vOffset * 10.0f));
}