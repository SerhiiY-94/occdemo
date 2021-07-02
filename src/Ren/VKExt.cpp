#define __VK_API_DEF__
#include "VK.h"
#undef __VK_API_DEF__

#include <cassert>

#include "Log.h"

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

bool Ren::LoadVulkan(ILog *log) {
#if defined(WIN32)
    HMODULE vulkan_module = LoadLibrary("vulkan-1.dll");
    if (!vulkan_module) {
        log->Error("Failed to load vulkan-1.dll");
        return false;
    }

#define LOAD_VK_FUN(x)                                                                  \
    x = (PFN_##x)GetProcAddress(vulkan_module, #x);                                     \
    if (!(x)) {                                                                         \
        log->Error("Failed to load %s", #x);                                            \
        return false;                                                                   \
    }
#elif defined(__linux__)
    void *vulkan_module = dlopen("./libvulkan.so", RTLD_LAZY);
    if (!vulkan_module) {
        log->Error("Failed to load libvulkan.so");
        return false;
    }

#define LOAD_VK_FUN(x)                                                                   \
    x = (PFN_##x)dlsym(vulkan_module, #x);                                               \
    if (!(x)) {                                                                          \
        log->Error("Failed to load %s", #x);                                             \
        return false;                                                                    \
    }
#else

#if defined(VK_USE_PLATFORM_IOS_MVK)
    void *vulkan_module = dlopen("libMoltenVK.dylib", RTLD_LAZY);
    if (!vulkan_module) {
        log->Error("Failed to load libMoltenVK.dylib");
        return false;
    }
#else
    void *vulkan_module = dlopen("./libvulkan.dylib", RTLD_LAZY);
    if (!vulkan_module) {
        log->Error("Failed to load libvulkan.dylib");
        return false;
    }
#endif

#define LOAD_VK_FUN(x)                                                                   \
    x = (PFN_##x)dlsym(vulkan_module, #x);                                               \
    if (!(x)) {                                                                          \
        log->Error("Failed to load %s", #x);                                             \
        return false;                                                                    \
    }
#endif

    LOAD_VK_FUN(vkCreateInstance);
    LOAD_VK_FUN(vkDestroyInstance);

    LOAD_VK_FUN(vkEnumerateInstanceLayerProperties);
    LOAD_VK_FUN(vkEnumerateInstanceExtensionProperties);

    LOAD_VK_FUN(vkGetInstanceProcAddr);
    LOAD_VK_FUN(vkEnumeratePhysicalDevices);

    LOAD_VK_FUN(vkGetPhysicalDeviceProperties);
    LOAD_VK_FUN(vkGetPhysicalDeviceFeatures);
    LOAD_VK_FUN(vkGetPhysicalDeviceQueueFamilyProperties);

    LOAD_VK_FUN(vkCreateDevice);
    LOAD_VK_FUN(vkDestroyDevice);

    LOAD_VK_FUN(vkEnumerateDeviceExtensionProperties);

    LOAD_VK_FUN(vkGetPhysicalDeviceSurfaceSupportKHR)
    LOAD_VK_FUN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
    LOAD_VK_FUN(vkGetPhysicalDeviceSurfaceFormatsKHR)
    LOAD_VK_FUN(vkGetPhysicalDeviceSurfacePresentModesKHR)

    LOAD_VK_FUN(vkCreateSwapchainKHR)
    LOAD_VK_FUN(vkDestroySwapchainKHR)

    LOAD_VK_FUN(vkGetDeviceQueue)
    LOAD_VK_FUN(vkCreateCommandPool)
    LOAD_VK_FUN(vkDestroyCommandPool)

    LOAD_VK_FUN(vkAllocateCommandBuffers)
    LOAD_VK_FUN(vkFreeCommandBuffers)

    LOAD_VK_FUN(vkGetSwapchainImagesKHR)

    LOAD_VK_FUN(vkCreateFence)
    LOAD_VK_FUN(vkWaitForFences)
    LOAD_VK_FUN(vkResetFences)
    LOAD_VK_FUN(vkDestroyFence)

    LOAD_VK_FUN(vkBeginCommandBuffer)
    LOAD_VK_FUN(vkEndCommandBuffer)

    LOAD_VK_FUN(vkCmdPipelineBarrier)

    LOAD_VK_FUN(vkQueueSubmit)
    LOAD_VK_FUN(vkQueueWaitIdle)

    LOAD_VK_FUN(vkResetCommandBuffer)

    LOAD_VK_FUN(vkCreateImageView)
    LOAD_VK_FUN(vkDestroyImageView)

    LOAD_VK_FUN(vkAcquireNextImageKHR)
    LOAD_VK_FUN(vkQueuePresentKHR)

    LOAD_VK_FUN(vkGetPhysicalDeviceMemoryProperties)
    LOAD_VK_FUN(vkGetPhysicalDeviceFormatProperties)

    LOAD_VK_FUN(vkCreateImage)
    LOAD_VK_FUN(vkDestroyImage)

    LOAD_VK_FUN(vkGetImageMemoryRequirements)
    LOAD_VK_FUN(vkAllocateMemory)
    LOAD_VK_FUN(vkFreeMemory)
    LOAD_VK_FUN(vkBindImageMemory)

    LOAD_VK_FUN(vkCreateRenderPass)
    LOAD_VK_FUN(vkDestroyRenderPass)

    LOAD_VK_FUN(vkCreateFramebuffer)
    LOAD_VK_FUN(vkDestroyFramebuffer);

    LOAD_VK_FUN(vkCreateBuffer)
    LOAD_VK_FUN(vkGetBufferMemoryRequirements)
    LOAD_VK_FUN(vkBindBufferMemory)
    LOAD_VK_FUN(vkDestroyBuffer)

    LOAD_VK_FUN(vkMapMemory)
    LOAD_VK_FUN(vkUnmapMemory)
    LOAD_VK_FUN(vkFlushMappedMemoryRanges)
    LOAD_VK_FUN(vkInvalidateMappedMemoryRanges)
    LOAD_VK_FUN(vkCreateShaderModule)
    LOAD_VK_FUN(vkDestroyShaderModule)
    LOAD_VK_FUN(vkCreateDescriptorSetLayout)
    LOAD_VK_FUN(vkDestroyDescriptorSetLayout)
    LOAD_VK_FUN(vkCreatePipelineLayout)
    LOAD_VK_FUN(vkDestroyPipelineLayout)
    LOAD_VK_FUN(vkCreateGraphicsPipelines)
    LOAD_VK_FUN(vkDestroyPipeline)
    LOAD_VK_FUN(vkCreateSemaphore)
    LOAD_VK_FUN(vkDestroySemaphore)
    LOAD_VK_FUN(vkCreateSampler)
    LOAD_VK_FUN(vkDestroySampler)
    LOAD_VK_FUN(vkCreateDescriptorPool)
    LOAD_VK_FUN(vkDestroyDescriptorPool)
    LOAD_VK_FUN(vkAllocateDescriptorSets)
    LOAD_VK_FUN(vkFreeDescriptorSets)
    LOAD_VK_FUN(vkUpdateDescriptorSets)

    LOAD_VK_FUN(vkCmdBeginRenderPass)
    LOAD_VK_FUN(vkCmdBindPipeline)
    LOAD_VK_FUN(vkCmdSetViewport)
    LOAD_VK_FUN(vkCmdSetScissor)
    LOAD_VK_FUN(vkCmdBindDescriptorSets)
    LOAD_VK_FUN(vkCmdBindVertexBuffers)
    LOAD_VK_FUN(vkCmdBindIndexBuffer)
    LOAD_VK_FUN(vkCmdDraw)
    LOAD_VK_FUN(vkCmdDrawIndexed)
    LOAD_VK_FUN(vkCmdEndRenderPass)
    LOAD_VK_FUN(vkCmdCopyBufferToImage)
    LOAD_VK_FUN(vkCmdCopyBuffer)
    LOAD_VK_FUN(vkCmdPushConstants)
    LOAD_VK_FUN(vkCmdBlitImage)
    LOAD_VK_FUN(vkCmdClearColorImage)

#if defined(VK_USE_PLATFORM_WIN32_KHR)
    LOAD_VK_FUN(vkCreateWin32SurfaceKHR)
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    LOAD_VK_FUN(vkCreateXlibSurfaceKHR);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
    LOAD_VK_FUN(vkCreateIOSSurfaceMVK);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
    LOAD_VK_FUN(vkCreateMacOSSurfaceMVK);
#endif
    LOAD_VK_FUN(vkDestroySurfaceKHR)

#undef LOAD_VK_FUN

    return true;
}

bool Ren::LoadVulkanExtensions(VkInstance instance, ILog *log) {
#define LOAD_VK_FUN(x)                                                                   \
    x = (PFN_##x)vkGetInstanceProcAddr(instance, #x);                                    \
    if (!(x)) {                                                                          \
        log->Error("Failed to load %s", #x);                                             \
        return false;                                                                    \
    }

    LOAD_VK_FUN(vkCreateDebugReportCallbackEXT)
    LOAD_VK_FUN(vkDestroyDebugReportCallbackEXT)
    LOAD_VK_FUN(vkDebugReportMessageEXT)
    LOAD_VK_FUN(vkDebugReportMessageEXT)

    LOAD_VK_FUN(vkDeviceWaitIdle)

    return true;

#undef LOAD_VK_FUN
}
