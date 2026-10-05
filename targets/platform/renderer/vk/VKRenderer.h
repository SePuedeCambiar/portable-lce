#pragma once

#include "platform/renderer/renderer.h"
#include "VKCommon.h"
#include <SDL.h>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include "glm/glm.hpp"

struct VKTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
};

struct VKChunkDrawCall {
    int prim;
    int first;
    int count;
    bool wasQuad;
};

struct VKChunkBuffer {
    VkBuffer vbo = VK_NULL_HANDLE;
    VkDeviceMemory vboMemory = VK_NULL_HANDLE;
    VkDeviceSize bufferSize = 0;
    std::vector<VKChunkDrawCall> draws;
    std::vector<uint8_t> rawVerts;
    bool valid = false;
    bool vboReady = false;
    bool isCompressed = false;

    void destroy(VkDevice device);
};

class VKRenderer : public IPlatformRenderer {
public:
    VKRenderer();
    virtual ~VKRenderer() override;

    virtual void Initialise() override;
    virtual void InitialiseContext() override;
    virtual void Tick() override;
    virtual void StartFrame() override;
    virtual void Present() override;
    virtual void Clear(int flags) override;
    virtual void SetClearColour(const float colourRGBA[4]) override;
    virtual void Shutdown() override;
    virtual void Suspend() override;
    [[nodiscard]] virtual bool Suspended() override;
    virtual void Resume() override;

    virtual void SetWindowSize(int w, int h) override;
    virtual void SetFullscreen(bool fs) override;
    [[nodiscard]] virtual bool IsWidescreen() override;
    [[nodiscard]] virtual bool IsHiDef() override;
    virtual void GetFramebufferSize(int& width, int& height) override;
    [[nodiscard]] virtual bool ShouldClose() override;
    virtual void Close() override;
    virtual void UpdateGamma(unsigned short usGamma) override;

    virtual void MatrixMode(int type) override;
    virtual void MatrixSetIdentity() override;
    virtual void MatrixTranslate(float x, float y, float z) override;
    virtual void MatrixRotate(float angle, float x, float y, float z) override;
    virtual void MatrixScale(float x, float y, float z) override;
    virtual void MatrixPerspective(float fovy, float aspect, float zNear, float zFar) override;
    virtual void MatrixOrthogonal(float left, float right, float bottom, float top, float zNear, float zFar) override;
    virtual void MatrixPop() override;
    virtual void MatrixPush() override;
    virtual void MatrixMult(float* mat) override;
    [[nodiscard]] virtual const float* MatrixGet(int type) override;
    virtual void Set_matrixDirty() override;

    virtual void DrawVertices(ePrimitiveType PrimitiveType, int count, void* dataIn, eVertexType vType, ePixelShaderType psType) override;

    virtual void CBuffLockStaticCreations() override;
    [[nodiscard]] virtual int CBuffCreate(int count) override;
    virtual void CBuffDelete(int first, int count) override;
    virtual void CBuffDeleteAll() override;
    virtual void CBuffStart(int index, bool full = false) override;
    virtual void CBuffClear(int index) override;
    virtual void flushIggyCache() override;
    [[nodiscard]] virtual int CBuffSize(int index) override;
    virtual void CBuffEnd() override;
    [[nodiscard]] virtual bool CBuffCall(int index, bool full = true) override;
    virtual void CBuffTick() override;
    virtual void CBuffDeferredModeStart() override;
    virtual void CBuffDeferredModeEnd() override;

    [[nodiscard]] virtual int TextureCreate() override;
    virtual void TextureFree(int idx) override;
    virtual void TextureBind(int idx) override;
    virtual void TextureBindVertex(int idx, bool scaleLight = false) override;
    virtual void TextureSetTextureLevels(int levels) override;
    [[nodiscard]] virtual int TextureGetTextureLevels() override;
    virtual void TextureData(int width, int height, void* data, int level, eTextureFormat format = TEXTURE_FORMAT_RxGyBzAw) override;
    virtual void TextureDataUpdate(int xoffset, int yoffset, int width, int height, void* data, int level) override;
    virtual void TextureSetParam(int param, int value) override;
    virtual void TextureDynamicUpdateStart() override;
    virtual void TextureDynamicUpdateEnd() override;
    [[nodiscard]] virtual int LoadTextureData(const char* szFilename, D3DXIMAGE_INFO* pSrcInfo, int** ppDataOut) override;
    [[nodiscard]] virtual int LoadTextureData(std::uint8_t* pbData, std::uint32_t byteCount, D3DXIMAGE_INFO* pSrcInfo, int** ppDataOut) override;
    [[nodiscard]] virtual int SaveTextureData(const char* szFilename, D3DXIMAGE_INFO* pSrcInfo, int* ppDataOut) override;
    [[nodiscard]] virtual int SaveTextureDataToMemory(void* pOutput, int outputCapacity, int* outputLength, int width, int height, int* ppDataIn) override;
    virtual void ReadPixels(int x, int y, int w, int h, void* buf) override;
    virtual void TextureGetStats() override;
    [[nodiscard]] virtual void* TextureGetTexture(int idx) override;

