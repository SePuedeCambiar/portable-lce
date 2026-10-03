#pragma once

#include <vulkan/vulkan.h>
#include <SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

#define VK_CHECK(result)                                                       \
    do {                                                                       \
        VkResult err = (result);                                               \
        if (err != VK_SUCCESS) {                                               \
            fprintf(stderr, "[Vulkan Error] %s at %s:%d (Code: %d)\n",         \
                    #result, __FILE__, __LINE__, err);                         \
            abort();                                                           \
        }                                                                      \
    } while (0)

struct QueueFamilyIndices {
    int graphicsFamily = -1;
    int presentFamily = -1;
    bool isComplete() const { return graphicsFamily >= 0 && presentFamily >= 0; }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};
