#include "VKRenderer.h"
#include "VKCommon.h"
#include <SDL_vulkan.h>
#include <iostream>
#include <set>
#include <algorithm>
#include <cstring>
#include <atomic>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "minecraft/util/Log.h"

#include "shaders/vert_spv.h"
#include "shaders/frag_spv.h"

// ============================================================================
// FRAME ANATOMY DUMP - VULKAN (MEDICIÓN PERFECTA CON TECLA P)
// ============================================================================
static bool s_vkPendingDump = false;
static bool s_vkDumpThisFrame = false;
static int s_vkDrawIndex = 0;
static bool s_vkWorldDetected = false;

static void LogVKDraw(const char* type, int count, int tex, glm::vec4 col, glm::vec3 off, bool depthMask, int chunkId = -1) {
    if (!s_vkDumpThisFrame) return;
    s_vkDrawIndex++;
    printf("[VK_WORLD_FRAME | Draw #%03d] %s | ChkId:%d | Verts:%d | Tex:%d | DM:%d | Col:(%.2f,%.2f,%.2f,%.2f) | Off:(%.1f,%.1f,%.1f)\n",
           s_vkDrawIndex, type, chunkId, count, tex, (int)depthMask, col.r, col.g, col.b, col.a, off.x, off.y, off.z);
    fflush(stdout);
}

#ifdef USE_VULKAN
namespace platform_internal {
IPlatformRenderer& PlatformRenderer_get() {
    static VKRenderer instance;
    return instance;
}
}
#endif

void VKChunkBuffer::destroy(VkDevice device) {
    if (vbo != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vbo, nullptr);
        vbo = VK_NULL_HANDLE;
    }
    if (vboMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vboMemory, nullptr);
        vboMemory = VK_NULL_HANDLE;
    }
    draws.clear();
    rawVerts.clear();
    valid = false;
    vboReady = false;
    bufferSize = 0;
}

static const glm::mat4 s_vkClipCorrection = glm::mat4(
    1.0f,  0.0f, 0.0f, 0.0f,
    0.0f,  1.0f, 0.0f, 0.0f,
    0.0f,  0.0f, 0.5f, 0.0f,
    0.0f,  0.0f, 0.5f, 1.0f
);

struct PushConstants {
    glm::mat4 uMVP;
    glm::vec4 uBaseColor;
    glm::vec3 uChunkOffset;
    int32_t   uHasTexture;
};

static const int STACK_DEPTH = 64;
struct MatrixStack {
    glm::mat4 stack[STACK_DEPTH];
    int top = 0;
    MatrixStack() { stack[0] = glm::mat4(1.f); }
    glm::mat4& cur() { return stack[top]; }
    void push() {
        if (top < STACK_DEPTH - 1) {
            stack[top + 1] = stack[top];
            ++top;
        }
    }
    void pop() {
        if (top > 0) --top;
    }
    void load(const glm::mat4& m) { cur() = m; }
    void mul(const glm::mat4& m) { cur() = cur() * m; }
};

static thread_local MatrixStack s_proj, s_mv, s_tex[2];
static thread_local int s_matMode = 0;

static MatrixStack& activeStack() {
    switch (s_matMode) {
        case 1:  return s_proj;
        case 2:  return s_tex[0];
        case 3:  return s_tex[1];
        default: return s_mv;
    }
}

static thread_local int s_recListId = -1;
static thread_local std::vector<uint8_t> s_recVerts;
static thread_local std::vector<VKChunkDrawCall> s_recDraws;
static thread_local bool s_recIsCompressed = false;

template <typename T>
static int stbLoad(unsigned char* data, int w, int h, T* info, int** out) {
    int* px = (int*)malloc(w * h * sizeof(int));
    if (!px) return -1;
    
    for (int i = 0; i < w * h; i++) {
        unsigned char r = data[i * 4], g = data[i * 4 + 1], b = data[i * 4 + 2], a = data[i * 4 + 3];
        px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    if (info) {
        info->Width = w;
        info->Height = h;
    }
    *out = px;
    return 0;
}

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

const int MAX_FRAMES_IN_FLIGHT = 1;
static std::atomic<int> s_nextTexId{10};

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

VKRenderer::VKRenderer() {
    m_window = nullptr;
    m_instance = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
    m_presentQueue = VK_NULL_HANDLE;
    m_surface = VK_NULL_HANDLE;
    m_swapchain = VK_NULL_HANDLE;
    m_shouldClose = false;
    m_windowWidth = 1280;
    m_windowHeight = 720;
}

VKRenderer::~VKRenderer() {
    Shutdown();
}

static QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices qIndices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) qIndices.graphicsFamily = i;

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) qIndices.presentFamily = i;

        if (qIndices.isComplete()) break;
        i++;
    }
    return qIndices;
}

static bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

static SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

static VkShaderModule createShaderModule(VkDevice device, const uint32_t* code, size_t sizeBytes) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = sizeBytes;
    createInfo.pCode = code;

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

VkFormat VKRenderer::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    return VK_FORMAT_D32_SFLOAT;
}

VkFormat VKRenderer::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

void VKRenderer::createDepthResources() {
    m_depthFormat = findDepthFormat();

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_swapchainExtent.width;
    imageInfo.extent.height = m_swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(m_device, &imageInfo, nullptr, &m_depthImage));

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_device, m_depthImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_depthImageMemory));
    VK_CHECK(vkBindImageMemory(m_device, m_depthImage, m_depthImageMemory, 0));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_depthImageView));
}

void VKRenderer::cleanupDepthResources() {
    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }
}

