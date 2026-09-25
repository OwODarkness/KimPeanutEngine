#include "vulkan_device.h"

#include <set>
#include <string>
#include <algorithm>

#include <GLFW/glfw3.h>

#include "log/logger.h"

namespace kpengine::graphics
{
#define KP_VULKAN_DEVICE_LOG_NAME "VulkanDeviceLog"

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
        VulkanDevice *const device = static_cast<VulkanDevice *>(pUserData);
        if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            KP_LOG(KP_VULKAN_DEVICE_LOG_NAME, LOG_LEVEL_ERROR, pCallbackData->pMessage);
            if (device)
            {
                device->RecordValidationError(pCallbackData->pMessage);
            }
        }
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            KP_LOG(KP_VULKAN_DEVICE_LOG_NAME, LOG_LEVEL_WARNING, pCallbackData->pMessage);
        }
        return VK_FALSE;
    }

    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo,
                                          void *user_data)
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
        createInfo.pUserData = user_data;
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

    void VulkanDevice::Initialize(GLFWwindow *window)
    {
        CreateInstance();
        CreateDebugMessager();
        CreateSurface(window);
        CreatePhysicalDevice();
        CreateLogicalDevice();
    }

    void VulkanDevice::Destroy()
    {
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

    std::optional<std::string> VulkanDevice::GetValidationDiagnostic() const
    {
        if (validation_error_count_.load(std::memory_order_relaxed) == 0)
        {
            return std::nullopt;
        }
        std::lock_guard<std::mutex> lock(validation_error_mutex_);
        return last_validation_error_.empty()
                   ? std::optional<std::string>{"Vulkan validation reported an error"}
                   : std::optional<std::string>{last_validation_error_};
    }

    void VulkanDevice::RecordValidationError(const char *message) noexcept
    {
        validation_error_count_.fetch_add(1, std::memory_order_relaxed);
        try
        {
            std::lock_guard<std::mutex> lock(validation_error_mutex_);
            last_validation_error_ = message ? message : "Vulkan validation reported an error";
        }
        catch (...)
        {
            // The atomic count remains authoritative when diagnostic storage
            // cannot allocate from the validation callback.
        }
    }

    void VulkanDevice::CreateInstance()
    {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "KimPeanut Engine";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 2, 0);
        app_info.pEngineName = "No Engine";
        app_info.engineVersion = VK_MAKE_VERSION(1, 2, 0);
        app_info.apiVersion = VK_API_VERSION_1_3;

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
                PopulateDebugMessengerCreateInfo(debug_create_info, this);
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
            KP_LOG(KP_VULKAN_DEVICE_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create instance");
            throw std::runtime_error("Failed to create instance");
        }
    }

    void VulkanDevice::CreateDebugMessager()
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
        PopulateDebugMessengerCreateInfo(debug_messager_create_info, this);
        if (CreateDebugUtilsMessengerEXT(instance_, &debug_messager_create_info, nullptr, &debug_messager_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_DEVICE_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create debug messager");
            throw std::runtime_error("Failed to create debug messager");
        }
    }

    void VulkanDevice::CreateSurface(GLFWwindow *window)
    {
        if (glfwCreateWindowSurface(instance_, window, nullptr, &surface_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_DEVICE_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create surface");
            throw std::runtime_error("Failed to create surface");
        }
    }

    void VulkanDevice::CreatePhysicalDevice()
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
            KP_LOG(KP_VULKAN_DEVICE_LOG_NAME, LOG_LEVEL_ERROR, "Failed to find suitable GPU");
            throw std::runtime_error("Failed to find suitable GPU");
        }
        else
        {
            physical_device_ = best_device;
            ray_tracing_enabled_ = QueryRayTracingSupport(physical_device_);
            ray_tracing_pipeline_enabled_ =
                ray_tracing_enabled_ && QueryRayTracingPipelineSupport(physical_device_);
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physical_device_, &props);
            std::string message = props.deviceName;
            message += " is ready";
            KP_LOG(KP_VULKAN_DEVICE_LOG_NAME, LOG_LEVEL_INFO, message);
        }
    }

    void VulkanDevice::CreateLogicalDevice()
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

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.features.samplerAnisotropy = VK_TRUE;

        VkPhysicalDeviceVulkan13Features device13_feature{};
        device13_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        device13_feature.dynamicRendering = VK_TRUE;
        device13_feature.synchronization2 = VK_TRUE;

        VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing{};
        descriptor_indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        uint32_t bindless_capacity = 0;
        bindless_textures_enabled_ = QueryBindlessTextureSupport(physical_device_, bindless_capacity);
        bindless_texture_table_capacity_ = bindless_textures_enabled_ ? bindless_capacity : 0;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features{};
        acceleration_structure_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{};
        ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features{};
        buffer_device_address_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features{};
        ray_tracing_pipeline_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

        if (ray_tracing_enabled_)
        {
            acceleration_structure_features.accelerationStructure = VK_TRUE;
            ray_query_features.rayQuery = VK_TRUE;
            buffer_device_address_features.bufferDeviceAddress = VK_TRUE;
            if (ray_tracing_pipeline_enabled_)
            {
                ray_tracing_pipeline_features.rayTracingPipeline = VK_TRUE;
            }
        }

        std::vector<const char *> enabled_extensions = device_extensions;
        if (ray_tracing_enabled_)
        {
            enabled_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            enabled_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            enabled_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            enabled_extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            if (ray_tracing_pipeline_enabled_)
            {
                enabled_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            }
        }

        if (bindless_textures_enabled_)
        {
            descriptor_indexing.runtimeDescriptorArray = VK_TRUE;
            descriptor_indexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            descriptor_indexing.descriptorBindingPartiallyBound = VK_TRUE;
            descriptor_indexing.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            descriptor_indexing.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
            features2.pNext = &descriptor_indexing;
            descriptor_indexing.pNext = &device13_feature;
        }
        else
        {
            features2.pNext = &device13_feature;
        }

        VkBaseOutStructure *feature_tail =
            reinterpret_cast<VkBaseOutStructure *>(&device13_feature);
        if (ray_tracing_enabled_)
        {
            feature_tail->pNext = reinterpret_cast<VkBaseOutStructure *>(
                &acceleration_structure_features);
            feature_tail = reinterpret_cast<VkBaseOutStructure *>(
                &acceleration_structure_features);
            feature_tail->pNext = reinterpret_cast<VkBaseOutStructure *>(&ray_query_features);
            feature_tail = reinterpret_cast<VkBaseOutStructure *>(&ray_query_features);
            feature_tail->pNext = reinterpret_cast<VkBaseOutStructure *>(
                &buffer_device_address_features);
            feature_tail = reinterpret_cast<VkBaseOutStructure *>(
                &buffer_device_address_features);
            if (ray_tracing_pipeline_enabled_)
            {
                feature_tail->pNext = reinterpret_cast<VkBaseOutStructure *>(
                    &ray_tracing_pipeline_features);
            }
        }

        VkDeviceCreateInfo device_create_info{};
        device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        device_create_info.ppEnabledExtensionNames = enabled_extensions.data();
        device_create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
        device_create_info.pQueueCreateInfos = queue_create_infos.data();
        device_create_info.enabledLayerCount = 0;
        device_create_info.ppEnabledLayerNames = nullptr;
        device_create_info.pEnabledFeatures = nullptr;
        device_create_info.pNext = &features2;

        if (vkCreateDevice(physical_device_, &device_create_info, nullptr, &logical_device_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_DEVICE_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create logicial device");
            throw std::runtime_error("Failed to find logicial device");
        }

        vkGetDeviceQueue(logical_device_, graphics_queue_.index, 0, &graphics_queue_.queue);
        vkGetDeviceQueue(logical_device_, present_queue_.index, 0, &present_queue_.queue);
        vkGetDeviceQueue(logical_device_, transfer_queue_.index, 0, &transfer_queue_.queue);
    }

    std::vector<const char *> VulkanDevice::FindRequiredExtensions() const
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

    bool VulkanDevice::CheckValidationLayerSupport(const std::vector<const char *> &validation_layers) const
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

    bool VulkanDevice::CheckDeviceExtensionsSupport(VkPhysicalDevice device, const std::vector<const char *> &extensions) const
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

    bool VulkanDevice::CheckPhysicalDeviceSuitable(VkPhysicalDevice device) const
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

    bool VulkanDevice::QueryBindlessTextureSupport(VkPhysicalDevice device, uint32_t &capacity) const
    {
        VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing{};
        descriptor_indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &descriptor_indexing;
        vkGetPhysicalDeviceFeatures2(device, &features);
        if (!descriptor_indexing.runtimeDescriptorArray ||
            !descriptor_indexing.shaderSampledImageArrayNonUniformIndexing ||
            !descriptor_indexing.descriptorBindingPartiallyBound ||
            !descriptor_indexing.descriptorBindingSampledImageUpdateAfterBind ||
            !descriptor_indexing.descriptorBindingUpdateUnusedWhilePending)
        {
            capacity = 0;
            return false;
        }

        VkPhysicalDeviceDescriptorIndexingProperties descriptor_properties{};
        descriptor_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &descriptor_properties;
        vkGetPhysicalDeviceProperties2(device, &properties);
        capacity = std::min({descriptor_properties.maxDescriptorSetUpdateAfterBindSampledImages,
                             descriptor_properties.maxDescriptorSetUpdateAfterBindSamplers,
                             descriptor_properties.maxPerStageDescriptorUpdateAfterBindSampledImages,
                             descriptor_properties.maxPerStageDescriptorUpdateAfterBindSamplers,
                             4096u});
        return capacity > 0;
    }

    bool VulkanDevice::QueryRayTracingSupport(VkPhysicalDevice device) const
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (VK_VERSION_MINOR(properties.apiVersion) < 2)
        {
            return false;
        }

        const std::vector<const char *> required_extensions = {
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};
        if (!CheckDeviceExtensionsSupport(device, required_extensions))
        {
            return false;
        }

        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features{};
        acceleration_structure_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{};
        ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features{};
        buffer_device_address_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        acceleration_structure_features.pNext = &ray_query_features;
        ray_query_features.pNext = &buffer_device_address_features;

        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &acceleration_structure_features;
        vkGetPhysicalDeviceFeatures2(device, &features);
        return acceleration_structure_features.accelerationStructure == VK_TRUE &&
               ray_query_features.rayQuery == VK_TRUE &&
               buffer_device_address_features.bufferDeviceAddress == VK_TRUE;
    }

    bool VulkanDevice::QueryRayTracingPipelineSupport(VkPhysicalDevice device) const
    {
        if (!CheckDeviceExtensionsSupport(
                device, {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME}))
        {
            return false;
        }
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR pipeline_features{};
        pipeline_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &pipeline_features;
        vkGetPhysicalDeviceFeatures2(device, &features);
        return pipeline_features.rayTracingPipeline == VK_TRUE;
    }
}
