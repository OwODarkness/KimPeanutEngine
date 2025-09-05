#include "vulkan_backend.h"
#include <iostream>
#include <set>
#include <functional>
#include <GLFW/glfw3.h>
#include "math/math_header.h"
#include "log/logger.h"
#include "vulkan_shader.h"
#include "common/vertex.h"
#include "config/path.h"

#define KP_VULKAN_BACKEND_LOG_NAME "VulkanBackendLog"
const int MAX_FRAMES_IN_FLIGHT = 2;
namespace kpengine::graphics
{
    std::vector<Vertex> vertex = {
        {{0.f, -0.5f, 0.f}},
        {{0.5f, 0.5f, 0.f}},
        {{-0.5f, 0.5f, 0.0f}}};

    // colors (r, g, b, a)
    std::vector<Vector3f> colors = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}};

    std::vector<uint32_t> indices = {0, 1, 2};

    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger)
    {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        }
        else
        {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator)
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(instance, debugMessenger, pAllocator);
        }
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData)
    {
        if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, pCallbackData->pMessage);
            return VK_FALSE;
        }
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_WARNNING, pCallbackData->pMessage);
            return VK_TRUE;
        }
        return VK_FALSE;
    }

    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo)
    {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;
    }

    int RateDeviceSuitability(VkPhysicalDevice device)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        int score = 0;

        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 1000;

        score += props.limits.maxImageDimension2D;

        // TODO: check for required features/extensions (swapchain, samplerAnisotropy etc.)
        // if missing, return 0 (unsuitable)

        return score;
    }

    QueueFamilyIndices QueueFamilyIndices::FindQueueFamilyIndices(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
    {
        uint32_t prop_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &prop_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_family_props(prop_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &prop_count, queue_family_props.data());
        QueueFamilyIndices queue_family_indices{};
        uint32_t index = 0;
        for (const auto &prop : queue_family_props)
        {
            if (prop.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                queue_family_indices.graphics_family = index;
            }
            else if (prop.queueFlags & VK_QUEUE_TRANSFER_BIT)
            {
                queue_family_indices.transfer_family = index;
            }

            VkBool32 is_support_present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &is_support_present);
            if (is_support_present)
            {
                queue_family_indices.present_family = index;
            }

            if (queue_family_indices.IsComplete())
            {
                break;
            }
            index++;
        }

        return queue_family_indices;
    }

    SwapchainSupportDetail SwapchainSupportDetail::FindSwapchainSupports(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        SwapchainSupportDetail swap_chain_support_detail{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &swap_chain_support_detail.capacities);

        uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
        swap_chain_support_detail.surface_formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, swap_chain_support_detail.surface_formats.data());

        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
        swap_chain_support_detail.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, swap_chain_support_detail.present_modes.data());
        return swap_chain_support_detail;
    }

    void VulkanBackend::Initialize()
    {
        CreateInstance();
        CreateDebugMessager();
        CreateSurface();
        CreatePhysicalDevice();
        CreateLogicalDevice();
        CreateSwapchain();
        CreateSwapchainImageViews();
        CreateSwapchainRenderPass();
        CreateGraphicsPipeline();
        CreateFrameBuffers();
        CreateCommandPools();
        CreateCommandBuffers();
        CreateVertexBuffers();
        CreateSyncObjects();
    }
    void VulkanBackend::BeginFrame()
    {
        // 1. wait for last frame to finish
        // 2. acquire RT
        // 3. submit draw command buffer
        // 3. wait for render and present

        vkWaitForFences(logical_device_, 1, &in_flight_fences_[current_frame], VK_TRUE, UINT64_MAX);

        uint32_t image_index;
        VkResult acquire_image_res = vkAcquireNextImageKHR(logical_device_, swapchain_, UINT64_MAX, available_image_sepmaphores_[current_frame], VK_NULL_HANDLE, &image_index);

        if (acquire_image_res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchain();
            return;
        }
        else if (acquire_image_res != VK_SUCCESS && acquire_image_res != VK_SUBOPTIMAL_KHR)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to acquire image");
            throw std::runtime_error("Failed to acquire image");
        }
        vkResetFences(logical_device_, 1, &in_flight_fences_[current_frame]);

        vkResetCommandBuffer(command_buffers_[current_frame], 0);
        RecordCommandBuffer(command_buffers_[current_frame], image_index);

        size_t render_finished_index = swapchain_images_.size() * current_frame + image_index;

        std::array<VkSemaphore, 1> wait_semaphores = {available_image_sepmaphores_[current_frame]};
        std::array<VkPipelineStageFlags, 1> wait_stages = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        std::array<VkSemaphore, 1> signal_semaphores = {render_finished_semaphores_[render_finished_index]};

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers_[current_frame];
        submit_info.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size());
        submit_info.pWaitSemaphores = wait_semaphores.data();
        submit_info.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size());
        submit_info.pSignalSemaphores = signal_semaphores.data();
        submit_info.pWaitDstStageMask = wait_stages.data();

        if (vkQueueSubmit(graphics_queue_.queue, 1, &submit_info, in_flight_fences_[current_frame]) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to submit commandbuffer");
            throw std::runtime_error("Failed to submit commandbuffer");
        }

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.pImageIndices = &image_index;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain_;
        present_info.waitSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size());
        present_info.pWaitSemaphores = signal_semaphores.data();
        present_info.pResults = nullptr;

        VkResult present_res = vkQueuePresentKHR(present_queue_.queue, &present_info);
        if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR || has_resized)
        {
            RecreateSwapchain();
            has_resized = false;
        }
        else if (present_res != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to present");
            throw std::runtime_error("Failed to present");
        }
    }
    void VulkanBackend::EndFrame()
    {
        current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void VulkanBackend::Present()
    {
    }

    void VulkanBackend::Cleanup()
    {
        vkDeviceWaitIdle(logical_device_);

        CleanupSwapchain();

        vkDestroyPipeline(logical_device_, pipeline_, nullptr);
        vkDestroyPipelineLayout(logical_device_, pipeline_layout_, nullptr);
        vkDestroyRenderPass(logical_device_, swapchain_renderpass_, nullptr);

        DestroyBuffer(pos_handle_);
        DestroyBuffer(color_handle_);
        DestroyBuffer(index_handle_);

        buffer_pool_.FreeMemory(logical_device_);

        for (size_t i = 0; i < available_image_sepmaphores_.size(); i++)
        {
            vkDestroySemaphore(logical_device_, available_image_sepmaphores_[i], nullptr);
        }

        for (size_t i = 0; i < render_finished_semaphores_.size(); i++)
        {
            vkDestroySemaphore(logical_device_, render_finished_semaphores_[i], nullptr);
        }

        for (size_t i = 0; i < in_flight_fences_.size(); i++)
        {
            vkDestroyFence(logical_device_, in_flight_fences_[i], nullptr);
        }

        vkDestroyCommandPool(logical_device_, graphics_command_pool_, nullptr);
        vkDestroyCommandPool(logical_device_, transfer_command_pool_, nullptr);

        vkDestroyDevice(logical_device_, nullptr);
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
#ifdef NDEBUG
        const bool enable_validation_layers = false;
#else
        const bool enable_validation_layers = true;
#endif
        if (enable_validation_layers)
        {
            DestroyDebugUtilsMessengerEXT(instance_, debug_messager_, nullptr);
        }
        vkDestroyInstance(instance_, nullptr);
    }

    BufferHandle VulkanBackend::CreateVertexBuffer(const void *data, size_t size)
    {
        return CreateBuffer(data, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    BufferHandle VulkanBackend::CreateIndexBuffer(const void *data, size_t size)
    {
        return CreateBuffer(data, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    void VulkanBackend::DestroyBuffer(BufferHandle handle)
    {
        buffer_pool_.DestroyBufferResource(logical_device_, handle);
    }

    void VulkanBackend::FramebufferResizeCallback(const ResizeEvent &event)
    {
        RenderBackend::FramebufferResizeCallback(event);
        has_resized = true;
    }

    void VulkanBackend::CreateInstance()
    {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "KimPeanut Engine";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "No Engine";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo instance_create_info{};
        instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_create_info.pApplicationInfo = &app_info;

        const std::vector<const char *> extensions = FindRequiredExtensions();
        instance_create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instance_create_info.ppEnabledExtensionNames = extensions.data();

        // setting validation layer for instance
#ifdef NDEBUG
        const bool enable_validation_layers = false;
#else
        const bool enable_validation_layers = true;
#endif
        VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};

        if (enable_validation_layers)
        {

            if (CheckValidationLayerSupport(validation_layers))
            {
                PopulateDebugMessengerCreateInfo(debug_create_info);
                instance_create_info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
                instance_create_info.ppEnabledLayerNames = validation_layers.data();
                instance_create_info.pNext = &debug_create_info;
            }
            else
            {
                instance_create_info.enabledLayerCount = 0;
                instance_create_info.pNext = nullptr;
            }
        }
        else
        {
            instance_create_info.enabledLayerCount = 0;
            instance_create_info.pNext = nullptr;
        }

        if (vkCreateInstance(&instance_create_info, nullptr, &instance_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create instance");
            throw std::runtime_error("Failed to create instance");
        }
    }

    void VulkanBackend::CreateDebugMessager()
    {
#ifdef NDEBUG
        const bool enable_validation_layers = false;
#else
        const bool enable_validation_layers = true;
#endif
        if (!enable_validation_layers)
        {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT debug_messager_create_info{};
        PopulateDebugMessengerCreateInfo(debug_messager_create_info);
        if (CreateDebugUtilsMessengerEXT(instance_, &debug_messager_create_info, nullptr, &debug_messager_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create debug messager");
            throw std::runtime_error("Failed to create debug messager");
        }
    }

    void VulkanBackend::CreateSurface()
    {
        if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create surface");
            throw std::runtime_error("Failed to creaate surface");
        }
    }

    void VulkanBackend::CreatePhysicalDevice()
    {
        uint32_t physical_device_count;
        vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);
        std::vector<VkPhysicalDevice> available_physical_devices(physical_device_count);
        vkEnumeratePhysicalDevices(instance_, &physical_device_count, available_physical_devices.data());

        VkPhysicalDevice best_device = VK_NULL_HANDLE;
        int best_rate = -1;
        for (const auto &device : available_physical_devices)
        {
            if (!CheckPhysicalDeviceSuitable(device))
            {
                continue;
            }
            int rate = RateDeviceSuitability(device);
            if (rate > best_rate)
            {
                best_rate = rate;
                best_device = device;
            }
        }
        if (best_device == VK_NULL_HANDLE)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to find suitable GPU");
            throw std::runtime_error("Failed to find suitable GPU");
        }
        else
        {
            physical_device_ = best_device;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physical_device_, &props);
            std::string message = props.deviceName;
            message += " is ready";
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_INFO, message);
        }
    }

    void VulkanBackend::CreateLogicalDevice()
    {
        QueueFamilyIndices queue_family_indices = QueueFamilyIndices::FindQueueFamilyIndices(physical_device_, surface_);
        graphics_queue_.index = queue_family_indices.graphics_family.value();
        present_queue_.index = queue_family_indices.present_family.value();
        transfer_queue_.index = queue_family_indices.transfer_family.value();

        std::set<uint32_t> queue_family_raw_indices = {graphics_queue_.index, present_queue_.index, transfer_queue_.index};
        std::vector<VkDeviceQueueCreateInfo> queue_create_infos{};
        for (uint32_t family_index : queue_family_raw_indices)
        {
            VkDeviceQueueCreateInfo queue_create_info{};
            queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueCount = 1;
            queue_create_info.queueFamilyIndex = family_index;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }
        VkPhysicalDeviceFeatures device_feature{};

        VkDeviceCreateInfo device_create_info{};
        device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.pEnabledFeatures = &device_feature;
        device_create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
        device_create_info.ppEnabledExtensionNames = device_extensions.data();
        device_create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
        device_create_info.pQueueCreateInfos = queue_create_infos.data();
        device_create_info.enabledLayerCount = 0;
        device_create_info.ppEnabledLayerNames = nullptr;

        if (vkCreateDevice(physical_device_, &device_create_info, nullptr, &logical_device_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create logicial device");
            throw std::runtime_error("Failed to find logicial device");
        }

        vkGetDeviceQueue(logical_device_, graphics_queue_.index, 0, &graphics_queue_.queue);
        vkGetDeviceQueue(logical_device_, present_queue_.index, 0, &present_queue_.queue);
        vkGetDeviceQueue(logical_device_, transfer_queue_.index, 0, &transfer_queue_.queue);
    }

    void VulkanBackend::CreateSwapchain()
    {
        VkSwapchainCreateInfoKHR swapchain_create_info{};
        swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_create_info.surface = surface_;
        swapchain_create_info.clipped = VK_TRUE;
        swapchain_create_info.imageArrayLayers = 1;
        swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        SwapchainSupportDetail swapchain_support_detail = SwapchainSupportDetail::FindSwapchainSupports(physical_device_, surface_);
        VkExtent2D resolution = ChooseSwapChainExtent(swapchain_support_detail.capacities);
        VkSurfaceFormatKHR surface_format = ChooseSwapChainSurfaceFormat(swapchain_support_detail.surface_formats);
        VkPresentModeKHR present_mode = ChooseSwapChainPresentMode(swapchain_support_detail.present_modes);
        uint32_t min_image_count = swapchain_support_detail.capacities.minImageCount + 1;
        if (swapchain_support_detail.capacities.maxImageCount > 0 && min_image_count > swapchain_support_detail.capacities.maxImageCount)
        {
            min_image_count = swapchain_support_detail.capacities.maxImageCount;
        }

        swapchain_create_info.imageExtent = resolution;
        swapchain_create_info.minImageCount = min_image_count;
        swapchain_create_info.imageFormat = surface_format.format;
        swapchain_create_info.imageColorSpace = surface_format.colorSpace;
        swapchain_create_info.presentMode = present_mode;

        std::array<uint32_t, 2> queue_family_raw_indices = {graphics_queue_.index, present_queue_.index};
        if (graphics_queue_.index != present_queue_.index)
        {
            swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchain_create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_raw_indices.size());
            swapchain_create_info.pQueueFamilyIndices = queue_family_raw_indices.data();
        }
        else
        {
            swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchain_create_info.queueFamilyIndexCount = 0;
            swapchain_create_info.pQueueFamilyIndices = nullptr;
        }
        swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;
        swapchain_create_info.preTransform = swapchain_support_detail.capacities.currentTransform;
        swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        if (vkCreateSwapchainKHR(logical_device_, &swapchain_create_info, nullptr, &swapchain_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create swapchain");
            throw std::runtime_error("Failed to find swapchain");
        }

        // local store
        resolution_ = resolution;
        swapchain_image_format_ = surface_format.format;
        uint32_t swapchain_image_count = 0;
        vkGetSwapchainImagesKHR(logical_device_, swapchain_, &swapchain_image_count, nullptr);
        swapchain_images_.resize(swapchain_image_count);
        vkGetSwapchainImagesKHR(logical_device_, swapchain_, &swapchain_image_count, swapchain_images_.data());
    }

    void VulkanBackend::CreateSwapchainImageViews()
    {
        swapchain_imageviews_.resize(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); i++)
        {
            VkImageViewCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image = swapchain_images_[i];
            create_info.format = swapchain_image_format_;
            create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;

            create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            create_info.subresourceRange.baseMipLevel = 0;
            create_info.subresourceRange.levelCount = 1;
            create_info.subresourceRange.baseArrayLayer = 0;
            create_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(logical_device_, &create_info, nullptr, &swapchain_imageviews_[i]) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create image view");
                throw std::runtime_error("Failed to create image view");
            }
        }
    }

    void VulkanBackend::CreateSwapchainRenderPass()
    {
        VkRenderPassCreateInfo renderpass_create_info{};
        renderpass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;

        // set subpass
        renderpass_create_info.subpassCount = 1;
        renderpass_create_info.pSubpasses = &subpass;

        VkSubpassDependency subpass_dependency{};
        subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpass_dependency.dstSubpass = 0;
        subpass_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpass_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpass_dependency.srcAccessMask = 0;
        subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // set dependency
        renderpass_create_info.dependencyCount = 1;
        renderpass_create_info.pDependencies = &subpass_dependency;

        VkAttachmentDescription attachment_desc{};
        attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc.format = swapchain_image_format_;
        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachment_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        renderpass_create_info.attachmentCount = 1;
        renderpass_create_info.pAttachments = &attachment_desc;

        if (vkCreateRenderPass(logical_device_, &renderpass_create_info, nullptr, &swapchain_renderpass_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create swapchain renderpass");
            throw std::runtime_error("Failed to create swapchain renderpass");
        }
    }

    void VulkanBackend::CreateGraphicsPipeline()
    {
        // set shader stage
        std::string spv_shader_dir = GetSPVShaderDirectory();
        ShaderHandle vert_handle = shader_manager_.LoadShader<GraphicsAPIType::GRAPHICS_API_VULKAN>(ShaderType::SHADER_TYPE_VERTEX, spv_shader_dir + "/simple_triangle.vert.spv");
        Shader *vert_shader = shader_manager_.GetShader(vert_handle);
        ShaderHandle frag_handle = shader_manager_.LoadShader<GraphicsAPIType::GRAPHICS_API_VULKAN>(ShaderType::SHADER_TYPE_FRAGMENT, spv_shader_dir + "/simple_triangle.frag.spv");
        Shader *frag_shader = shader_manager_.GetShader(frag_handle);

        VkShaderModule vert_shader_module;
        CreateShaderModule(vert_shader->GetCode(), vert_shader->GetCodeSize(), vert_shader_module);
        VkShaderModule frag_shader_module;
        CreateShaderModule(frag_shader->GetCode(), frag_shader->GetCodeSize(), frag_shader_module);

        VkPipelineShaderStageCreateInfo vert_stage_create_info{};
        vert_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_stage_create_info.module = vert_shader_module;
        vert_stage_create_info.pName = "main";
        vert_stage_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
        ;

        VkPipelineShaderStageCreateInfo frag_stage_create_info{};
        frag_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_stage_create_info.module = frag_shader_module;
        frag_stage_create_info.pName = "main";
        frag_stage_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        ;

        std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vert_stage_create_info, frag_stage_create_info};

        // set vertex stage
        VkVertexInputBindingDescription bindings[2] = {
            {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},   // pos
            {1, sizeof(Vector3f), VK_VERTEX_INPUT_RATE_VERTEX}, // color
        };
        VkVertexInputAttributeDescription attributes[2] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
        };
        VkPipelineVertexInputStateCreateInfo vertex_state_create_info{};
        vertex_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_state_create_info.vertexBindingDescriptionCount = 2;
        vertex_state_create_info.pVertexBindingDescriptions = bindings;
        vertex_state_create_info.vertexAttributeDescriptionCount = 2;
        vertex_state_create_info.pVertexAttributeDescriptions = attributes;

        // set assemblystate
        VkPipelineInputAssemblyStateCreateInfo assembly_state_create_info{};
        assembly_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        assembly_state_create_info.primitiveRestartEnable = VK_FALSE;

        // set viewport state
        VkPipelineViewportStateCreateInfo viewport_state_create_info{};
        viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state_create_info.viewportCount = 1;
        viewport_state_create_info.scissorCount = 1;

        // set raster state
        VkPipelineRasterizationStateCreateInfo raster_state_create_info{};
        raster_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster_state_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster_state_create_info.depthClampEnable = VK_FALSE;
        raster_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
        raster_state_create_info.rasterizerDiscardEnable = VK_FALSE;
        raster_state_create_info.lineWidth = 1.f;
        raster_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
        raster_state_create_info.depthBiasEnable = VK_FALSE;
        raster_state_create_info.depthBiasConstantFactor = 0.0f;
        raster_state_create_info.depthBiasClamp = 0.0f;
        raster_state_create_info.depthBiasSlopeFactor = 0.0f;

        // set dynamic state
        std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state_create_info{};
        dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
        dynamic_state_create_info.pDynamicStates = dynamic_states.data();

        // set multi sample
        VkPipelineMultisampleStateCreateInfo multisample_state_create_info{};
        multisample_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample_state_create_info.sampleShadingEnable = VK_FALSE;
        multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisample_state_create_info.minSampleShading = 1.0f;
        multisample_state_create_info.pSampleMask = nullptr;
        multisample_state_create_info.alphaToCoverageEnable = VK_FALSE;
        multisample_state_create_info.alphaToOneEnable = VK_FALSE;

        // set color blend state
        VkPipelineColorBlendAttachmentState colorblend_attachment{};
        colorblend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorblend_attachment.blendEnable = VK_TRUE;
        colorblend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorblend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorblend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorblend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorblend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorblend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorblend_state_create_info{};
        colorblend_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorblend_state_create_info.attachmentCount = 1;
        colorblend_state_create_info.pAttachments = &colorblend_attachment;
        colorblend_state_create_info.logicOpEnable = VK_FALSE;
        colorblend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
        colorblend_state_create_info.blendConstants[0] = 0.f;
        colorblend_state_create_info.blendConstants[1] = 0.f;
        colorblend_state_create_info.blendConstants[2] = 0.f;
        colorblend_state_create_info.blendConstants[3] = 0.f;

        VkPipelineLayoutCreateInfo layout_create_info{};
        layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_create_info.setLayoutCount = 0;
        layout_create_info.pSetLayouts = nullptr;
        layout_create_info.pushConstantRangeCount = 0;
        layout_create_info.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(logical_device_, &layout_create_info, nullptr, &pipeline_layout_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create pipeline layout");
            throw std::runtime_error("Failed to create pipeline layout");
        }

        VkGraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_create_info.stageCount = static_cast<uint32_t>(stages.size());
        pipeline_create_info.pStages = stages.data();
        pipeline_create_info.pVertexInputState = &vertex_state_create_info;
        pipeline_create_info.pInputAssemblyState = &assembly_state_create_info;
        pipeline_create_info.pViewportState = &viewport_state_create_info;
        pipeline_create_info.pRasterizationState = &raster_state_create_info;
        pipeline_create_info.pMultisampleState = &multisample_state_create_info;
        pipeline_create_info.pDepthStencilState = nullptr;
        pipeline_create_info.pColorBlendState = &colorblend_state_create_info;
        pipeline_create_info.pDynamicState = &dynamic_state_create_info;
        pipeline_create_info.layout = pipeline_layout_;
        pipeline_create_info.renderPass = swapchain_renderpass_;
        pipeline_create_info.subpass = 0;
        pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
        pipeline_create_info.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(logical_device_, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create pipeline");
            throw std::runtime_error("Failed to create pipeline");
        }

        vkDestroyShaderModule(logical_device_, vert_shader_module, nullptr);
        vkDestroyShaderModule(logical_device_, frag_shader_module, nullptr);
    }

    void VulkanBackend::CreateFrameBuffers()
    {
        swapchain_framebuffers_.resize(swapchain_imageviews_.size());
        for (size_t i = 0; i < swapchain_framebuffers_.size(); i++)
        {
            VkFramebufferCreateInfo framebuffer_create_info{};
            framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_create_info.width = resolution_.width;
            framebuffer_create_info.height = resolution_.height;
            framebuffer_create_info.renderPass = swapchain_renderpass_;
            framebuffer_create_info.layers = 1;
            framebuffer_create_info.attachmentCount = 1;
            framebuffer_create_info.pAttachments = &swapchain_imageviews_[i];

            if (vkCreateFramebuffer(logical_device_, &framebuffer_create_info, nullptr, &swapchain_framebuffers_[i]))
            {
                KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create framebuffer");
                throw std::runtime_error("Failed to create framebuffer");
            }
        }
    }

    void VulkanBackend::CreateCommandPools()
    {
        // graphics pool create
        VkCommandPoolCreateInfo graphics_command_pool_create_info{};
        graphics_command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        graphics_command_pool_create_info.queueFamilyIndex = graphics_queue_.index;
        graphics_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(logical_device_, &graphics_command_pool_create_info, nullptr, &graphics_command_pool_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create graphics command pool");
            throw std::runtime_error("Failed to create graphics command pool");
        }

        // transfer pool create
        VkCommandPoolCreateInfo transfer_command_pool_create_info{};
        transfer_command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        transfer_command_pool_create_info.queueFamilyIndex = transfer_queue_.index;
        transfer_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(logical_device_, &transfer_command_pool_create_info, nullptr, &transfer_command_pool_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create transfer command pool");
            throw std::runtime_error("Failed to create transfer command pool");
        }
    }

    void VulkanBackend::CreateCommandBuffers()
    {
        VkCommandBufferAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        allocate_info.commandPool = graphics_command_pool_;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateCommandBuffers(logical_device_, &allocate_info, command_buffers_.data()) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to allocate command buffer");
            throw std::runtime_error("Failed to allocate command buffer");
        }
    }

    void VulkanBackend::CreateVertexBuffers()
    {
        VkDeviceSize pos_size = sizeof(Vertex) * vertex.size();
        pos_handle_ = CreateVertexBuffer(vertex.data(), pos_size);

        VkDeviceSize color_size = sizeof(Vector3f) * colors.size();
        color_handle_ = CreateVertexBuffer(colors.data(), color_size);

        VkDeviceSize indices_size = sizeof(uint32_t) * indices.size();
        index_handle_ = CreateIndexBuffer(indices.data(), indices_size);
    }

    BufferHandle VulkanBackend::CreateBuffer(const void* data, size_t size, VkBufferUsageFlags usage)
    {
        std::array<uint32_t, 2> queue_family_indices = {graphics_queue_.index, transfer_queue_.index};

        // stage buffer
        VkBufferCreateInfo stage_buffer_create_info{};
        stage_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stage_buffer_create_info.size = size;
        stage_buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stage_buffer_create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        stage_buffer_create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_indices.size());
        stage_buffer_create_info.pQueueFamilyIndices = queue_family_indices.data();
        BufferHandle stage_handle = buffer_pool_.CreateBufferResource(physical_device_, logical_device_, &stage_buffer_create_info, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        buffer_pool_.BindBufferData(logical_device_, stage_handle, size, data);

        VkBufferCreateInfo dst_buffer_create_info{};
        dst_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dst_buffer_create_info.size = size;
        dst_buffer_create_info.usage = usage;
        dst_buffer_create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        dst_buffer_create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_indices.size());
        dst_buffer_create_info.pQueueFamilyIndices = queue_family_indices.data();

        BufferHandle dst_handle = buffer_pool_.CreateBufferResource(physical_device_, logical_device_, &dst_buffer_create_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        CopyBuffer(stage_handle, dst_handle, size);

        buffer_pool_.DestroyBufferResource(logical_device_, stage_handle);

        return dst_handle;
    }

    void VulkanBackend::CreateShaderModule(const void *data, size_t size, VkShaderModule &shader_module)
    {

        VkShaderModuleCreateInfo shader_module_create_info{};
        shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_module_create_info.codeSize = static_cast<uint32_t>(size);
        shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(data);

        if (vkCreateShaderModule(logical_device_, &shader_module_create_info, nullptr, &shader_module) != VK_SUCCESS)
        {
            KP_LOG("VulkanShaderLog", LOG_LEVEL_ERROR, "Failed to create shader module:");
            throw std::runtime_error("Failed to create shader module");
        }
    }

    void VulkanBackend::CreateSyncObjects()
    {
        VkSemaphoreCreateInfo semaphore_create_info{};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_create_info{};
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        available_image_sepmaphores_.resize(MAX_FRAMES_IN_FLIGHT);
        for (size_t i = 0; i < available_image_sepmaphores_.size(); i++)
        {
            if (vkCreateSemaphore(logical_device_, &semaphore_create_info, nullptr, &available_image_sepmaphores_[i]) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create avaiable_image_semaphore");
                throw std::runtime_error("Failed to create avaiable_image_semaphore");
            }
        }

        in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);
        for (size_t i = 0; i < in_flight_fences_.size(); i++)
        {
            if (vkCreateFence(logical_device_, &fence_create_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create in flight fence");
                throw std::runtime_error("Failed to create in_flight_fence");
            }
        }

        render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT * swapchain_images_.size());
        for (size_t i = 0; i < render_finished_semaphores_.size(); i++)
        {
            if (vkCreateSemaphore(logical_device_, &semaphore_create_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create render_finished_semaphore", );
                throw std::runtime_error("Failed to create render_finished_semaphores");
            }
        }
    }

    void VulkanBackend::CopyBuffer(BufferHandle src_handle, BufferHandle dst_handle, VkDeviceSize size)
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info{};
        command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.commandPool = transfer_command_pool_;
        command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount = 1;

        VkCommandBuffer copy_command_buffer;
        if (vkAllocateCommandBuffers(logical_device_, &command_buffer_allocate_info, &copy_command_buffer) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to allocate copy command buffer");
            throw std::runtime_error("Failed to allocate copy command buffer");
        }

        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(copy_command_buffer, &command_buffer_begin_info);
        {
            VkBufferCopy copy_region{};
            copy_region.srcOffset = 0;
            copy_region.dstOffset = 0;
            copy_region.size = size;

            VkBuffer src_buffer = buffer_pool_.GetBufferResource(src_handle)->buffer;
            VkBuffer dst_buffer = buffer_pool_.GetBufferResource(dst_handle)->buffer;
            vkCmdCopyBuffer(copy_command_buffer, src_buffer, dst_buffer, 1, &copy_region);
        }
        vkEndCommandBuffer(copy_command_buffer);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &copy_command_buffer;

        vkQueueSubmit(transfer_queue_.queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(transfer_queue_.queue);

        vkFreeCommandBuffers(logical_device_, transfer_command_pool_, 1, &copy_command_buffer);
    }

    void VulkanBackend::RecreateSwapchain()
    {
        while (height_ == 0 || width_ == 0)
        {
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(logical_device_);
        CleanupSwapchain();
        CreateSwapchain();
        CreateSwapchainImageViews();
        CreateFrameBuffers();
    }

    void VulkanBackend::CleanupSwapchain()
    {

        for (size_t i = 0; i < swapchain_framebuffers_.size(); i++)
        {
            vkDestroyFramebuffer(logical_device_, swapchain_framebuffers_[i], nullptr);
        }

        for (size_t i = 0; i < swapchain_imageviews_.size(); i++)
        {
            vkDestroyImageView(logical_device_, swapchain_imageviews_[i], nullptr);
        }
        vkDestroySwapchainKHR(logical_device_, swapchain_, nullptr);
    }

    void VulkanBackend::RecordCommandBuffer(VkCommandBuffer commandbuffer, uint32_t image_index)
    {

        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.pInheritanceInfo = nullptr;

        if (vkBeginCommandBuffer(commandbuffer, &command_buffer_begin_info) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to begin command buffer");
            throw std::runtime_error("Failed to begin command buffer");
        }

        VkRect2D render_area{};
        render_area.extent = resolution_;
        render_area.offset.x = 0;
        render_area.offset.y = 0;
        VkClearValue clear_value{0.f, 0.f, 0.f, 1.f};

        VkRenderPassBeginInfo render_pass_begin_info{};
        render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass = swapchain_renderpass_;

        render_pass_begin_info.framebuffer = swapchain_framebuffers_[image_index];

        render_pass_begin_info.renderArea = render_area;
        render_pass_begin_info.clearValueCount = 1;
        render_pass_begin_info.pClearValues = &clear_value;

        vkCmdBeginRenderPass(commandbuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
        {
            vkCmdBindPipeline(commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

            VkViewport viewport{};
            viewport.width = static_cast<float>(resolution_.width);
            viewport.height = static_cast<float>(resolution_.height);
            viewport.maxDepth = 1.f;
            viewport.minDepth = 0.f;
            viewport.x = 0.f;
            viewport.y = 0.f;
            vkCmdSetViewport(commandbuffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.extent = resolution_;
            scissor.offset.x = 0;
            scissor.offset.y = 0;
            vkCmdSetScissor(commandbuffer, 0, 1, &scissor);

            // TODO: GetBufferResource by bufferhandle;
            VulkanBufferResource *index_buffer_resource = buffer_pool_.GetBufferResource(index_handle_);
            VulkanBufferResource *pos_buffer_resource = buffer_pool_.GetBufferResource(pos_handle_);
            VulkanBufferResource *color_buffer_resource = buffer_pool_.GetBufferResource(color_handle_);

            VkBuffer vertexBuffers[] = {pos_buffer_resource->buffer, color_buffer_resource->buffer};
            VkDeviceSize offsets[] = {0, 0};
            vkCmdBindVertexBuffers(commandbuffer, 0, 2, vertexBuffers, offsets);

            uint32_t index_count = static_cast<uint32_t>(indices.size());
            vkCmdBindIndexBuffer(commandbuffer, index_buffer_resource->buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(commandbuffer, index_count, 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(commandbuffer);
        if (vkEndCommandBuffer(commandbuffer) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to end record command buffer");
            throw std::runtime_error("Failed to end record command buffer");
        }
    }

    std::vector<const char *> VulkanBackend::FindRequiredExtensions() const
    {
        uint32_t count{};
        const char **extensions_name = glfwGetRequiredInstanceExtensions(&count);
        std::vector<const char *> extensions(extensions_name, extensions_name + count);
#ifdef NDEBUG
        const bool enable_validation_layers = false;
#else
        const bool enable_validation_layers = true;
#endif
        if (enable_validation_layers)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        return extensions;
    }

    bool VulkanBackend::CheckValidationLayerSupport(const std::vector<const char *> &validation_layers) const
    {
        std::set<std::string> required_layers(validation_layers.begin(), validation_layers.end());

        uint32_t prop_count = 0;
        vkEnumerateInstanceLayerProperties(&prop_count, nullptr);
        std::vector<VkLayerProperties> available_layer_props(prop_count);
        vkEnumerateInstanceLayerProperties(&prop_count, available_layer_props.data());

        for (const auto &layer_prop : available_layer_props)
        {
            required_layers.erase(layer_prop.layerName);
        }
        return required_layers.empty();
    }

    bool VulkanBackend::CheckDeviceExtensionsSupport(VkPhysicalDevice device, const std::vector<const char *> &extensions) const
    {
        std::set<std::string> required_extensions(extensions.begin(), extensions.end());

        uint32_t prop_count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &prop_count, nullptr);
        std::vector<VkExtensionProperties> available_extensions_props(prop_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &prop_count, available_extensions_props.data());

        for (const auto &extension_prop : available_extensions_props)
        {
            required_extensions.erase(extension_prop.extensionName);
        }
        return required_extensions.empty();
    }

    bool VulkanBackend::CheckPhysicalDeviceSuitable(VkPhysicalDevice device) const
    {
        QueueFamilyIndices queue_family_indices = QueueFamilyIndices::FindQueueFamilyIndices(device, surface_);

        bool is_swapchain_supported = CheckDeviceExtensionsSupport(device, device_extensions);

        bool is_swap_adequate = false;
        if (is_swapchain_supported)
        {
            SwapchainSupportDetail swap_chain_details = SwapchainSupportDetail::FindSwapchainSupports(device, surface_);
            is_swap_adequate = !swap_chain_details.surface_formats.empty() && !swap_chain_details.present_modes.empty();
        }

        return queue_family_indices.IsComplete() && is_swapchain_supported && is_swap_adequate;
    }

    VkSurfaceFormatKHR VulkanBackend::ChooseSwapChainSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available_formats) const
    {
        for (const auto &avail_format : available_formats)
        {
            if (avail_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                avail_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return avail_format;
            }
        }
        return available_formats[0];
    }

    VkPresentModeKHR VulkanBackend::ChooseSwapChainPresentMode(const std::vector<VkPresentModeKHR> &available_modes) const
    {
        for (const auto &avail_mode : available_modes)
        {
            if (avail_mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanBackend::ChooseSwapChainExtent(const VkSurfaceCapabilitiesKHR &capacity) const
    {
        if (capacity.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capacity.currentExtent;
        }
        else
        {
            int height{};
            int width{};
            if (has_resized)
            {
                width = width_;
                height = height_;
            }
            else
            {
                glfwGetWindowSize(window_, &width, &height);
            }
            VkExtent2D actual_size{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            actual_size.width = std::clamp(actual_size.width, capacity.minImageExtent.width, capacity.maxImageExtent.width);
            actual_size.height = std::clamp(actual_size.height, capacity.minImageExtent.height, capacity.maxImageExtent.height);
            return actual_size;
        }
    }
}