void VKRenderer::createDefaultWhiteTexture() {
    uint32_t whitePixel = 0xFFFFFFFF;
    VkDeviceSize imageSize = 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vkCreateBuffer(m_device, &bufInfo, nullptr, &stagingBuffer));

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, memReqs.size,
                                   findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingMemory));
    VK_CHECK(vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0));

    void* data = nullptr;
    vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, &whitePixel, imageSize);
    vkUnmapMemory(m_device, stagingMemory);

    m_defaultWhiteTexture.width = 1;
    m_defaultWhiteTexture.height = 1;

    VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent.width = 1;
    imgInfo.extent.height = 1;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VK_CHECK(vkCreateImage(m_device, &imgInfo, nullptr, &m_defaultWhiteTexture.image));
    vkGetImageMemoryRequirements(m_device, m_defaultWhiteTexture.image, &memReqs);

    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_defaultWhiteTexture.memory));
    VK_CHECK(vkBindImageMemory(m_device, m_defaultWhiteTexture.image, m_defaultWhiteTexture.memory, 0));

    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = m_defaultWhiteTexture.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = { 1, 1, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_defaultWhiteTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(cmd);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_defaultWhiteTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_defaultWhiteTexture.view));

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_defaultWhiteTexture.sampler));

    VkDescriptorSetAllocateInfo descAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descAllocInfo.descriptorPool = m_descriptorPool;
    descAllocInfo.descriptorSetCount = 1;
    descAllocInfo.pSetLayouts = &m_descriptorSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(m_device, &descAllocInfo, &m_defaultWhiteTexture.descriptorSet));

    VkDescriptorImageInfo descImgInfo{};
    descImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImgInfo.imageView = m_defaultWhiteTexture.view;
    descImgInfo.sampler = m_defaultWhiteTexture.sampler;

    VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptorWrite.dstSet = m_defaultWhiteTexture.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImgInfo;
    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
}

VkCommandBuffer VKRenderer::beginSingleTimeCommands() {
    std::lock_guard<std::mutex> lk(m_queueMtx);
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VKRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    std::lock_guard<std::mutex> lk(m_queueMtx);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VKRenderer::Initialise() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return;

    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        m_windowWidth = dm.w;
        m_windowHeight = dm.h;
    }

    Uint32 windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN;
    m_window = SDL_CreateWindow("Minecraft Console Edition (Vulkan Backend)",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                m_windowWidth, m_windowHeight, windowFlags);
    if (!m_window) return;

    SDL_ShowWindow(m_window);
    SDL_RaiseWindow(m_window);

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Portable-LCE";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "4J Engine (Vulkan)";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    uint32_t sdlExtCount = 0;
    SDL_Vulkan_GetInstanceExtensions(m_window, &sdlExtCount, nullptr);
    std::vector<const char*> extensions(sdlExtCount);
    SDL_Vulkan_GetInstanceExtensions(m_window, &sdlExtCount, extensions.data());

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
    if (!SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface)) return;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    int bestScore = -1;

    for (const auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        QueueFamilyIndices qIndices = findQueueFamilies(dev, m_surface);
        bool extensionsSupported = checkDeviceExtensionSupport(dev);
        bool swapChainAdequate = false;
        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(dev, m_surface);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        if (qIndices.isComplete() && extensionsSupported && swapChainAdequate) {
            int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1000 : 500;
            if (score > bestScore) {
                bestScore = score;
                bestDevice = dev;
            }
        }
    }

    m_physicalDevice = bestDevice;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    printf("[Vulkan] GPU Asignada: %s\n", props.deviceName);

    QueueFamilyIndices qIndices = findQueueFamilies(m_physicalDevice, m_surface);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<int> uniqueQueueFamilies = {qIndices.graphicsFamily, qIndices.presentFamily};

    float queuePriority = 1.0f;
    for (int queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Activamos capacidades avanzadas de líneas y polígonos soportadas por Intel Mesa
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.wideLines = VK_TRUE;

    VkDeviceCreateInfo deviceCreateInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device));
    vkGetDeviceQueue(m_device, qIndices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, qIndices.presentFamily, 0, &m_presentQueue);

    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice, m_surface);
    VkSurfaceFormatKHR surfaceFormat = swapChainSupport.formats[0];
    for (const auto& availableFormat : swapChainSupport.formats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM) {
            surfaceFormat = availableFormat;
            break;
        }
    }

    int w, h;
    SDL_Vulkan_GetDrawableSize(m_window, &w, &h);
    m_windowWidth = w;
    m_windowHeight = h;
    m_swapchainExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    m_swapchainImageFormat = surfaceFormat.format;

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    VkSwapchainCreateInfoKHR swapCreateInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapCreateInfo.surface = m_surface;
    swapCreateInfo.minImageCount = imageCount;
    swapCreateInfo.imageFormat = surfaceFormat.format;
    swapCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapCreateInfo.imageExtent = m_swapchainExtent;
    swapCreateInfo.imageArrayLayers = 1;
    swapCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t qFamilyIndices[] = {static_cast<uint32_t>(qIndices.graphicsFamily), static_cast<uint32_t>(qIndices.presentFamily)};
    if (qIndices.graphicsFamily != qIndices.presentFamily) {
        swapCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapCreateInfo.queueFamilyIndexCount = 2;
        swapCreateInfo.pQueueFamilyIndices = qFamilyIndices;
    } else {
        swapCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swapCreateInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    swapCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapCreateInfo.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(m_device, &swapCreateInfo, nullptr, &m_swapchain));

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageViews.resize(m_swapchainImages.size());
    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainImageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]));
    }

    createDepthResources();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthAttachmentRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass));

    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> fbAttachments = { m_swapchainImageViews[i], m_depthImageView };

        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
        framebufferInfo.pAttachments = fbAttachments.data();
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]));
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = qIndices.graphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));

    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()));

    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]));
        VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]));
    }

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;
    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout));

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192};
    VkDescriptorPoolCreateInfo descPoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 8192, 1, &poolSize};
    VK_CHECK(vkCreateDescriptorPool(m_device, &descPoolInfo, nullptr, &m_descriptorPool));

    VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_descriptorSetLayout, 1, &pushConstantRange};
    VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout));

    VkShaderModule vertModule = createShaderModule(m_device, vert_spv, sizeof(vert_spv));
    VkShaderModule fragModule = createShaderModule(m_device, frag_spv, sizeof(frag_spv));

    VkPipelineShaderStageCreateInfo vertStageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main"};
    VkPipelineShaderStageCreateInfo fragStageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main"};
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

    VkVertexInputBindingDescription bindingDescription{0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    attributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    attributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 12};
    attributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 20};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr};

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, (uint32_t)dynamicStates.size(), dynamicStates.data()};

    // 1. PIPELINE OPACO (Para bloques sólidos - Escribe en Depth Buffer, SÓLIDO)
    VkPipelineDepthStencilStateCreateInfo depthOpaque{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthOpaque.depthTestEnable = VK_TRUE;
    depthOpaque.depthWriteEnable = VK_TRUE;
    depthOpaque.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blendOpaque{};
    blendOpaque.colorWriteMask = 0xF;
    blendOpaque.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendingOpaque{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlendingOpaque.attachmentCount = 1;
    colorBlendingOpaque.pAttachments = &blendOpaque;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthOpaque;
    pipelineInfo.pColorBlendState = &colorBlendingOpaque;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelineOpaque));

    // 2. PIPELINE TRANSPARENTE (Para agua, hielo, nubes - Prueba depth, no bloquea)
    VkPipelineDepthStencilStateCreateInfo depthTrans{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthTrans.depthTestEnable = VK_TRUE;
    depthTrans.depthWriteEnable = VK_FALSE;
    depthTrans.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blendTrans{};
    blendTrans.colorWriteMask = 0xF;
    blendTrans.blendEnable = VK_TRUE;
    blendTrans.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendTrans.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendTrans.colorBlendOp = VK_BLEND_OP_ADD;
    blendTrans.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendTrans.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendTrans.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlendingTrans{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlendingTrans.attachmentCount = 1;
    colorBlendingTrans.pAttachments = &blendTrans;

    pipelineInfo.pDepthStencilState = &depthTrans;
    pipelineInfo.pColorBlendState = &colorBlendingTrans;
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelineTransparent));

    // 3. PIPELINE SIN PROFUNDIDAD (Para Sol, Estrellas y HUD 2D)
    VkPipelineDepthStencilStateCreateInfo depthNone{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthNone.depthTestEnable = VK_FALSE;
    depthNone.depthWriteEnable = VK_FALSE;

    pipelineInfo.pDepthStencilState = &depthNone;
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelineNoDepth));

    // 4. PIPELINE PARA LÍNEAS (Para el recuadro de bloque seleccionado y caña de pescar)
    // NOTA: En Vulkan, para topologías de líneas, polygonMode DEBE ser VK_POLYGON_MODE_FILL
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; 
    rasterizer.lineWidth = 1.0f;
    pipelineInfo.pDepthStencilState = &depthTrans; // <--- ¡Prueba profundidad para ocultar aristas traseras!
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelineLines));

    // 5. PIPELINE PARA TRIANGLE FAN (Para la cúpula continua y horizonte del Cielo)
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelineTriangleFan));

    // Restaurar configuración base
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;

    vkDestroyShaderModule(m_device, fragModule, nullptr);
    vkDestroyShaderModule(m_device, vertModule, nullptr);

    // Dynamic VBO
    VkBufferCreateInfo vboInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, DYNAMIC_VERTEX_BUFFER_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vkCreateBuffer(m_device, &vboInfo, nullptr, &m_dynamicVertexBuffer));

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, m_dynamicVertexBuffer, &memReqs);
    VkMemoryAllocateInfo memAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, memReqs.size,
                                  findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VK_CHECK(vkAllocateMemory(m_device, &memAlloc, nullptr, &m_dynamicVertexMemory));
    VK_CHECK(vkBindBufferMemory(m_device, m_dynamicVertexBuffer, m_dynamicVertexMemory, 0));
    VK_CHECK(vkMapMemory(m_device, m_dynamicVertexMemory, 0, DYNAMIC_VERTEX_BUFFER_SIZE, 0, &m_dynamicVertexMapped));

    // Global EBO Quads
    const int MAX_QUADS = 65536;
    std::vector<uint32_t> quadIndices;
    quadIndices.reserve(MAX_QUADS * 6);
    for (int i = 0; i < MAX_QUADS; ++i) {
        uint32_t base = i * 4;
        quadIndices.push_back(base + 0); quadIndices.push_back(base + 1); quadIndices.push_back(base + 2);
        quadIndices.push_back(base + 0); quadIndices.push_back(base + 2); quadIndices.push_back(base + 3);
    }

    VkBufferCreateInfo eboInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, quadIndices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vkCreateBuffer(m_device, &eboInfo, nullptr, &m_globalEBO));
    vkGetBufferMemoryRequirements(m_device, m_globalEBO, &memReqs);

    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(m_device, &memAlloc, nullptr, &m_globalEBOMemory));
    VK_CHECK(vkBindBufferMemory(m_device, m_globalEBO, m_globalEBOMemory, 0));

    void* eboMapped = nullptr;
    VK_CHECK(vkMapMemory(m_device, m_globalEBOMemory, 0, eboInfo.size, 0, &eboMapped));
    memcpy(eboMapped, quadIndices.data(), quadIndices.size() * sizeof(uint32_t));
    vkUnmapMemory(m_device, m_globalEBOMemory);

    createDefaultWhiteTexture();

    m_baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    printf("[Vulkan] Inicialización de pipelines (Opaco, Transparente, NoDepth, Líneas, TriangleFan) completada.\n");
}

