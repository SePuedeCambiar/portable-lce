#pragma once
#include "gl3_loader.h"
#include <cstdint>
#include "platform/renderer/IPlatformRenderer.h"

// Esto soluciona el "Infierno de Nombres": 
// Le dice al compilador que ImageInfo y D3DXIMAGE_INFO son lo mismo.
typedef D3DXIMAGE_INFO ImageInfo;

extern IPlatformRenderer& PlatformRenderer;

class GLRenderer : public IPlatformRenderer {
public:
    // Core & Lifecycle
    void Initialise();
    void InitialiseContext();
    void Shutdown() override; // CORREGIDO: era voidL, ahora es void y tiene override
    void Close();
    bool ShouldClose();
    void Tick() override;
    void Suspend() override;
    bool Suspended() override;
    void Resume() override;

    // Window
    void SetWindowSize(int w, int h) override;
    void SetFullscreen(bool fs) override;
    void StartFrame() override;
    void Present() override;
    void GetFramebufferSize(int& width, int& height) override;

    // Render State
    void DrawVertices(ePrimitiveType PrimitiveType, int count, void* dataIn, eVertexType vType, ePixelShaderType psType) override;

    void Clear(int flags) override;
    void SetClearColour(const float colourRGBA[4]) override;
    void SetChunkOffset(float x, float y, float z) override;
    void StateSetColour(float r, float g, float b, float a) override;
    void StateSetDepthMask(bool enable) override;
    void StateSetBlendEnable(bool enable) override;
    void StateSetBlendFunc(int src, int dst) override;
    void StateSetBlendFactor(unsigned int colour) override;
    void StateSetAlphaFunc(int func, float param) override;
    void StateSetDepthFunc(int func) override;
    void StateSetFaceCull(bool enable) override;
    void StateSetFaceCullCW(bool enable) override;
    void StateSetLineWidth(float width) override;
    void StateSetWriteEnable(bool red, bool green, bool blue, bool alpha) override;
    void StateSetDepthTestEnable(bool enable) override;
    void StateSetAlphaTestEnable(bool enable) override;
    void StateSetDepthSlopeAndBias(float slope, float bias) override;
    void StateSetFogEnable(bool enable) override;
    void StateSetFogMode(int mode) override;
    void StateSetFogNearDistance(float dist) override;
    void StateSetFogFarDistance(float dist) override;
    void StateSetFogDensity(float density) override;
    void StateSetFogColour(float red, float green, float blue) override;
    void StateSetLightingEnable(bool enable) override;
    void StateSetVertexTextureUV(float u, float v) override;
    void StateSetLightColour(int light, float red, float green, float blue) override;
    void StateSetLightAmbientColour(float red, float green, float blue) override;
    void StateSetLightDirection(int light, float x, float y, float z) override;
    void StateSetLightEnable(int light, bool enable) override;
    void StateSetViewport(eViewportType viewportType) override;
    void StateSetEnableViewportClipPlanes(bool enable) override;
    void StateSetTexGenCol(int col, float x, float y, float z, float w, bool eyeSpace) override;
    void StateSetStencil(int Function, std::uint8_t stencil_ref, std::uint8_t stencil_func_mask, std::uint8_t stencil_write_mask) override;
    void StateSetForceLOD(int LOD) override;
    void StateSetTextureEnable(bool enable) override;
    void StateSetActiveTexture(int tex) override;


    // Textures
    int TextureCreate() override;
    void TextureFree(int idx) override;
    void TextureBind(int idx) override;
    void TextureBindVertex(int idx, bool scaleLight = false) override;
    void TextureSetTextureLevels(int levels) override;
    int TextureGetTextureLevels() override;
    void TextureData(int width, int height, void* data, int level, eTextureFormat format) override;
    void TextureDataUpdate(int xoffset, int yoffset, int width, int height, void* data, int level) override;
    void TextureSetParam(int param, int value) override;
    void TextureDynamicUpdateStart() override;
    void TextureDynamicUpdateEnd() override;
    int LoadTextureData(const char* szFilename, D3DXIMAGE_INFO* pSrcInfo, int** ppDataOut) override;
    int LoadTextureData(std::uint8_t* pbData, std::uint32_t byteCount, D3DXIMAGE_INFO* pSrcInfo, int** ppDataOut) override;
    int SaveTextureData(const char* szFilename, D3DXIMAGE_INFO* pSrcInfo, int* ppDataOut) override;
    int SaveTextureDataToMemory(void* pOutput, int outputCapacity, int* outputLength, int width, int height, int* ppDataIn) override;
    void ReadPixels(int x, int y, int w, int h, void* buf) override;
    void TextureGetStats() override;
    void* TextureGetTexture(int idx) override;
    
    void SetAtlasSize(int width, int height) override; 

    // Command Buffers
    int CBuffCreate(int count) override;
    void CBuffDelete(int first, int count) override;
    void CBuffDeleteAll() override;
    void CBuffStart(int index, bool full) override;
    void CBuffClear(int index) override;
    void CBuffEnd() override;
    bool CBuffCall(int index, bool full) override;
    void CBuffTick() override;
    void CBuffDeferredModeStart() override;
    void CBuffDeferredModeEnd() override;
    void flushIggyCache() override;
    int CBuffSize(int index) override;
    void CBuffLockStaticCreations() override;

    // Capturing & Events
    void DoScreenGrabOnNextPresent() override;
    void CaptureThumbnail(ImageFileBuffer* pngOut) override;
    void CaptureScreen(ImageFileBuffer* jpgOut, void* previewOut) override;
    void BeginConditionalSurvey(int identifier) override;
    void EndConditionalSurvey() override;
    void BeginConditionalRendering(int identifier) override;
    void EndConditionalRendering() override;
    void BeginEvent(const char* eventName) override;
    void EndEvent() override;

    // Matrix
    void MatrixMode(int type) override;
    void MatrixSetIdentity() override;
    void MatrixTranslate(float x, float y, float z) override;
    void MatrixRotate(float angle, float x, float y, float z) override;
    void MatrixScale(float x, float y, float z) override;
    void MatrixPerspective(float fovy, float aspect, float zNear, float zFar) override;
    void MatrixOrthogonal(float left, float right, float bottom, float top, float zNear, float zFar) override;
    void MatrixPop() override;
    void MatrixPush() override;
    void MatrixMult(float* mat) override;
    const float* MatrixGet(int type) override;
    void Set_matrixDirty() override;

    void UpdateGamma(unsigned short usGamma) override;
    bool IsWidescreen() override;
    bool IsHiDef() override;
    // GetFramebufferSize ya estaba declarado arriba, se eliminó la duplicada de aquí.
};
