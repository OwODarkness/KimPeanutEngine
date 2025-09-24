#include "vulkan_backend.h"
#include <iostream>
#include <set>
#include <chrono>
#include <functional>
#include <GLFW/glfw3.h>
#include "log/logger.h"
#include "vulkan_shader.h"
#include "common/vertex.h"
#include "config/path.h"
#include "vulkan_buffer_pool.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_texture.h"
#include "vulkan_sampler.h"
#include "vulkan_image_memory_pool.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "tool/image_loader.h"
#include "tool/assimp_model_loader.h"

namespace kpengine::graphics
{

#define KP_VULKAN_BACKEND_LOG_NAME "VulkanBackendLog"
    constexpr int MAX_FRAMES_IN_FLIGHT = 2;

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

    VulkanBackend::VulkanBackend() : buffer_pool_(std::make_unique<VulkanBufferPool>()),
                                     pipeline_manager_(std::make_unique<VulkanPipelineManager>()),
                                     image_memory_pool_(std::make_unique<VulkanImageMemoryPool>()),
                                     texture_manager_(std::make_unique<TextureManager>()),
                                     sampler_manager_(std::make_unique<SamplerManager>()),
                                     model_loader_(std::make_unique<AssimpModelLoader>())
    {
    }

    void VulkanBackend::Initialize()
    {
        CreateInstance();
        CreateDebugMessager();
        CreateSurface();
        CreatePhysicalDevice();
        CreateLogicalDevice();
        InitVulkanContext();
        CreateSwapchain();
        CreateSwapchainImageViews();
        CreateSwapchainRenderPass();
        CreateGraphicsPipeline();
        CreateCommandPools();
        CreateCommandBuffers();
        CreateTextures();
        CreateDepthResource();
        CreateColorResource();
        CreateVertexBuffers();
        CreateFrameBuffers();
        CreateUniformBuffers();
        CreateDescriptorPool();
        CreateDescriptorSets();
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
        UpdateUniformBuffer(current_frame);

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

        for (size_t i = 0; i < uniform_buffer_handles_.size(); i++)
        {
            buffer_pool_->DestroyBufferResource(logical_device_, uniform_buffer_handles_[i]);
        }
        vkDestroyDescriptorPool(logical_device_, descriptor_pool_, nullptr);
        pipeline_manager_->DestroyPipelineResource(logical_device_, pipeline_handle);
        vkDestroyRenderPass(logical_device_, swapchain_renderpass_, nullptr);

        DestroyBufferResource(pos_handle_);

        DestroyBufferResource(index_handle_);

        GraphicsContext context;
        context.native = static_cast<void *>(&context_);
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        texture_manager_->DestroyTexture(context, texture_handle);
        sampler_manager_->DestroySampler(context, sampler_handle);

        buffer_pool_->FreeMemory(logical_device_);
        image_memory_pool_->Destroy(logical_device_);

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

            msaa_sampe_count_ = GetMaxUsableSampleCount();
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
        device_feature.samplerAnisotropy = VK_TRUE;

        VkDeviceCreateInfo device_create_info{};
        device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.pEnabledFeatures = &device_feature;
        device_create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
        device_create_info.ppEnabledExtensionNames = device_extensions.data();
        device_create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
        device_create_info.pQueueCreateInfos = queue_create_infos.data();
        device_create_info.enabledLayerCount = 0;
        device_create_info.ppEnabledLayerNames = nullptr;
        device_create_info.pEnabledFeatures = &device_feature;

        if (vkCreateDevice(physical_device_, &device_create_info, nullptr, &logical_device_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create logicial device");
            throw std::runtime_error("Failed to find logicial device");
        }

        vkGetDeviceQueue(logical_device_, graphics_queue_.index, 0, &graphics_queue_.queue);
        vkGetDeviceQueue(logical_device_, present_queue_.index, 0, &present_queue_.queue);
        vkGetDeviceQueue(logical_device_, transfer_queue_.index, 0, &transfer_queue_.queue);
    }

    void VulkanBackend::InitVulkanContext()
    {
        context_.backend = this;

        context_.physical_device = physical_device_;
        context_.logical_device = logical_device_;
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

        VkAttachmentDescription color_attachment{};
        color_attachment.samples = ConvertToVulkanSampleCount(msaa_sampe_count_);
        color_attachment.format = swapchain_image_format_;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depth_attachment{};
        depth_attachment.format = VK_FORMAT_D32_SFLOAT;
        depth_attachment.samples = ConvertToVulkanSampleCount(msaa_sampe_count_);
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription color_attachment_resolve{};
        color_attachment_resolve.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment_resolve.format = swapchain_image_format_;
        color_attachment_resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment_resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment_resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment_resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment_resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment_resolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_attachment_ref{};
        depth_attachment_ref.attachment = 1;
        depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_attachment_resolve_ref{};
        color_attachment_resolve_ref.attachment = 2;
        color_attachment_resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;
        subpass.pResolveAttachments = &color_attachment_resolve_ref;

        VkSubpassDependency subpass_dependency{};
        subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpass_dependency.dstSubpass = 0;
        subpass_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        subpass_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        subpass_dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 3> attachments = {color_attachment, depth_attachment, color_attachment_resolve};

        VkRenderPassCreateInfo renderpass_create_info{};
        renderpass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        // set subpass
        renderpass_create_info.subpassCount = 1;
        renderpass_create_info.pSubpasses = &subpass;
        // set dependency
        renderpass_create_info.dependencyCount = 1;
        renderpass_create_info.pDependencies = &subpass_dependency;
        // set attachment
        renderpass_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderpass_create_info.pAttachments = attachments.data();

        if (vkCreateRenderPass(logical_device_, &renderpass_create_info, nullptr, &swapchain_renderpass_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create swapchain renderpass");
            throw std::runtime_error("Failed to create swapchain renderpass");
        }
    }

    void VulkanBackend::CreateGraphicsPipeline()
    {
        PipelineDesc pipeline_desc{};
        // set shader stage
        std::string spv_shader_dir = GetSPVShaderDirectory();
        ShaderHandle vert_handle = shader_manager_.CreateShader<GraphicsAPIType::GRAPHICS_API_VULKAN>(ShaderType::SHADER_TYPE_VERTEX, spv_shader_dir + "/simple_triangle.vert.spv");
        Shader *vert_shader = shader_manager_.GetShader(vert_handle);
        ShaderHandle frag_handle = shader_manager_.CreateShader<GraphicsAPIType::GRAPHICS_API_VULKAN>(ShaderType::SHADER_TYPE_FRAGMENT, spv_shader_dir + "/simple_triangle.frag.spv");
        Shader *frag_shader = shader_manager_.GetShader(frag_handle);

        pipeline_desc.vert_shader = vert_shader;
        pipeline_desc.frag_shader = frag_shader;

        // set vertex stage
        pipeline_desc.binding_descs = {

            {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}, // pos
        };
        pipeline_desc.attri_descs = {
            {0, 0, VertexFormat::VERTEX_FORMAT_THREE_FLOATS, 0},
            {1, 0, VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(Vertex, tex_coord)},
        };

        pipeline_desc.descriptor_binding_descs = {
            {{0, 1, DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX},
             {1, 1, DescriptorType::DESCRIPTOR_TYPE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT}},
        };

        pipeline_desc.multisample_state.rasterization_samples = msaa_sampe_count_;

        RasterState state{};
        state.cull_mode = CullMode::CULL_MODE_BACK;
        state.front_face = FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        pipeline_desc.raster_state = state;
        pipeline_handle = pipeline_manager_->CreatePipelineResource(logical_device_, pipeline_desc, swapchain_renderpass_);
    }

    void VulkanBackend::CreateFrameBuffers()
    {
        swapchain_framebuffers_.resize(swapchain_imageviews_.size());
        Texture *depth_textrue = texture_manager_->GetTexture(depth_handle);
        VkImageView depth_view = ConvertToVulkanTextureResource(depth_textrue->GetTextueHandle()).view;

        Texture* color_texture = texture_manager_->GetTexture(color_handle);
        VkImageView color_view = ConvertToVulkanTextureResource(color_texture->GetTextueHandle()).view;

        for (size_t i = 0; i < swapchain_framebuffers_.size(); i++)
        {
            std::array<VkImageView, 3> attachments = {color_view, depth_view, swapchain_imageviews_[i]};
            VkFramebufferCreateInfo framebuffer_create_info{};
            framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_create_info.width = resolution_.width;
            framebuffer_create_info.height = resolution_.height;
            framebuffer_create_info.renderPass = swapchain_renderpass_;
            framebuffer_create_info.layers = 1;
            framebuffer_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebuffer_create_info.pAttachments = attachments.data();

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

    void VulkanBackend::CreateTextures()
    {
        std::string texture_path = GetTextureDirectory() + "wallpaper.jpg";
        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = static_cast<void *>(&context_);

        TextureData texture_data{};
        ImageLoader::ReadFromFile(texture_path, texture_data);

        TextureSettings texture_settings{};
        texture_settings.mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(texture_data.width, texture_data.height)))) + 1;
        texture_settings.format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        texture_settings.usage = TextureUsage::TEXTURE_USAGE_TRANSFER_DST | TextureUsage::TEXTURE_USAGE_TRANSFER_SRC | TextureUsage::TEXTURE_USAGE_SAMPLE;
        texture_settings.sample_count = 1;
        texture_handle = texture_manager_->CreateTexture(context, texture_data, texture_settings);

        SamplerSettings sampler_settings{};
        sampler_settings.address_mode_u = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.address_mode_v = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.address_mode_w = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.enable_anisotropy = true;

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_device_, &properties);
        sampler_settings.max_anisotropy = properties.limits.maxSamplerAnisotropy;
        sampler_settings.mag_filter = SamplerFilterType::SAMPLER_FILTER_LINEAR;
        sampler_settings.min_filter = SamplerFilterType::SAMPLER_FILTER_LINEAR;
        sampler_settings.mip_lod_bias = 0.f;
        sampler_settings.min_lod = 0.f;
        sampler_settings.max_lod = 0.f;

        sampler_handle = sampler_manager_->CreateSampler(context, sampler_settings);
    }

