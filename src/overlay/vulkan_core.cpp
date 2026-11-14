#include "overlay/vulkan_core.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace sf {
namespace {

const char* resultName(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        default: return "VK_ERROR_<other>";
    }
}

#define VK_TRY(expr)                                                        \
    do {                                                                    \
        const VkResult sf_result = (expr);                                  \
        if (sf_result != VK_SUCCESS) {                                      \
            SF_LOGE("%s -> %s", #expr, resultName(sf_result));              \
            return false;                                                   \
        }                                                                   \
    } while (false)

VkCompositeAlphaFlagBitsKHR pickCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
    if (supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    if (supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

}  // namespace

VulkanCore::~VulkanCore() { shutdown(); }

bool VulkanCore::createInstance() {
    uint32_t apiVersion = VK_API_VERSION_1_0;
    auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    if (enumerateVersion) {
        uint32_t supported = 0;
        if (enumerateVersion(&supported) == VK_SUCCESS && supported >= VK_API_VERSION_1_1) {
            apiVersion = VK_API_VERSION_1_1;
        }
    }
    apiVersion_ = apiVersion;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "SfImGui";
    app.applicationVersion = 1;
    app.pEngineName = "SfImGui";
    app.engineVersion = 1;
    app.apiVersion = apiVersion;

    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = extensions;

    VK_TRY(vkCreateInstance(&info, nullptr, &instance_));
    SF_LOGI("Vulkan %u.%u instance created", VK_API_VERSION_MAJOR(apiVersion),
            VK_API_VERSION_MINOR(apiVersion));
    return true;
}

bool VulkanCore::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    info.window = window;
    VK_TRY(vkCreateAndroidSurfaceKHR(instance_, &info, nullptr, &surface_));
    return true;
}

bool VulkanCore::pickPhysicalDevice() {
    uint32_t count = 0;
    VK_TRY(vkEnumeratePhysicalDevices(instance_, &count, nullptr));
    if (count == 0) {
        SF_LOGE("no Vulkan physical devices");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    VK_TRY(vkEnumeratePhysicalDevices(instance_, &count, devices.data()));

    for (VkPhysicalDevice candidate : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        for (uint32_t i = 0; i < familyCount; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
            VkBool32 presentSupported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &presentSupported) !=
                VK_SUCCESS) {
                continue;
            }
            if (!presentSupported) continue;

            physical_ = candidate;
            queueFamily_ = i;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(candidate, &props);
            std::snprintf(deviceName_, sizeof(deviceName_), "%s", props.deviceName);
            SF_LOGI("using GPU: %s (API %u.%u)", props.deviceName,
                    VK_API_VERSION_MAJOR(props.apiVersion),
                    VK_API_VERSION_MINOR(props.apiVersion));
            return true;
        }
    }
    SF_LOGE("no Vulkan device with graphics + present support");
    return false;
}

bool VulkanCore::createDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;

    VK_TRY(vkCreateDevice(physical_, &info, nullptr, &device_));
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

bool VulkanCore::createSwapchain(int windowWidth, int windowHeight) {
    VkSurfaceCapabilitiesKHR caps{};
    VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps));

    uint32_t formatCount = 0;
    VK_TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, nullptr));
    if (formatCount == 0) return false;
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, formats.data()));

    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if ((candidate.format == VK_FORMAT_R8G8B8A8_UNORM ||
             candidate.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = candidate;
            break;
        }
    }
    format_ = chosen.format;

    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        extent_ = caps.currentExtent;
    } else {
        extent_.width = std::min(static_cast<uint32_t>(windowWidth), caps.maxImageExtent.width);
        extent_.height = std::min(static_cast<uint32_t>(windowHeight), caps.maxImageExtent.height);
    }
    if (extent_.width == 0 || extent_.height == 0) return false;

    minImageCount_ = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) minImageCount_ = std::min(minImageCount_, caps.maxImageCount);
    if (minImageCount_ < 2) minImageCount_ = 2;

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = minImageCount_;
    info.imageFormat = format_;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = pickCompositeAlpha(caps.supportedCompositeAlpha);
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VK_TRY(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain));
    if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = swapchain;

    uint32_t imageCount = 0;
    VK_TRY(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr));
    images_.resize(imageCount);
    VK_TRY(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, images_.data()));

    width_ = static_cast<int>(extent_.width);
    height_ = static_cast<int>(extent_.height);
    SF_LOGI("swapchain %dx%d, %u images, format %d", width_, height_, imageCount,
            static_cast<int>(format_));
    return true;
}

bool VulkanCore::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    VK_TRY(vkCreateRenderPass(device_, &info, nullptr, &renderPass_));
    return true;
}

bool VulkanCore::createFramebuffers() {
    views_.resize(images_.size());
    framebuffers_.resize(images_.size());

    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_TRY(vkCreateImageView(device_, &viewInfo, nullptr, &views_[i]));

        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = renderPass_;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &views_[i];
        fbInfo.width = extent_.width;
        fbInfo.height = extent_.height;
        fbInfo.layers = 1;
        VK_TRY(vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffers_[i]));
    }
    return true;
}