void VKRenderer::InitialiseContext() {}
void VKRenderer::Tick() {}

void VKRenderer::StartFrame() {
    if (!m_device || !m_swapchain) return;

    // 1. Si se presionó P, este frame se registrará completo en la terminal
    if (s_vkPendingDump) {
        s_vkDumpThisFrame = true;
        s_vkPendingDump = false;
        s_vkDrawIndex = 0;
        printf("\n==================== INICIO DEL FRAME DUMP (TECLA P EN VULKAN) ====================\n");
        fflush(stdout);
    }

    // 2. Destrucción segura y diferida de chunks antiguos
    std::vector<VKChunkBuffer> toDestroy;
    {
        std::lock_guard<std::mutex> lk(m_destructionMtx);
        if (!m_pendingDestructions.empty()) {
            toDestroy = std::move(m_pendingDestructions);
            m_pendingDestructions.clear();
        }
    }
    for (auto& cb : toDestroy) {
        cb.destroy(m_device);
    }

    // 3. Sincronización con la GPU y adquisición de imagen del swapchain
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                            m_imageAvailableSemaphores[m_currentFrame],
                                            VK_NULL_HANDLE, &m_imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return;

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    // 4. RESET CRÍTICO DE ESTADO PARA EL NUEVO FRAME:
    // Evita que el cielo herede las coordenadas del último chunk del frame anterior
    m_dynamicVertexOffset = 0;
    m_chunkOffset = glm::vec3(0.0f);

    // 5. Reinicio y comienzo del Command Buffer del frame
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // 6. Apertura del RenderPass principal con Color de Cielo Seguro
    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[m_imageIndex];
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    std::array<VkClearValue, 2> clearValues{};

    // PROTECCIÓN CONTRA PARPADEO NEGRO:
    // Si m_clearColor es negro puro (0,0,0), usamos el azul de cielo clásico (0.46, 0.71, 1.0)
    // para que el fondo nunca nazca negro entre fotogramas.
    if (m_clearColor[0] == 0.0f && m_clearColor[1] == 0.0f && m_clearColor[2] == 0.0f) {
        clearValues[0].color = {{0.46f, 0.71f, 1.0f, 1.0f}};
    } else {
        clearValues[0].color = {{m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]}};
    }
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 7. Configuración de Viewport dinámico (con Y invertida nativa de Vulkan) y Scissor
    VkViewport viewport{
        0.0f,
        (float)m_swapchainExtent.height,
        (float)m_swapchainExtent.width,
        -(float)m_swapchainExtent.height,
        0.0f,
        1.0f
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, m_swapchainExtent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    m_frameStarted = true;
}