    void VulkanBackend::CreateDepthResource()
    {
        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = static_cast<void *>(&context_);

        TextureSettings depth_settings{};
        depth_settings.mip_levels = 1;
        depth_settings.format = TextureFormat::TEXTURE_FORMAT_D32;
        depth_settings.usage = TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT;
        depth_settings.aspect = ImageAspect::IMAGE_ASPECT_DEPTH;
        depth_settings.sample_count = msaa_sampe_count_;
        TextureData depth_data{};
        depth_data.width = resolution_.width;
        depth_data.height = resolution_.height;
        depth_handle = texture_manager_->CreateTexture(context, depth_data, depth_settings);
    }

    void VulkanBackend::CreateColorResource()
    {
        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = static_cast<void *>(&context_);

        TextureSettings color_settings{};
        color_settings.mip_levels = 1;
        color_settings.format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        color_settings.usage = TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT;
        color_settings.aspect = ImageAspect::IMAGE_ASPECT_COLOR;
        color_settings.sample_count = msaa_sampe_count_;
        TextureData color_data{};
        color_data.width = resolution_.width;
        color_data.height = resolution_.height;
        color_handle = texture_manager_->CreateTexture(context, color_data, color_settings);
    }

    void VulkanBackend::CreateVertexBuffers()
    {

        std::string model_path = GetModelDirectory() + "sphere/sphere.obj";
        model_loader_->Load(model_path, mesh_resource);

        const VkDeviceSize vertices_size = sizeof(Vertex) * mesh_resource.vertices.size();
        pos_handle_ = CreateVertexBuffer(mesh_resource.vertices.data(), vertices_size);

        const VkDeviceSize indices_size = sizeof(uint32_t) * mesh_resource.indices.size();
        index_handle_ = CreateIndexBuffer(mesh_resource.indices.data(), indices_size);
    }