    virtual void StateSetColour(float r, float g, float b, float a) override;
    virtual void StateSetDepthMask(bool enable) override;
    virtual void StateSetBlendEnable(bool enable) override;
    virtual void StateSetBlendFunc(int src, int dst) override;
    virtual void StateSetBlendFactor(unsigned int colour) override;
    virtual void StateSetAlphaFunc(int func, float param) override;
    virtual void StateSetDepthFunc(int func) override;
    virtual void StateSetFaceCull(bool enable) override;
    virtual void StateSetFaceCullCW(bool enable) override;
    virtual void StateSetLineWidth(float width) override;
    virtual void StateSetWriteEnable(bool red, bool green, bool blue, bool alpha) override;
    virtual void StateSetDepthTestEnable(bool enable) override;
    virtual void StateSetAlphaTestEnable(bool enable) override;
    virtual void StateSetDepthSlopeAndBias(float slope, float bias) override;
    virtual void StateSetFogEnable(bool enable) override;
    virtual void StateSetFogMode(int mode) override;
    virtual void StateSetFogNearDistance(float dist) override;
    virtual void StateSetFogFarDistance(float dist) override;
    virtual void StateSetFogDensity(float density) override;
    virtual void StateSetFogColour(float red, float green, float blue) override;
    virtual void StateSetLightingEnable(bool enable) override;
    virtual void StateSetVertexTextureUV(float u, float v) override;
    virtual void StateSetLightColour(int light, float red, float green, float blue) override;
    virtual void StateSetLightAmbientColour(float red, float green, float blue) override;
    virtual void StateSetLightDirection(int light, float x, float y, float z) override;
    virtual void StateSetLightEnable(int light, bool enable) override;
    virtual void StateSetViewport(eViewportType viewportType) override;
    virtual void StateSetEnableViewportClipPlanes(bool enable) override;
    virtual void StateSetTexGenCol(int col, float x, float y, float z, float w, bool eyeSpace) override;
    virtual void StateSetStencil(int Function, std::uint8_t stencil_ref, std::uint8_t stencil_func_mask, std::uint8_t stencil_write_mask) override;
    virtual void StateSetForceLOD(int LOD) override;
    virtual void StateSetTextureEnable(bool enable) override;
    virtual void StateSetActiveTexture(int tex) override;

    virtual void SetChunkOffset(float x, float y, float z) override;
    virtual void SetAtlasSize(int width, int height) override;

    virtual void BeginConditionalSurvey(int identifier) override;
    virtual void EndConditionalSurvey() override;
    virtual void BeginConditionalRendering(int identifier) override;
    virtual void EndConditionalRendering() override;

    virtual void DoScreenGrabOnNextPresent() override;
    virtual void CaptureThumbnail(ImageFileBuffer* pngOut) override;
    virtual void CaptureScreen(ImageFileBuffer* jpgOut, void* previewOut) override;

    virtual void BeginEvent(const char* eventName) override;
    virtual void EndEvent() override;

private:
    int m_terrainAtlasId = 1;

    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat();
    void createDepthResources();
    void cleanupDepthResources();
    void createDefaultWhiteTexture();

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    SDL_Window* m_window = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;

    // Depth Buffer
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Pipelines
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipelineOpaque = VK_NULL_HANDLE;
    VkPipeline m_pipelineTransparent = VK_NULL_HANDLE;

    // Separación de Unidades de Textura (Unit 0 = Atlas/GUI, Unit 1 = Lightmap)
    std::unordered_map<int, VKTexture> m_textures;
    int m_boundTextureId = 1;
    int m_boundLightmapId = -1;
    VKTexture m_defaultWhiteTexture;
    std::mutex m_textureMtx;
    std::mutex m_queueMtx;

    // Chunks (CBuff)
    std::unordered_map<int, VKChunkBuffer> m_chunkPool;
    std::vector<VKChunkBuffer> m_pendingDestructions;
    std::shared_mutex m_poolMtx;
    std::mutex m_destructionMtx;
    int m_nextListBase = 1;

    // Dynamic VBO
    VkBuffer m_dynamicVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_dynamicVertexMemory = VK_NULL_HANDLE;
    void* m_dynamicVertexMapped = nullptr;
    VkDeviceSize m_dynamicVertexOffset = 0;
    const VkDeviceSize DYNAMIC_VERTEX_BUFFER_SIZE = 16 * 1024 * 1024;

    // Index Buffer global (Quads)
    VkBuffer m_globalEBO = VK_NULL_HANDLE;
    VkDeviceMemory m_globalEBOMemory = VK_NULL_HANDLE;

    // Sincronización
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;
    uint32_t m_imageIndex = 0;
    bool m_frameStarted = false;

    // Estados
    float m_clearColor[4] = {0.08f, 0.08f, 0.12f, 1.0f};
    glm::vec4 m_baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 m_chunkOffset = {0.0f, 0.0f, 0.0f};
    bool m_textureEnabled = true;
    bool m_depthMaskEnabled = true;
    bool m_depthTestEnabled = true;
    bool m_blendEnabled = true;

    // Estados de Blending y Viñeta
    int m_blendSrc = 1;
    int m_blendDst = 0;
    bool m_isVignettePass = false;

    bool m_shouldClose = false;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
};