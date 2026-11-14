#pragma once

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace sf {

// Owns the Vulkan objects an overlay needs: instance, android surface, device,
// swapchain, render pass, framebuffers, one command buffer per swapchain image
// and two frames in flight of synchronisation.
class VulkanCore {
public:
    static constexpr int kFramesInFlight = 2;

    VulkanCore() = default;
    ~VulkanCore();

    VulkanCore(const VulkanCore&) = delete;
    VulkanCore& operator=(const VulkanCore&) = delete;

    bool init(ANativeWindow* window);
    bool ensureSwapchain(int width, int height);
    bool beginFrame(uint32_t* imageIndex);
    bool endFrame(uint32_t imageIndex);
    void shutdown();

    // Force the next ensureSwapchain() call to rebuild the swapchain and its
    // synchronisation objects (used when a frame could not be submitted).
    void markDirty() { dirty_ = true; }

    bool ready() const { return device_ != VK_NULL_HANDLE && swapchain_ != VK_NULL_HANDLE; }

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physical_; }
    VkDevice device() const { return device_; }
    uint32_t queueFamily() const { return queueFamily_; }
    VkQueue queue() const { return queue_; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkExtent2D extent() const { return extent_; }
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }
    uint32_t minImageCount() const { return minImageCount_; }
    uint32_t apiVersion() const { return apiVersion_; }
    const char* deviceName() const { return deviceName_; }
    VkCommandBuffer commandBuffer(uint32_t imageIndex) const { return commandBuffers_[imageIndex]; }
    VkFramebuffer framebuffer(uint32_t imageIndex) const { return framebuffers_[imageIndex]; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    bool createInstance();
    bool createSurface(ANativeWindow* window);
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain(int width, int height);
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPool();
    bool createSyncObjects();
    void destroySwapchain();
    void destroySyncObjects();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    int width_ = 0;
    int height_ = 0;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkCommandBuffer> commandBuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    std::vector<VkSemaphore> imageAvailable_;
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence> inFlight_;
    uint32_t currentFrame_ = 0;
    uint32_t minImageCount_ = 2;
    uint32_t apiVersion_ = VK_API_VERSION_1_0;
    bool dirty_ = false;
    char deviceName_[256] = {};
};

}  // namespace sf