    BufferHandle VulkanBackend::CreateStageBufferResource(size_t size)
    {
        // stage buffer
        VkBufferCreateInfo stage_buffer_create_info{};
        stage_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stage_buffer_create_info.size = size;
        stage_buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stage_buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return buffer_pool_->CreateBufferResource(physical_device_, logical_device_, &stage_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_STAGING);
    }

    bool VulkanBackend::DestroyBufferResource(BufferHandle handle)
    {
        return buffer_pool_->DestroyBufferResource(logical_device_, handle);
    }

    void VulkanBackend::UploadDataToBuffer(BufferHandle handle, size_t size, const void *data)
    {
        buffer_pool_->UploadData(logical_device_, handle, size, data);
    }

    VulkanBufferResource *VulkanBackend::GetBufferResource(BufferHandle handle)
    {
        return buffer_pool_->GetBufferResource(handle);
    }

    BufferHandle VulkanBackend::CreateBuffer(const void *data, size_t size, VkBufferUsageFlags usage)
    {
        BufferHandle stage_handle = CreateStageBufferResource(size);

        UploadDataToBuffer(stage_handle, size, data);

        VkBufferCreateInfo dst_buffer_create_info{};
        dst_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dst_buffer_create_info.size = size;
        dst_buffer_create_info.usage = usage;
        dst_buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        BufferHandle dst_handle = buffer_pool_->CreateBufferResource(physical_device_, logical_device_, &dst_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_DEVICE);
        TransferBufferOwnership(dst_handle, graphics_queue_.index, transfer_queue_.index);
        CopyBuffer(stage_handle, dst_handle, size);
        TransferBufferOwnership(dst_handle, transfer_queue_.index, graphics_queue_.index);
        DestroyBufferResource(stage_handle);

        return dst_handle;
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

    void VulkanBackend::CreateUniformBuffers()
    {
        VkDeviceSize buffer_size = sizeof(UniformBufferObject);

        uniform_buffer_handles_.resize(MAX_FRAMES_IN_FLIGHT);
        uniform_buffer_mapped_ptr_.resize(MAX_FRAMES_IN_FLIGHT);

        VkBufferCreateInfo buffer_create_info{};
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        buffer_create_info.queueFamilyIndexCount = 0;
        buffer_create_info.pQueueFamilyIndices = nullptr;
        buffer_create_info.size = buffer_size;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            uniform_buffer_handles_[i] = buffer_pool_->CreateBufferResource(physical_device_, logical_device_, &buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM);
        }
        VkPhysicalDeviceProperties physical_device_props;
        vkGetPhysicalDeviceProperties(physical_device_, &physical_device_props);
        VkDeviceSize alignment = physical_device_props.limits.minUniformBufferOffsetAlignment;
        VkDeviceSize aligned_buffer_size = (buffer_size + alignment - 1) & ~(alignment - 1);
        void *mapped_ptr = nullptr;
        buffer_pool_->MapBuffer(logical_device_, uniform_buffer_handles_.back(), aligned_buffer_size * uniform_buffer_handles_.size(), &mapped_ptr);
        VkDeviceSize base_offset = buffer_pool_->GetBufferResource(uniform_buffer_handles_.back())->allocation.offset;
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {

            VulkanBufferResource *resource = buffer_pool_->GetBufferResource(uniform_buffer_handles_[i]);
            uniform_buffer_mapped_ptr_[i] = static_cast<char *>(mapped_ptr) + resource->allocation.offset - base_offset;
        }
    }

