#include "platform/renderer/renderer.h"
#include "java/IntBuffer.h"
#include "java/FloatBuffer.h"
#include "java/ByteBuffer.h"
#include <cstring>

inline int* getIntPtr(IntBuffer* buf) {
    return buf ? (int*)buf->getBuffer() + buf->position() : nullptr;
}

inline void* getBytePtr(ByteBuffer* buf) {
    return buf ? (char*)buf->getBuffer() + buf->position() : nullptr;
}

int glGenTextures_4J() {
    return PlatformRenderer.TextureCreate();
}

void glGenTextures_4J(int n, unsigned int* textures) {
    if (!textures) return;
    for (int i = 0; i < n; i++) {
        textures[i] = (unsigned int)PlatformRenderer.TextureCreate();
    }
}

void glDeleteTextures_4J(int id) {
    PlatformRenderer.TextureFree(id);
}

void glDeleteTextures_4J(int n, const unsigned int* textures) {
    if (!textures) return;
    for (int i = 0; i < n; i++) {
        PlatformRenderer.TextureFree((int)textures[i]);
    }
}

void glGenTextures_4J(IntBuffer* buf) {
    if (!buf) return;
    int n = buf->limit() - buf->position();
    int* dst = getIntPtr(buf);
    for (int i = 0; i < n; i++) {
        dst[i] = PlatformRenderer.TextureCreate();
    }
}

void glDeleteTextures_4J(IntBuffer* buf) {
    if (!buf) return;
    int n = buf->limit() - buf->position();
    int* src = getIntPtr(buf);
    for (int i = 0; i < n; i++) {
        PlatformRenderer.TextureFree(src[i]);
    }
}

void glTexImage2D_4J(int target, int level, int internalformat, int width,
                     int height, int border, int format, int type,
                     ByteBuffer* pixels) {
    (void)target; (void)internalformat; (void)border; (void)format; (void)type;
    PlatformRenderer.TextureData(width, height, getBytePtr(pixels), level,
                                 IPlatformRenderer::TEXTURE_FORMAT_RxGyBzAw);
}

void glTexSubImage2D_4J(int target, int level, int xoffset, int yoffset,
                        int width, int height, int format, int type,
                        ByteBuffer* pixels) {
    (void)target; (void)format; (void)type;
    PlatformRenderer.TextureDataUpdate(xoffset, yoffset, width, height, getBytePtr(pixels), level);
}

void glLight_4J(int light, int pname, FloatBuffer* params) {
    if (!params) return;
    const float* p = params->_getDataPointer();
    int idx = (light == 0x4001) ? 1 : 0;
    if (pname == 0x1203) {
        PlatformRenderer.StateSetLightDirection(idx, p[0], p[1], p[2]);
    } else if (pname == 0x1201) {
        PlatformRenderer.StateSetLightColour(idx, p[0], p[1], p[2]);
    } else if (pname == 0x1200) {
        PlatformRenderer.StateSetLightAmbientColour(p[0], p[1], p[2]);
    }
}

void glLightModel_4J(int pname, FloatBuffer* params) {
    if (!params) return;
    if (pname == 0x0B53) {
        const float* p = params->_getDataPointer();
        PlatformRenderer.StateSetLightAmbientColour(p[0], p[1], p[2]);
    }
}

void glFog_4J(int pname, FloatBuffer* params) {
    if (!params) return;
    const float* p = params->_getDataPointer();
    if (pname == 0x0B66) {
        PlatformRenderer.StateSetFogColour(p[0], p[1], p[2]);
    }
}

void glGetFloat_4J(int pname, FloatBuffer* params) {
    if (!params) return;
    const float* m = PlatformRenderer.MatrixGet(pname);
    if (m) {
        memcpy(params->_getDataPointer(), m, 16 * sizeof(float));
    }
}

void glGetFloat(int pname, FloatBuffer* params) {
    glGetFloat_4J(pname, params);
}

void glCallLists_4J(IntBuffer* lists) {
    if (!lists) return;
    int count = lists->limit() - lists->position();
    int* ids = getIntPtr(lists);
    for (int i = 0; i < count; i++) {
        (void)PlatformRenderer.CBuffCall(ids[i], false);
    }
}

void glReadPixels_4J(int x, int y, int w, int h, int f, int t, ByteBuffer* p) {
    (void)f; (void)t;
    PlatformRenderer.ReadPixels(x, y, w, h, getBytePtr(p));
}

void glTexCoordPointer_4J(int, int, FloatBuffer*) {}
void glNormalPointer_4J(int, ByteBuffer*) {}
void glColorPointer_4J(int, bool, int, ByteBuffer*) {}
void glVertexPointer_4J(int, int, FloatBuffer*) {}
void glEndList_4J(int) {}
void glTexGen_4J(int, int, FloatBuffer*) {}

// Registro de unidad activa: 0 = GL_TEXTURE0 (Atlas de Bloques), 1 = GL_TEXTURE1 (Lightmap)
static int s_currentActiveTextureUnit = 0;