void VKRenderer::DrawVertices(ePrimitiveType ptype, int count, void* dataIn, eVertexType vType, ePixelShaderType psType) {
    if (count <= 0 || !dataIn) return;

    bool allowDepthWrite = m_depthTestEnabled && m_depthMaskEnabled;

    // 1. Filtro de seguridad de la viñeta
    if (count == 4 && !allowDepthWrite && (m_isVignettePass || m_blendSrc == 0 || m_boundTextureId == 121)) {
        return; // No tapar la pantalla
    }

    bool wasQuad = (ptype == PRIMITIVE_TYPE_QUAD_LIST || (int)ptype == 0x0007);
    size_t stride = (vType == VERTEX_TYPE_COMPRESSED) ? 16 : 32;
    size_t bytes = (size_t)count * stride;

    s_recIsCompressed = (vType == VERTEX_TYPE_COMPRESSED);

    // 2. Si estamos grabando una lista de chunks (CBuff), acumular en memoria y salir
    if (s_recListId >= 0) {
        int first = (int)(s_recVerts.size() / stride);
        s_recVerts.insert(s_recVerts.end(), (const uint8_t*)dataIn, (const uint8_t*)dataIn + bytes);
        s_recDraws.push_back({(int)ptype, first, count, wasQuad});
        return;
    }

    if (!m_frameStarted) return;
    if (m_dynamicVertexOffset + bytes > DYNAMIC_VERTEX_BUFFER_SIZE) return;

    // 3. Copiar vértices al Dynamic VBO
    memcpy((char*)m_dynamicVertexMapped + m_dynamicVertexOffset, dataIn, bytes);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // 4. SELECCIÓN DE PIPELINE SEGÚN TOPOLOGÍA (Líneas, Fans, NoDepth, Opaque)
    int ptypeVal = (int)ptype;
    bool isLine = (ptypeVal == 1 || ptypeVal == 3);      // GL_LINES (1) o GL_LINE_STRIP (3)
    bool isFan  = (ptypeVal == 2 || ptypeVal == 6);      // GL_TRIANGLE_FAN (Cúpula del cielo)

    VkPipeline targetPipeline = m_pipelineTransparent;
    if (isLine && m_pipelineLines != VK_NULL_HANDLE) {
        targetPipeline = m_pipelineLines;              // Recuadro del bloque seleccionado
    } else if (isFan && m_pipelineTriangleFan != VK_NULL_HANDLE) {
        targetPipeline = m_pipelineTriangleFan;        // Cúpula continua del Cielo
    } else if (!m_depthTestEnabled && m_pipelineNoDepth != VK_NULL_HANDLE) {
        targetPipeline = m_pipelineNoDepth;            // Sol, Estrellas, HUD 2D
    } else if (allowDepthWrite && m_pipelineOpaque != VK_NULL_HANDLE) {
        targetPipeline = m_pipelineOpaque;             // Geometría sólida
    }

    if (targetPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
    }

    // 5. Selección y enlace de texturas
    VkDescriptorSet currentSet = m_defaultWhiteTexture.descriptorSet;
    int hasTex = 0;
    if (m_textureEnabled && m_boundTextureId > 0) {
        std::lock_guard<std::mutex> lk(m_textureMtx);
        auto it = m_textures.find(m_boundTextureId);
        if (it != m_textures.end() && it->second.descriptorSet != VK_NULL_HANDLE) {
            currentSet = it->second.descriptorSet;
            hasTex = 1;
        }
    }

    if (currentSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &currentSet, 0, nullptr);
    }

    // 6. Push Constants (Matrices MVP, color base real y offsets)
    PushConstants pc;
    pc.uMVP = s_vkClipCorrection * (s_proj.cur() * s_mv.cur());
    // Respetar el color real recibido de glColor4f/glColor3f (sin forzar blanco)
    pc.uBaseColor = m_baseColor;
    pc.uChunkOffset = glm::vec3(0.0f); // El cielo, la mano y la UI NO llevan offset de chunk
    pc.uHasTexture = hasTex;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);

    // 7. Enlazar Vertex Buffer dinámico
    VkDeviceSize offsets[] = { m_dynamicVertexOffset };
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_dynamicVertexBuffer, offsets);

    // 8. Dibujar según la topología
    if (wasQuad) {
        vkCmdBindIndexBuffer(cmd, m_globalEBO, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, (count / 4) * 6, 1, 0, 0, 0);
    } else {
        vkCmdDraw(cmd, count, 1, 0, 0);
    }

    // 9. Avanzar el offset alineado a 64 bytes
    m_dynamicVertexOffset += (bytes + 63) & ~63;
}

void VKRenderer::Present() {
    if (!m_window) return;

    // 1. Detección no destructiva de la tecla 'P' (NO consume eventos de ratón/WASD)
    const Uint8* keyState = SDL_GetKeyboardState(nullptr);
    static bool s_pWasPressed = false;
    if (keyState[SDL_SCANCODE_P]) {
        if (!s_pWasPressed) {
            s_vkPendingDump = true;
            s_pWasPressed = true;
        }
    } else {
        s_pWasPressed = false;
    }

    // 2. Comprobar si se cerró la ventana usando PEEK (sin vaciar la cola de Minecraft)
    SDL_PumpEvents();
    SDL_Event evs[8];
    int count = SDL_PeepEvents(evs, 8, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
    for (int i = 0; i < count; i++) {
        if (evs[i].type == SDL_QUIT || (evs[i].type == SDL_WINDOWEVENT && evs[i].window.event == SDL_WINDOWEVENT_CLOSE)) {
            m_shouldClose = true;
        }
    }

    // 3. Cierre del dump de telemetría si se capturó este frame
    if (s_vkDumpThisFrame) {
        printf("==================== FIN DEL FRAME DUMP (VULKAN | Total Draws: %d) ====================\n\n", s_vkDrawIndex);
        fflush(stdout);
        s_vkDumpThisFrame = false;
    }

    if (!m_frameStarted) return;

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    {
        std::lock_guard<std::mutex> lk(m_queueMtx);
        VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]));
    }

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_imageIndex;

    vkQueuePresentKHR(m_presentQueue, &presentInfo);

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_frameStarted = false;
}