    void VulkanBackend::CreateDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> pool_sizes;
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        VkDescriptorPoolCreateInfo descriptor_pool_create_info{};
        descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool_create_info.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        descriptor_pool_create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        descriptor_pool_create_info.pPoolSizes = pool_sizes.data();

        if (vkCreateDescriptorPool(logical_device_, &descriptor_pool_create_info, nullptr, &descriptor_pool_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create descriptor pool", );
            throw std::runtime_error("Failed to create descriptor pool");
        }
    }

    void VulkanBackend::CreateDescriptorSets()
    {
        VulkanPipelineResource *resource = pipeline_manager_->GetPipelineResource(pipeline_handle);
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, resource->descriptor_set_layouts[0]);
        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = descriptor_pool_;
        allocate_info.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        allocate_info.pSetLayouts = layouts.data();

        descriptor_sets_.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateDescriptorSets(logical_device_, &allocate_info, descriptor_sets_.data()) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to allocate descriptor set");
            throw std::runtime_error("Failed to allocate descriptor set");
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorBufferInfo buffer_info{};
            VulkanBufferResource *buf_resource = buffer_pool_->GetBufferResource(uniform_buffer_handles_[i]);
            buffer_info.buffer = buf_resource->buffer;
            buffer_info.offset = 0;
            buffer_info.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo image_info{};
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Texture *texture = texture_manager_->GetTexture(texture_handle);
            VulkanTextureResource texture_resource = ConvertToVulkanTextureResource(texture->GetTextueHandle());
            image_info.imageView = texture_resource.view;
            Sampler *sampler = sampler_manager_->GetSampler(sampler_handle);
            VulkanSamplerResource sample_resource = ConvertToVulkanSamplerResource(sampler->GetSampleHandle());
            image_info.sampler = sample_resource.sampler;