// ============================================================================
// INTERCEPCIONES C DIRECTAS DE OPENGL A VULKAN PLATFORM RENDERER
// ============================================================================
extern "C" {

void glActiveTexture(unsigned int texture) {
    // GL_TEXTURE0 = 0x84C0 (o índice 0), GL_TEXTURE1 = 0x84C1 (o índice 1)
    s_currentActiveTextureUnit = (texture == 0x84C1 || texture == 1) ? 1 : 0;
    PlatformRenderer.StateSetActiveTexture(s_currentActiveTextureUnit);
}

void glClientActiveTexture(unsigned int texture) {
    (void)texture;
}

void glBindTexture(unsigned int target, unsigned int texture) {
    (void)target;
    if (s_currentActiveTextureUnit == 1) {
        // Unidad 1: Lightmap dinámico (antorchas y ciclo día/noche)
        PlatformRenderer.TextureBindVertex((int)texture);
    } else {
        // Unidad 0: Atlas de bloques (terrain.png), texturas de mobs, cielo, GUI
        PlatformRenderer.TextureBind((int)texture);
        PlatformRenderer.StateSetTextureEnable(texture != 0);
    }
}

void glTexSubImage2D(unsigned int target, int level, int xoffset, int yoffset,
                     int width, int height, unsigned int format, unsigned int type,
                     const void* pixels) {
    (void)target; (void)format; (void)type;
    PlatformRenderer.TextureDataUpdate(xoffset, yoffset, width, height, (void*)pixels, level);
}

void glDeleteTextures(int n, const unsigned int* textures) {
    if (!textures) return;
    for (int i = 0; i < n; i++) {
        PlatformRenderer.TextureFree((int)textures[i]);
    }
}

void glEnable(unsigned int cap) {
    switch (cap) {
        case 0x0DE1: // GL_TEXTURE_2D
            if (s_currentActiveTextureUnit == 0) {
                PlatformRenderer.StateSetTextureEnable(true);
            }
            break;
        case 0x0B71: // GL_DEPTH_TEST
            PlatformRenderer.StateSetDepthTestEnable(true);
            break;
        case 0x0BE2: // GL_BLEND
            PlatformRenderer.StateSetBlendEnable(true);
            break;
        case 0x0B44: // GL_CULL_FACE
            PlatformRenderer.StateSetFaceCull(true);
            break;
        case 0x0B50: // GL_LIGHTING
            PlatformRenderer.StateSetLightingEnable(true);
            break;
        case 0x0B60: // GL_FOG
            PlatformRenderer.StateSetFogEnable(true);
            break;
        case 0x0BC0: // GL_ALPHA_TEST
            PlatformRenderer.StateSetAlphaTestEnable(true);
            break;
        default:
            break;
    }
}

void glDisable(unsigned int cap) {
    switch (cap) {
        case 0x0DE1: // GL_TEXTURE_2D
            // CORRECCIÓN SPRINT 1: Solo apagar texturas si se pide en la Unidad 0.
            // Si Minecraft apaga GL_TEXTURE_2D en la Unidad 1 (Lightmap), el terreno no se apaga.
            if (s_currentActiveTextureUnit == 0) {
                PlatformRenderer.StateSetTextureEnable(false);
            }
            break;
        case 0x0B71: // GL_DEPTH_TEST
            PlatformRenderer.StateSetDepthTestEnable(false);
            break;
        case 0x0BE2: // GL_BLEND
            PlatformRenderer.StateSetBlendEnable(false);
            break;
        case 0x0B44: // GL_CULL_FACE
            PlatformRenderer.StateSetFaceCull(false);
            break;
        case 0x0B50: // GL_LIGHTING
            PlatformRenderer.StateSetLightingEnable(false);
            break;
        case 0x0B60: // GL_FOG
            PlatformRenderer.StateSetFogEnable(false);
            break;
        case 0x0BC0: // GL_ALPHA_TEST
            PlatformRenderer.StateSetAlphaTestEnable(false);
            break;
        default:
            break;
    }
}

void glDepthMask(unsigned char flag) {
    PlatformRenderer.StateSetDepthMask(flag != 0);
}

void glDepthFunc(unsigned int func) {
    PlatformRenderer.StateSetDepthFunc((int)func);
}

void glBlendFunc(unsigned int sfactor, unsigned int dfactor) {
    PlatformRenderer.StateSetBlendFunc((int)sfactor, (int)dfactor);
}

void glClear(unsigned int mask) {
    PlatformRenderer.Clear((int)mask);
}

void glClearColor(float red, float green, float blue, float alpha) {
    float c[4] = {red, green, blue, alpha};
    PlatformRenderer.SetClearColour(c);
}

void glAlphaFunc(unsigned int func, float ref) {
    PlatformRenderer.StateSetAlphaFunc((int)func, ref);
}

void glPolygonOffset(float factor, float units) {
    PlatformRenderer.StateSetDepthSlopeAndBias(factor, units);
}

void glFrontFace(unsigned int mode) {
    // GL_CW = 0x0900, GL_CCW = 0x0901
    PlatformRenderer.StateSetFaceCullCW(mode == 0x0900);
}

void glColorMask(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha) {
    PlatformRenderer.StateSetWriteEnable(red != 0, green != 0, blue != 0, alpha != 0);
}

void glColor4f(float red, float green, float blue, float alpha) {
    PlatformRenderer.StateSetColour(red, green, blue, alpha);
}

void glColor3f(float red, float green, float blue) {
    PlatformRenderer.StateSetColour(red, green, blue, 1.0f);
}

void glColor4ub(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha) {
    PlatformRenderer.StateSetColour(red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f);
}

void glColor3ub(unsigned char red, unsigned char green, unsigned char blue) {
    PlatformRenderer.StateSetColour(red / 255.0f, green / 255.0f, blue / 255.0f, 1.0f);
}

} // extern "C"