// CORRECCIÓN CRÍTICA: NO BORRAR EL COLOR A MITAD DE FRAME (Solo Depth)
void VKRenderer::Clear(int flags) {
    if (!m_frameStarted) return;
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    if (flags & 0x00000100) { // GL_DEPTH_BUFFER_BIT
        VkClearAttachment clearDepth{};
        clearDepth.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clearDepth.clearValue.depthStencil = {1.0f, 0};

        VkClearRect clearRect{};
        clearRect.rect.offset = {0, 0};
        clearRect.rect.extent = m_swapchainExtent;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clearDepth, 1, &clearRect);
    }
}

void VKRenderer::SetClearColour(const float c[4]) { memcpy(m_clearColor, c, 16); }

void VKRenderer::Shutdown() {
    if (m_device) {
        vkDeviceWaitIdle(m_device);

        CBuffDeleteAll();
        {
            std::lock_guard<std::mutex> lk(m_destructionMtx);
            for (auto& cb : m_pendingDestructions) cb.destroy(m_device);
            m_pendingDestructions.clear();
        }

        cleanupDepthResources();

        if (m_defaultWhiteTexture.sampler) vkDestroySampler(m_device, m_defaultWhiteTexture.sampler, nullptr);
        if (m_defaultWhiteTexture.view) vkDestroyImageView(m_device, m_defaultWhiteTexture.view, nullptr);
        if (m_defaultWhiteTexture.image) vkDestroyImage(m_device, m_defaultWhiteTexture.image, nullptr);
        if (m_defaultWhiteTexture.memory) vkFreeMemory(m_device, m_defaultWhiteTexture.memory, nullptr);

        for (auto& pair : m_textures) {
            VKTexture& tex = pair.second;
            if (tex.sampler) vkDestroySampler(m_device, tex.sampler, nullptr);
            if (tex.view) vkDestroyImageView(m_device, tex.view, nullptr);
            if (tex.image) vkDestroyImage(m_device, tex.image, nullptr);
            if (tex.memory) vkFreeMemory(m_device, tex.memory, nullptr);
        }
        m_textures.clear();

        if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

        if (m_dynamicVertexMapped) {
            vkUnmapMemory(m_device, m_dynamicVertexMemory);
            m_dynamicVertexMapped = nullptr;
        }
        if (m_dynamicVertexBuffer) vkDestroyBuffer(m_device, m_dynamicVertexBuffer, nullptr);
        if (m_dynamicVertexMemory) vkFreeMemory(m_device, m_dynamicVertexMemory, nullptr);

        if (m_globalEBO) vkDestroyBuffer(m_device, m_globalEBO, nullptr);
        if (m_globalEBOMemory) vkFreeMemory(m_device, m_globalEBOMemory, nullptr);

        // Destrucción de la tríada de pipelines de Vulkan
        if (m_pipelineOpaque) vkDestroyPipeline(m_device, m_pipelineOpaque, nullptr);
        if (m_pipelineTransparent) vkDestroyPipeline(m_device, m_pipelineTransparent, nullptr);
        if (m_pipelineNoDepth) vkDestroyPipeline(m_device, m_pipelineNoDepth, nullptr);
            if (m_pipelineLines) vkDestroyPipeline(m_device, m_pipelineLines, nullptr);
            if (m_pipelineTriangleFan) vkDestroyPipeline(m_device, m_pipelineTriangleFan, nullptr);
        if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
        }

        if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        for (auto fb : m_swapchainFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
        m_swapchainFramebuffers.clear();

        if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        for (auto iv : m_swapchainImageViews) vkDestroyImageView(m_device, iv, nullptr);
        m_swapchainImageViews.clear();

        if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_instance) {
        if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

void VKRenderer::Suspend() {}
bool VKRenderer::Suspended() { return false; }
void VKRenderer::Resume() {}
void VKRenderer::SetWindowSize(int w, int h) { m_windowWidth = w; m_windowHeight = h; }
void VKRenderer::SetFullscreen(bool fs) {}
bool VKRenderer::IsWidescreen() { return true; }
bool VKRenderer::IsHiDef() { return true; }

void VKRenderer::GetFramebufferSize(int& width, int& height) {
    if (m_window) {
        int w, h;
        SDL_Vulkan_GetDrawableSize(m_window, &w, &h);
        m_windowWidth = w;
        m_windowHeight = h;
    }
    width = m_windowWidth;
    height = m_windowHeight;
}

bool VKRenderer::ShouldClose() { return m_shouldClose; }
void VKRenderer::Close() { m_shouldClose = true; }
void VKRenderer::UpdateGamma(unsigned short usGamma) {}

void VKRenderer::MatrixMode(int type) {
    if (type == 0x1701)      s_matMode = 1;
    else if (type == 0x1702) s_matMode = 2;
    else                     s_matMode = 0;
}
void VKRenderer::MatrixSetIdentity() { activeStack().load(glm::mat4(1.f)); }
void VKRenderer::MatrixTranslate(float x, float y, float z) { activeStack().mul(glm::translate(glm::mat4(1.f), {x, y, z})); }

void VKRenderer::MatrixRotate(float angle, float x, float y, float z) {
    // 4J pasa el ángulo ya en radianes: pasar 'angle' directo sin glm::radians()
    activeStack().mul(glm::rotate(glm::mat4(1.f), angle, {x, y, z}));
}

void VKRenderer::MatrixScale(float x, float y, float z) { activeStack().mul(glm::scale(glm::mat4(1.f), {x, y, z})); }
void VKRenderer::MatrixPerspective(float fovy, float aspect, float zNear, float zFar) { s_proj.cur() = glm::perspective(glm::radians(fovy), aspect, zNear, zFar); }
void VKRenderer::MatrixOrthogonal(float left, float right, float bottom, float top, float zNear, float zFar) { s_proj.cur() = glm::ortho(left, right, bottom, top, zNear, zFar); }
void VKRenderer::MatrixPop() { activeStack().pop(); }
void VKRenderer::MatrixPush() { activeStack().push(); }
void VKRenderer::MatrixMult(float* mat) { activeStack().mul(glm::make_mat4(mat)); }
const float* VKRenderer::MatrixGet(int type) {
    static float buf[16];
    glm::mat4* m = (type == 0xBA6) ? &s_mv.cur() : (type == 0xBA7) ? &s_proj.cur() : nullptr;
    if (m) memcpy(buf, glm::value_ptr(*m), 64);
    else memset(buf, 0, 64);
    return buf;
}
void VKRenderer::Set_matrixDirty() {}

void VKRenderer::CBuffLockStaticCreations() {}

int VKRenderer::CBuffCreate(int count) {
    std::unique_lock<std::shared_mutex> lk(m_poolMtx);
    int b = m_nextListBase;
    m_nextListBase += count;
    return b;
}

void VKRenderer::CBuffDelete(int first, int count) {
    std::unique_lock<std::shared_mutex> lk(m_poolMtx);
    for (int i = first; i < first + count; i++) {
        auto it = m_chunkPool.find(i);
        if (it != m_chunkPool.end()) {
            std::lock_guard<std::mutex> lk_del(m_destructionMtx);
            m_pendingDestructions.push_back(std::move(it->second));
            m_chunkPool.erase(it);
        }
    }
}

void VKRenderer::CBuffDeleteAll() {
    std::unique_lock<std::shared_mutex> lk(m_poolMtx);
    for (auto& kv : m_chunkPool) {
        std::lock_guard<std::mutex> lk_del(m_destructionMtx);
        m_pendingDestructions.push_back(std::move(kv.second));
    }
    m_chunkPool.clear();
    m_nextListBase = 1;
}

void VKRenderer::CBuffStart(int index, bool full) {
    s_recListId = index;
    s_recVerts.clear();
    s_recDraws.clear();
    s_recIsCompressed = false;
}

void VKRenderer::CBuffClear(int index) {
    std::unique_lock<std::shared_mutex> lk(m_poolMtx);
    auto it = m_chunkPool.find(index);
    if (it != m_chunkPool.end()) {
        std::lock_guard<std::mutex> lk_del(m_destructionMtx);
        m_pendingDestructions.push_back(std::move(it->second));
        m_chunkPool.erase(it);
    }
}

void VKRenderer::flushIggyCache() {}
int VKRenderer::CBuffSize(int index) { return 0; }

void VKRenderer::CBuffEnd() {
    if (s_recListId < 0) return;

    VKChunkBuffer newCb;
    newCb.rawVerts = std::move(s_recVerts);
    newCb.draws = std::move(s_recDraws);
    newCb.valid = true;
    newCb.vboReady = false;
    newCb.isCompressed = s_recIsCompressed;

    {
        std::unique_lock<std::shared_mutex> lk_pool(m_poolMtx);
        auto it = m_chunkPool.find(s_recListId);
        if (it != m_chunkPool.end()) {
            std::lock_guard<std::mutex> lk_del(m_destructionMtx);
            m_pendingDestructions.push_back(std::move(it->second));
        }
        m_chunkPool[s_recListId] = std::move(newCb);
    }

    s_recVerts.clear();
    s_recListId = -1;
}

bool VKRenderer::CBuffCall(int index, bool full) {
    if (!m_frameStarted) return false;

    s_vkWorldDetected = true;

    std::shared_lock<std::shared_mutex> lk_pool(m_poolMtx);
    auto it = m_chunkPool.find(index);
    if (it == m_chunkPool.end() || !it->second.valid) return false;

    VKChunkBuffer& cb = it->second;

    int totalVerts = 0;
    for (const auto& dc : cb.draws) totalVerts += dc.count;
    LogVKDraw("CBuffCall   ", totalVerts, m_boundTextureId, m_baseColor, m_chunkOffset, m_depthMaskEnabled, index);

    // 1. Subida perezosa de la malla a VRAM si aún no está lista
    if (!cb.vboReady) {
        if (cb.rawVerts.empty() || cb.draws.empty()) return false;

        cb.bufferSize = cb.rawVerts.size();
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, cb.bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE};
        VK_CHECK(vkCreateBuffer(m_device, &bufferInfo, nullptr, &cb.vbo));

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(m_device, cb.vbo, &memReqs);

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, memReqs.size,
                                       findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &cb.vboMemory));
        VK_CHECK(vkBindBufferMemory(m_device, cb.vbo, cb.vboMemory, 0));

        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(m_device, cb.vboMemory, 0, cb.bufferSize, 0, &mapped));
        memcpy(mapped, cb.rawVerts.data(), cb.bufferSize);
        vkUnmapMemory(m_device, cb.vboMemory);

        cb.rawVerts.clear();
        cb.rawVerts.shrink_to_fit();
        cb.vboReady = true;
    }

    if (cb.vbo == VK_NULL_HANDLE || cb.draws.empty()) return true;

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // 2. Selección de Pipeline: Sólido para terreno opaco, Transparente para agua o cielo
    bool allowDepthWrite = m_depthTestEnabled && m_depthMaskEnabled;
    VkPipeline targetPipeline = allowDepthWrite ? m_pipelineOpaque : m_pipelineTransparent;
    if (!m_depthTestEnabled && m_pipelineNoDepth != VK_NULL_HANDLE) {
        targetPipeline = m_pipelineNoDepth;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);

    // 3. Selección de Textura: Si glDisable(GL_TEXTURE_2D) está activo (Cielo/Estrellas), no usar textura
    VkDescriptorSet currentSet = m_defaultWhiteTexture.descriptorSet;
    int hasTex = 0;

    if (m_textureEnabled) {
        int terrainTexId = (m_boundTextureId > 0 && m_boundTextureId != m_boundLightmapId) 
                           ? m_boundTextureId 
                           : m_terrainAtlasId;

        std::lock_guard<std::mutex> lk(m_textureMtx);
        auto texIt = m_textures.find(terrainTexId);
        if (texIt != m_textures.end() && texIt->second.descriptorSet != VK_NULL_HANDLE) {
            currentSet = texIt->second.descriptorSet;
            hasTex = 1;
        } else if (m_terrainAtlasId > 0) {
            auto atlasIt = m_textures.find(m_terrainAtlasId);
            if (atlasIt != m_textures.end() && atlasIt->second.descriptorSet != VK_NULL_HANDLE) {
                currentSet = atlasIt->second.descriptorSet;
                hasTex = 1;
            }
        }
    }

    if (currentSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &currentSet, 0, nullptr);
    }

    // 4. Push Constants:
    // Si la textura está apagada (Cielo, Estrellas), usar m_baseColor de glColor3f.
    // Si es un chunk con texturas, usar blanco neutro (1,1,1,1).
    PushConstants pc;
    pc.uMVP = s_vkClipCorrection * (s_proj.cur() * s_mv.cur());
    pc.uBaseColor = m_textureEnabled ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : m_baseColor;
    
    // Si el índice es menor a 1000 (skyList, starList, darkList), NO sumar offset de chunk
    pc.uChunkOffset = (index < 1000) ? glm::vec3(0.0f) : m_chunkOffset;
    pc.uHasTexture = hasTex;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);

    // 5. Enlazar buffers y despachar draws
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &cb.vbo, offsets);
    vkCmdBindIndexBuffer(cmd, m_globalEBO, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& dc : cb.draws) {
        if (dc.count <= 0) continue;
        if (dc.wasQuad) {
            uint32_t indexCount = (dc.count / 4) * 6;
            vkCmdDrawIndexed(cmd, indexCount, 1, 0, dc.first, 0);
        } else {
            vkCmdDraw(cmd, dc.count, 1, dc.first, 0);
        }
    }

    return true;
}