            // 0 buffer, 1 image
            std::array<VkWriteDescriptorSet, 2> descriptor_writes{};
            descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_writes[0].dstSet = descriptor_sets_[i];
            descriptor_writes[0].dstBinding = 0;
            descriptor_writes[0].dstArrayElement = 0;
            descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptor_writes[0].descriptorCount = 1;
            descriptor_writes[0].pBufferInfo = &buffer_info;

            descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_writes[1].dstSet = descriptor_sets_[i];
            descriptor_writes[1].dstBinding = 1;
            descriptor_writes[1].dstArrayElement = 0;
            descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptor_writes[1].descriptorCount = 1;
            descriptor_writes[1].pImageInfo = &image_info;

            vkUpdateDescriptorSets(logical_device_, static_cast<uint32_t>(descriptor_writes.size()), descriptor_writes.data(), 0, nullptr);
        }
    }

    void VulkanBackend::UpdateUniformBuffer(uint32_t current_image)
    {
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();
        UniformBufferObject ubo{};
        Transform3f model{};
        // model.rotator_.pitch_ = -90.f;
        model.scale_ = {0.5f, 0.5f, 0.5f};
        model.rotator_.pitch_ = time * 90.f;

        ubo.model = Matrix4f::MakeTransformMatrix(model).Transpose();
        Vector3f camera = {0.f, 0.f, 2.f};
        Vector3f target = {0.f, 0.f, 0.f};
        Vector3f dir = target - camera;
        ubo.view = Matrix4f::MakeCameraMatrix(camera, dir, {0.f, 1.f, 0.f}).Transpose();
        float aspect = resolution_.width / (float)resolution_.height;
        ubo.proj = Matrix4f::MakePerProjMatrix(math::DegreeToRadian(45.f), aspect, 0.1f, 10.f).Transpose();
        ubo.proj[1][1] *= -1;
        if (uniform_buffer_mapped_ptr_[current_image])
        {
            void *start_address = static_cast<char *>(uniform_buffer_mapped_ptr_[current_image]) + current_image * sizeof(UniformBufferObject);
            memcpy(uniform_buffer_mapped_ptr_[current_image], &ubo, sizeof(ubo));
        }
    }

    VkCommandBuffer VulkanBackend::BeginSingleTimeCommands(VkCommandPool command_pool)
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info{};
        command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.commandPool = command_pool;
        command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount = 1;

        VkCommandBuffer commandbuffer;
        if (vkAllocateCommandBuffers(logical_device_, &command_buffer_allocate_info, &commandbuffer) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to allocate copy command buffer");
            throw std::runtime_error("Failed to allocate copy command buffer");
        }

        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandbuffer, &command_buffer_begin_info);

        return commandbuffer;
    }

    void VulkanBackend::EndSingleTimeCommands(VkCommandBuffer commandbuffer, VkCommandPool commandpool, VkQueue queue)
    {
        vkEndCommandBuffer(commandbuffer);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &commandbuffer;

        vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(logical_device_, commandpool, 1, &commandbuffer);
    }

    void VulkanBackend::TransferBufferOwnership(BufferHandle buffer_handle, uint32_t src_queue_family, uint32_t dst_queue_family)
    {
        VkCommandPool release_command_pool;
        VkQueue release_queue;
        VkCommandPool acquire_command_pool;
        VkQueue acquire_queue;

        VkPipelineStageFlags release_src_stage_flags;
        VkPipelineStageFlags release_dst_stage_flags;
        VkPipelineStageFlags acquire_src_stage_flags;
        VkPipelineStageFlags acquire_dst_stage_flags;

        VulkanBufferResource *resource = buffer_pool_->GetBufferResource(buffer_handle);

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.buffer = resource->buffer;
        barrier.size = VK_WHOLE_SIZE;
        barrier.offset = 0;

        if (dst_queue_family == graphics_queue_.index)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            barrier.srcQueueFamilyIndex = transfer_queue_.index;
            barrier.dstQueueFamilyIndex = graphics_queue_.index;

            release_command_pool = transfer_command_pool_;
            release_queue = transfer_queue_.queue;
            acquire_command_pool = graphics_command_pool_;
            acquire_queue = graphics_queue_.queue;

            release_src_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            release_dst_stage_flags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            acquire_src_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            acquire_dst_stage_flags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }
        else if (dst_queue_family == transfer_queue_.index)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.srcQueueFamilyIndex = graphics_queue_.index;
            barrier.dstQueueFamilyIndex = transfer_queue_.index;

            release_command_pool = graphics_command_pool_;
            release_queue = graphics_queue_.queue;
            acquire_command_pool = transfer_command_pool_;
            acquire_queue = transfer_queue_.queue;

            release_src_stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            release_dst_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            acquire_src_stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            acquire_dst_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }

        // release
        VkCommandBuffer release_command_buffer = BeginSingleTimeCommands(release_command_pool);
        vkCmdPipelineBarrier(release_command_buffer, release_src_stage_flags, release_dst_stage_flags, 0,
                             0, nullptr,
                             1, &barrier,
                             0, nullptr);
        EndSingleTimeCommands(release_command_buffer, release_command_pool, release_queue);

        // acquire
        VkCommandBuffer acquire_command_buffer = BeginSingleTimeCommands(acquire_command_pool);
        vkCmdPipelineBarrier(acquire_command_buffer, acquire_src_stage_flags, acquire_dst_stage_flags, 0,
                             0, nullptr,
                             1, &barrier,
                             0, nullptr);
        EndSingleTimeCommands(acquire_command_buffer, acquire_command_pool, acquire_queue);
    }

    void VulkanBackend::CopyBuffer(BufferHandle src_handle, BufferHandle dst_handle, VkDeviceSize size)
    {
        VkCommandBuffer command_buffer = BeginSingleTimeCommands(transfer_command_pool_);
        {
            VkBufferCopy copy_region{};
            copy_region.srcOffset = 0;
            copy_region.dstOffset = 0;
            copy_region.size = size;

            VkBuffer src_buffer = buffer_pool_->GetBufferResource(src_handle)->buffer;
            VkBuffer dst_buffer = buffer_pool_->GetBufferResource(dst_handle)->buffer;

            vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);
        }

        EndSingleTimeCommands(command_buffer, transfer_command_pool_, transfer_queue_.queue);
    }

    void VulkanBackend::TransitionImageLayout(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access, VkImageAspectFlags aspect_mask, uint32_t base_mip_level, uint32_t level_count)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = dst_access;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = aspect_mask;
        barrier.subresourceRange.baseMipLevel = base_mip_level;
        barrier.subresourceRange.levelCount = level_count;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkCommandBuffer command_buffer = BeginSingleTimeCommands(graphics_command_pool_);
        vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        EndSingleTimeCommands(command_buffer, graphics_command_pool_, graphics_queue_.queue);
    }
    void VulkanBackend::ReleaseImageOwnerShip(VkImage image, VkImageLayout current_layout, uint32_t src_queue_family, uint32_t dst_queue_family, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkImageAspectFlags aspect_mask, VkCommandPool command_pool, VkQueue queue, uint32_t base_mip_level, uint32_t level_count)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.oldLayout = current_layout;
        barrier.newLayout = current_layout;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = 0;
        barrier.srcQueueFamilyIndex = src_queue_family;
        barrier.dstQueueFamilyIndex = dst_queue_family;
        barrier.subresourceRange.aspectMask = aspect_mask;
        barrier.subresourceRange.baseMipLevel = base_mip_level;
        barrier.subresourceRange.levelCount = level_count;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkCommandBuffer command_buffer = BeginSingleTimeCommands(command_pool);
        vkCmdPipelineBarrier(command_buffer, src_stage, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        EndSingleTimeCommands(command_buffer, command_pool, queue);
    }
    void VulkanBackend::AcquireImageOwnerShip(VkImage image, VkImageLayout expected_layout, uint32_t src_queue_family, uint32_t dst_queue_family, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access, VkImageAspectFlags aspect_mask, VkCommandPool command_pool, VkQueue queue, uint32_t base_mip_level, uint32_t level_count)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = expected_layout;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = dst_access;
        barrier.srcQueueFamilyIndex = src_queue_family;
        barrier.dstQueueFamilyIndex = dst_queue_family;
        barrier.subresourceRange.aspectMask = aspect_mask;
        barrier.subresourceRange.baseMipLevel = base_mip_level;
        barrier.subresourceRange.levelCount = level_count;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkCommandBuffer command_buffer = BeginSingleTimeCommands(command_pool);
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        EndSingleTimeCommands(command_buffer, command_pool, queue);
    }

    void VulkanBackend::TransitionImageLayout(VkImage image, TextureUsage src_usage, TextureUsage dst_usage, uint32_t base_mip_level, uint32_t level_count)
    {
        if (src_usage == TextureUsage::None && dst_usage == TextureUsage::TEXTURE_USAGE_TRANSFER_DST)
        {
            TransitionImageLayout(
                image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                base_mip_level, level_count);
        }
        else if (src_usage == TextureUsage::TEXTURE_USAGE_TRANSFER_DST && dst_usage == TextureUsage::TEXTURE_USAGE_SAMPLE)
        {
            TransitionImageLayout(
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                base_mip_level, level_count);
        }
        else if (src_usage == TextureUsage::None && dst_usage == TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT)
        {
            TransitionImageLayout(
                image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                0,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                base_mip_level, level_count);
        }
        else if (src_usage == TextureUsage::None && dst_usage == TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT)
        {

            TransitionImageLayout(
                image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                base_mip_level, level_count);
        }
        else
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Unsupport layout transition");
            throw std::runtime_error("Unsupport layout transition");
        }
    }

    void VulkanBackend::ReleaseImageOwnerShip(VkImage image, TextureUsage src_usage, TextureUsage dst_usage, uint32_t base_mip_level, uint32_t level_count)
    {
        if (src_usage == TextureUsage::None && dst_usage == TextureUsage::TEXTURE_USAGE_TRANSFER_DST)
        {
            ReleaseImageOwnerShip(
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                graphics_queue_.index,
                transfer_queue_.index,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                0,
                VK_IMAGE_ASPECT_COLOR_BIT,
                graphics_command_pool_,
                graphics_queue_.queue,
                base_mip_level, level_count);
        }
        else if (src_usage == TextureUsage::TEXTURE_USAGE_TRANSFER_DST && dst_usage == TextureUsage::TEXTURE_USAGE_SAMPLE)
        {
            ReleaseImageOwnerShip(
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                transfer_queue_.index,
                graphics_queue_.index,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                transfer_command_pool_,
                transfer_queue_.queue,
                base_mip_level, level_count);
        }
    }
    void VulkanBackend::AcquireImageOwnerShip(VkImage image, TextureUsage src_usage, TextureUsage dst_usage, uint32_t base_mip_level, uint32_t level_count)
    {
        if (src_usage == TextureUsage::None &&
            dst_usage == TextureUsage::TEXTURE_USAGE_TRANSFER_DST)
        {
            // Acquire on transfer queue, expect layout TRANSFER_DST_OPTIMAL
            AcquireImageOwnerShip(
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                graphics_queue_.index,
                transfer_queue_.index,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                transfer_command_pool_,
                transfer_queue_.queue,
                base_mip_level, level_count);
        }
        else if (src_usage == TextureUsage::TEXTURE_USAGE_TRANSFER_DST &&
                 dst_usage == TextureUsage::TEXTURE_USAGE_SAMPLE)
        {
            AcquireImageOwnerShip(
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                transfer_queue_.index,
                graphics_queue_.index,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                graphics_command_pool_,
                graphics_queue_.queue,
                base_mip_level, level_count);
        }
    }

    void VulkanBackend::GenerateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mip_levels)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        int32_t mip_width = width;
        int32_t mip_height = height;
        VkCommandBuffer command_buffer = BeginSingleTimeCommands(graphics_command_pool_);
        for (uint32_t i = 1; i < mip_levels; i++)
        {
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.subresourceRange.baseMipLevel = i - 1;

            vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mip_width, mip_height, 1};

            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mip_width > 1 ? mip_width / 2 : 1, mip_height > 1 ? mip_height / 2 : 1, 1};

            vkCmdBlitImage(
                command_buffer,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            if (mip_width > 1)
            {
                mip_width /= 2;
            }
            if (mip_height > 1)
            {
                mip_height /= 2;
            }
        }
        EndSingleTimeCommands(command_buffer, graphics_command_pool_, graphics_queue_.queue);
    }

    void VulkanBackend::CopyBufferToImage(BufferHandle buffer_handle, VkImage image, uint32_t width, uint32_t height)
    {
        VulkanBufferResource *buffer_resource = buffer_pool_->GetBufferResource(buffer_handle);
        VkCommandBuffer command_buffer = BeginSingleTimeCommands(transfer_command_pool_);

        VkBufferImageCopy region{};
        region.bufferRowLength = 0;
        region.bufferOffset = 0;
        region.bufferImageHeight = 0;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        vkCmdCopyBufferToImage(command_buffer, buffer_resource->buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        EndSingleTimeCommands(command_buffer, transfer_command_pool_, transfer_queue_.queue);
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
        CreateDepthResource();
        CreateColorResource();
        CreateFrameBuffers();
    }

    void VulkanBackend::CleanupSwapchain()
    {

        for (size_t i = 0; i < swapchain_framebuffers_.size(); i++)
        {
            vkDestroyFramebuffer(logical_device_, swapchain_framebuffers_[i], nullptr);
        }
        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = static_cast<void *>(&context_);
        texture_manager_->DestroyTexture(context, depth_handle);
        texture_manager_->DestroyTexture(context, color_handle);

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

        std::array<VkClearValue, 2> clear_values;
        clear_values[0].color = {0.f, 0.f, 0.f, 1.f};
        clear_values[1].depthStencil = {1.f, 0};

        VkRenderPassBeginInfo render_pass_begin_info{};
        render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass = swapchain_renderpass_;

        render_pass_begin_info.framebuffer = swapchain_framebuffers_[image_index];

        render_pass_begin_info.renderArea = render_area;
        render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
        render_pass_begin_info.pClearValues = clear_values.data();

        vkCmdBeginRenderPass(commandbuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
        {
            VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline_handle);
            vkCmdBindPipeline(commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_resource->pipeline);

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

            VulkanBufferResource *index_buffer_resource = buffer_pool_->GetBufferResource(index_handle_);
            VulkanBufferResource *pos_buffer_resource = buffer_pool_->GetBufferResource(pos_handle_);

            VkBuffer vertexBuffers[] = {pos_buffer_resource->buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandbuffer, 0, 1, vertexBuffers, offsets);

            uint32_t index_count = static_cast<uint32_t>(mesh_resource.indices.size());
            vkCmdBindIndexBuffer(commandbuffer, index_buffer_resource->buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdBindDescriptorSets(commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_resource->layout, 0, 1, &descriptor_sets_[current_frame], 0, nullptr);

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

        VkPhysicalDeviceFeatures physical_device_features{};
        vkGetPhysicalDeviceFeatures(device, &physical_device_features);

        bool is_swap_adequate = false;
        if (is_swapchain_supported)
        {
            SwapchainSupportDetail swap_chain_details = SwapchainSupportDetail::FindSwapchainSupports(device, surface_);
            is_swap_adequate = !swap_chain_details.surface_formats.empty() && !swap_chain_details.present_modes.empty();
        }

        return queue_family_indices.IsComplete() && is_swapchain_supported && is_swap_adequate && physical_device_features.samplerAnisotropy;
    }

    VkSurfaceFormatKHR VulkanBackend::ChooseSwapChainSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available_formats) const
    {
        for (const auto &avail_format : available_formats)
        {
            if (avail_format.format == VK_FORMAT_R8G8B8A8_SRGB &&
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

    uint32_t VulkanBackend::GetMaxUsableSampleCount()
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device_, &properties);

        VkSampleCountFlags sample_count = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
        if (sample_count & VK_SAMPLE_COUNT_64_BIT)
        {
            return 64;
        }
        if (sample_count & VK_SAMPLE_COUNT_32_BIT)
        {
            return 32;
        }
        if (sample_count & VK_SAMPLE_COUNT_16_BIT)
        {
            return 16;
        }
        if (sample_count & VK_SAMPLE_COUNT_8_BIT)
        {
            return 8;
        }
        if (sample_count & VK_SAMPLE_COUNT_4_BIT)
        {
            return 4;
        }
        if (sample_count & VK_SAMPLE_COUNT_2_BIT)
        {
            return 2;
        }
        return 1;
    }

    VulkanBackend::~VulkanBackend() = default;
}