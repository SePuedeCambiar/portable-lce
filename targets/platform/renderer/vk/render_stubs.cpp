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
    for (int i = 0; i < n; i++) {
        textures[i] = (unsigned int)PlatformRenderer.TextureCreate();
    }
}

void glDeleteTextures_4J(int id) {
    PlatformRenderer.TextureFree(id);
}

void glDeleteTextures_4J(int n, const unsigned int* textures) {
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

void glLight_4J(int light, int pname, FloatBuffer* params) {
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
    if (pname == 0x0B53) {
        const float* p = params->_getDataPointer();
        PlatformRenderer.StateSetLightAmbientColour(p[0], p[1], p[2]);
    }
}

void glFog_4J(int pname, FloatBuffer* params) {
    const float* p = params->_getDataPointer();
    if (pname == 0x0B66) {
        PlatformRenderer.StateSetFogColour(p[0], p[1], p[2]);
    }
}

void glGetFloat_4J(int pname, FloatBuffer* params) {
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

extern "C" void glBindTexture(unsigned int target, unsigned int texture) {
    PlatformRenderer.TextureBind((int)texture);
    PlatformRenderer.StateSetTextureEnable(texture != 0);
}

extern "C" void glEnable(unsigned int cap) {
    if (cap == 0x0DE1) { // GL_TEXTURE_2D
        PlatformRenderer.StateSetTextureEnable(true);
    }
}

extern "C" void glDisable(unsigned int cap) {
    if (cap == 0x0DE1) { // GL_TEXTURE_2D
        PlatformRenderer.StateSetTextureEnable(false);
    }
}