void VKRenderer::CBuffTick() {}
void VKRenderer::CBuffDeferredModeStart() {}
void VKRenderer::CBuffDeferredModeEnd() {}

int VKRenderer::TextureCreate() { 
    int id = s_nextTexId++;
    m_boundTextureId = id;
    return id; 
}

void VKRenderer::TextureFree(int idx) {
    std::lock_guard<std::mutex> lk(m_textureMtx);
    auto it = m_textures.find(idx);
    if (it != m_textures.end()) {
        if (m_device) {
            vkDeviceWaitIdle(m_device);
            if (it->second.sampler) vkDestroySampler(m_device, it->second.sampler, nullptr);
            if (it->second.view) vkDestroyImageView(m_device, it->second.view, nullptr);
            if (it->second.image) vkDestroyImage(m_device, it->second.image, nullptr);
            if (it->second.memory) vkFreeMemory(m_device, it->second.memory, nullptr);
        }
        m_textures.erase(it);
    }
}

void VKRenderer::TextureBind(int idx) { 
    if (idx > 0) {
        m_boundTextureId = idx; 
    }
}

void VKRenderer::TextureBindVertex(int idx, bool scaleLight) {
    m_boundLightmapId = idx;
}

void VKRenderer::TextureSetTextureLevels(int levels) {}
int VKRenderer::TextureGetTextureLevels() { return 1; }

