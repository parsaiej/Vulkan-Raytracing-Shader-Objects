#ifndef RENDER_CONTEXT_H
#define RENDER_CONTEXT_H

// Context for OS-Window, Vulkan Initialization, Swapchain Management.
// ---------------------------------------------------------

const uint32_t kWindowWidth       = 1920U; // NOLINT
const uint32_t kWindowHeight      = 1080U; // NOLINT
const uint32_t kMaxFramesInFlight = 3U;

struct FrameParams;
class Scene;

class RenderContext
{
public:

    RenderContext(uint32_t windowWidth, uint32_t windowHeight);
    ~RenderContext();

    // Dispatch a render loop into the OS window, invoking a provided command recording callback
    // each frame.
    void Dispatch(const std::function<void(FrameParams)>& commandsFunc, const std::function<void()>& interfaceFunc);

    inline VkInstance&       GetInstance() { return m_VKInstance; }
    inline VkDevice&         GetDevice() { return m_VKDeviceLogical; }
    inline VkPhysicalDevice& GetDevicePhysical() { return m_VKDevicePhysical; }
    inline VmaAllocator&     GetAllocator() { return m_VKMemoryAllocator; }
    inline VkQueue&          GetCommandQueue() { return m_VKCommandQueue; }
    inline uint32_t&         GetCommandQueueIndex() { return m_VKCommandQueueIndex; }
    inline std::mutex&       GetCommandQueueMutex() { return m_VKCommandQueueMutex; }
    inline VkCommandPool&    GetCommandPool() { return m_VKCommandPool; }
    inline VkDescriptorPool& GetDescriptorPool() { return m_VKDescriptorPool; }
    inline GLFWwindow*       GetWindow() { return m_Window; }

    inline const VkImage&     GetSwapchainImage(uint32_t swapChainImageIndex) { return m_VKSwapchainImages.at(swapChainImageIndex); }
    inline const VkImageView& GetSwapchainImageView(uint32_t swapChainImageIndex) { return m_VKSwapchainImageViews.at(swapChainImageIndex); }

private:

    VkInstance       m_VKInstance        = VK_NULL_HANDLE;
    VkPhysicalDevice m_VKDevicePhysical  = VK_NULL_HANDLE;
    VkDevice         m_VKDeviceLogical   = VK_NULL_HANDLE;
    VkDescriptorPool m_VKDescriptorPool  = VK_NULL_HANDLE;
    VmaAllocator     m_VKMemoryAllocator = VK_NULL_HANDLE;
    GLFWwindow*      m_Window;

    // Command Primitives
    VkCommandPool m_VKCommandPool       = VK_NULL_HANDLE;
    VkQueue       m_VKCommandQueue      = VK_NULL_HANDLE;
    uint32_t      m_VKCommandQueueIndex = UINT_MAX;

    // For multi-threaded queue submissions
    std::mutex m_VKCommandQueueMutex;

    // Swapchain Primitives
    VkSwapchainKHR           m_VKSwapchain = VK_NULL_HANDLE;
    VkSurfaceKHR             m_VKSurface   = VK_NULL_HANDLE;
    std::vector<VkImage>     m_VKSwapchainImages;
    std::vector<VkImageView> m_VKSwapchainImageViews;

    // Frame Primitives
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_VKCommandBuffers {};
    std::array<VkSemaphore, kMaxFramesInFlight>     m_VKImageAvailableSemaphores {};
    std::array<VkSemaphore, kMaxFramesInFlight>     m_VKRenderCompleteSemaphores {};
    std::array<VkFence, kMaxFramesInFlight>         m_VKInFlightFences {};
};

#endif