bool VulkanCore::createCommandPool() {
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = queueFamily_;
    VK_TRY(vkCreateCommandPool(device_, &info, nullptr, &commandPool_));

    commandBuffers_.resize(images_.size());
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = commandPool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    VK_TRY(vkAllocateCommandBuffers(device_, &alloc, commandBuffers_.data()));
    return true;
}

bool VulkanCore::createSyncObjects() {
    imageAvailable_.resize(kFramesInFlight);
    inFlight_.resize(kFramesInFlight);
    renderFinished_.resize(images_.size());

    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kFramesInFlight; ++i) {
        VK_TRY(vkCreateSemaphore(device_, &semaphore, nullptr, &imageAvailable_[i]));
        VK_TRY(vkCreateFence(device_, &fence, nullptr, &inFlight_[i]));
    }
    for (size_t i = 0; i < images_.size(); ++i) {
        VK_TRY(vkCreateSemaphore(device_, &semaphore, nullptr, &renderFinished_[i]));
    }
    return true;
}

void VulkanCore::destroySyncObjects() {
    for (VkSemaphore semaphore : imageAvailable_) {
        if (semaphore) vkDestroySemaphore(device_, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : renderFinished_) {
        if (semaphore) vkDestroySemaphore(device_, semaphore, nullptr);
    }
    for (VkFence fence : inFlight_) {
        if (fence) vkDestroyFence(device_, fence, nullptr);
    }
    imageAvailable_.clear();
    renderFinished_.clear();
    inFlight_.clear();
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    commandBuffers_.clear();
}

void VulkanCore::destroySwapchain() {
    for (VkFramebuffer framebuffer : framebuffers_) {
        if (framebuffer) vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    for (VkImageView view : views_) {
        if (view) vkDestroyImageView(device_, view, nullptr);
    }
    views_.clear();
    images_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool VulkanCore::init(ANativeWindow* window) {
    if (!window) return false;
    if (!createInstance()) return false;
    if (!createSurface(window)) return false;
    if (!pickPhysicalDevice()) return false;
    if (!createDevice()) return false;
    if (!createRenderPass()) return false;

    const int width = ANativeWindow_getWidth(window);
    const int height = ANativeWindow_getHeight(window);
    return ensureSwapchain(width, height);
}

bool VulkanCore::ensureSwapchain(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (!dirty_ && swapchain_ != VK_NULL_HANDLE && width == width_ && height == height_) return true;

    if (device_ == VK_NULL_HANDLE) return false;
    vkDeviceWaitIdle(device_);

    destroySyncObjects();
    destroySwapchain();

    if (!createSwapchain(width, height)) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPool()) return false;
    if (!createSyncObjects()) return false;
    dirty_ = false;
    return true;
}

bool VulkanCore::beginFrame(uint32_t* imageIndex) {
    if (!ready()) return false;

    VK_TRY(vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX));

    const VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                    imageAvailable_[currentFrame_],
                                                    VK_NULL_HANDLE, imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        dirty_ = true;
        return false;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        SF_LOGE("vkAcquireNextImageKHR -> %s", resultName(acquired));
        return false;
    }

    VK_TRY(vkResetFences(device_, 1, &inFlight_[currentFrame_]));
    return true;
}

bool VulkanCore::endFrame(uint32_t imageIndex) {
    VkSemaphore waitSemaphores[] = {imageAvailable_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {renderFinished_[imageIndex]};

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = waitSemaphores;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffers_[imageIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = signalSemaphores;

    const VkResult submitted = vkQueueSubmit(queue_, 1, &submit, inFlight_[currentFrame_]);
    if (submitted != VK_SUCCESS) {
        SF_LOGE("vkQueueSubmit -> %s", resultName(submitted));
        // The fence was reset for this frame and nothing will signal it; rebuild
        // the synchronisation objects or the next beginFrame() would block
        // forever on that fence.
        dirty_ = true;
        return false;
    }

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = signalSemaphores;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;

    const VkResult presented = vkQueuePresentKHR(queue_, &present);
    currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
    // SUBOPTIMAL only means "the frame was shown, but the swapchain no longer
    // matches the surface optimally"; rebuilding on it made the renderer tear
    // the swapchain down every single frame, which looked like a blank screen.
    if (presented == VK_SUBOPTIMAL_KHR) return true;
    if (presented == VK_ERROR_OUT_OF_DATE_KHR) {
        dirty_ = true;
        return false;
    }
    if (presented != VK_SUCCESS) {
        SF_LOGE("vkQueuePresentKHR -> %s", resultName(presented));
        return false;
    }
    return true;
}

void VulkanCore::shutdown() {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);

    destroySyncObjects();
    destroySwapchain();

    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

}  // namespace sf