void VKRenderer::TextureData(int width, int height, void* data, int level, eTextureFormat format) {
    // CORRECCIÓN: Permitir data == nullptr (Minecraft reserva el atlas vacío antes de llenarlo)
    if (width <= 0 || height <= 0 || level != 0) return;

    std::lock_guard<std::mutex> lk(m_textureMtx);
    int texId = (m_boundTextureId > 0) ? m_boundTextureId : s_nextTexId++;
    VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

    // Solo usar staging buffer si vienen píxeles reales de CPU
    if (data != nullptr) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE};
        VK_CHECK(vkCreateBuffer(m_device, &bufferInfo, nullptr, &stagingBuffer));

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReqs);
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, memReqs.size,
                                       findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingBufferMemory));
        VK_CHECK(vkBindBufferMemory(m_device, stagingBuffer, stagingBufferMemory, 0));

        void* mapped = nullptr;
        vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &mapped);
        memcpy(mapped, data, imageSize);
        vkUnmapMemory(m_device, stagingBufferMemory);
    }

    auto it = m_textures.find(texId);
    if (it != m_textures.end()) {
        vkDeviceWaitIdle(m_device);
        if (it->second.sampler) vkDestroySampler(m_device, it->second.sampler, nullptr);
        if (it->second.view) vkDestroyImageView(m_device, it->second.view, nullptr);
        if (it->second.image) vkDestroyImage(m_device, it->second.image, nullptr);
        if (it->second.memory) vkFreeMemory(m_device, it->second.memory, nullptr);
    }

    VKTexture tex;
    tex.width = width;
    tex.height = height;

    printf("[Vulkan Texture] Creada ID: %d | %dx%d (%s)\n", texId, width, height, data ? "Con Datos" : "Lienzo Vacío");

    // DETECCIÓN DEFINITIVA DEL ATLAS DE BLOQUES (terrain.png es >= 256x256)
    if (width >= 256 && height >= 256) {
        m_terrainAtlasId = texId;
        printf("[Vulkan] >>> ATLAS DE BLOQUES REGISTRADO: TexId=%d (%dx%d) <<<\n", texId, width, height);
    }

    VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent.width = width;
    imgInfo.extent.height = height;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VK_CHECK(vkCreateImage(m_device, &imgInfo, nullptr, &tex.image));

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_device, tex.image, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, memReqs.size,
                                   findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory));
    VK_CHECK(vkBindImageMemory(m_device, tex.image, tex.memory, 0));

    VkCommandBuffer cmd = beginSingleTimeCommands();

    if (data != nullptr) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = tex.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };
        vkCmdCopyBufferToImage(cmd, stagingBuffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    } else {
        // Si no hay datos, hacemos la transición directa a SHADER_READ_ONLY para que TextureDataUpdate escriba en ella
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.image = tex.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    endSingleTimeCommands(cmd);

    if (stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view));

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler));

    VkDescriptorSetAllocateInfo descAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    descAllocInfo.descriptorPool = m_descriptorPool;
    descAllocInfo.descriptorSetCount = 1;
    descAllocInfo.pSetLayouts = &m_descriptorSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(m_device, &descAllocInfo, &tex.descriptorSet));

    VkDescriptorImageInfo descImgInfo{};
    descImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImgInfo.imageView = tex.view;
    descImgInfo.sampler = tex.sampler;

    VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptorWrite.dstSet = tex.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImgInfo;
    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

    m_textures[texId] = tex;
}

void VKRenderer::TextureDataUpdate(int xoffset, int yoffset, int width, int height, void* data, int level) {
    if (width <= 0 || height <= 0 || !data) return;

    std::lock_guard<std::mutex> lk(m_textureMtx);
    int texId = (m_boundTextureId > 0) ? m_boundTextureId : 1;
    auto it = m_textures.find(texId);
    if (it == m_textures.end()) {
        TextureData(width, height, data, level, TEXTURE_FORMAT_RxGyBzAw);
        return;
    }

    VkDeviceSize imageSize = width * height * 4;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vkCreateBuffer(m_device, &bufferInfo, nullptr, &stagingBuffer));

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, memReqs.size,
                                   findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingMemory));
    VK_CHECK(vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0));

    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped);
    memcpy(mapped, data, imageSize);
    vkUnmapMemory(m_device, stagingMemory);

    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = it->second.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = { xoffset, yoffset, 0 };
    region.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, it->second.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(cmd);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);
}

void VKRenderer::TextureSetParam(int param, int value) {}
void VKRenderer::TextureDynamicUpdateStart() {}
void VKRenderer::TextureDynamicUpdateEnd() {}

int VKRenderer::LoadTextureData(const char* szFilename, D3DXIMAGE_INFO* pSrcInfo, int** ppDataOut) {
    int w, h, c;
    unsigned char* d = stbi_load(szFilename, &w, &h, &c, 4);
    if (!d) return -1;
    int hr = stbLoad(d, w, h, pSrcInfo, ppDataOut);
    stbi_image_free(d);
    return hr;
}

int VKRenderer::LoadTextureData(std::uint8_t* pbData, std::uint32_t byteCount, D3DXIMAGE_INFO* pSrcInfo, int** ppDataOut) {
    int w, h, c;
    unsigned char* d = stbi_load_from_memory(pbData, byteCount, &w, &h, &c, 4);
    if (!d) return -1;
    int hr = stbLoad(d, w, h, pSrcInfo, ppDataOut);
    stbi_image_free(d);
    return hr;
}

int VKRenderer::SaveTextureData(const char* szFilename, D3DXIMAGE_INFO* pSrcInfo, int* ppDataOut) { return 0; }
int VKRenderer::SaveTextureDataToMemory(void* pOutput, int outputCapacity, int* outputLength, int width, int height, int* ppDataIn) { return 0; }
void VKRenderer::ReadPixels(int x, int y, int w, int h, void* buf) {}
void VKRenderer::TextureGetStats() {}
void* VKRenderer::TextureGetTexture(int idx) { return nullptr; }

void VKRenderer::StateSetColour(float r, float g, float b, float a) { m_baseColor = {r, g, b, a}; }

void VKRenderer::StateSetDepthMask(bool enable) { 
    m_depthMaskEnabled = enable; 
}

void VKRenderer::StateSetBlendEnable(bool enable) { 
    m_blendEnabled = enable; 
}

void VKRenderer::StateSetDepthTestEnable(bool enable) { 
    m_depthTestEnabled = enable; 
}

void VKRenderer::StateSetBlendFunc(int src, int dst) {
    m_blendSrc = src;
    m_blendDst = dst;
    // Si la fuente es GL_ZERO (0), es el pase de viñeta que oscurece la pantalla
    m_isVignettePass = (src == 0);
}


void VKRenderer::StateSetBlendFactor(unsigned int colour) {}
void VKRenderer::StateSetAlphaFunc(int func, float param) {}
void VKRenderer::StateSetDepthFunc(int func) {}
void VKRenderer::StateSetFaceCull(bool enable) {}
void VKRenderer::StateSetFaceCullCW(bool enable) {}
void VKRenderer::StateSetLineWidth(float width) {}
void VKRenderer::StateSetWriteEnable(bool red, bool green, bool blue, bool alpha) {}
void VKRenderer::StateSetAlphaTestEnable(bool enable) {}
void VKRenderer::StateSetDepthSlopeAndBias(float slope, float bias) {}
void VKRenderer::StateSetFogEnable(bool enable) {}
void VKRenderer::StateSetFogMode(int mode) {}
void VKRenderer::StateSetFogNearDistance(float dist) {}
void VKRenderer::StateSetFogFarDistance(float dist) {}
void VKRenderer::StateSetFogDensity(float density) {}
void VKRenderer::StateSetFogColour(float red, float green, float blue) {}
void VKRenderer::StateSetLightingEnable(bool enable) {}
void VKRenderer::StateSetVertexTextureUV(float u, float v) {}
void VKRenderer::StateSetLightColour(int light, float red, float green, float blue) {}
void VKRenderer::StateSetLightAmbientColour(float red, float green, float blue) {}
void VKRenderer::StateSetLightDirection(int light, float x, float y, float z) {}
void VKRenderer::StateSetLightEnable(int light, bool enable) {}
void VKRenderer::StateSetViewport(eViewportType viewportType) {}
void VKRenderer::StateSetEnableViewportClipPlanes(bool enable) {}
void VKRenderer::StateSetTexGenCol(int col, float x, float y, float z, float w, bool eyeSpace) {}
void VKRenderer::StateSetStencil(int Function, std::uint8_t stencil_ref, std::uint8_t stencil_func_mask, std::uint8_t stencil_write_mask) {}
void VKRenderer::StateSetForceLOD(int LOD) {}
void VKRenderer::StateSetTextureEnable(bool enable) { m_textureEnabled = enable; }
void VKRenderer::StateSetActiveTexture(int tex) {}

void VKRenderer::SetChunkOffset(float x, float y, float z) { m_chunkOffset = {x, y, z}; }
void VKRenderer::SetAtlasSize(int width, int height) {}

void VKRenderer::BeginConditionalSurvey(int identifier) {}
void VKRenderer::EndConditionalSurvey() {}
void VKRenderer::BeginConditionalRendering(int identifier) {}
void VKRenderer::EndConditionalRendering() {}

void VKRenderer::DoScreenGrabOnNextPresent() {}
void VKRenderer::CaptureThumbnail(ImageFileBuffer* pngOut) {}
void VKRenderer::CaptureScreen(ImageFileBuffer* jpgOut, void* previewOut) {}

void VKRenderer::BeginEvent(const char* eventName) {}
void VKRenderer::EndEvent() {}