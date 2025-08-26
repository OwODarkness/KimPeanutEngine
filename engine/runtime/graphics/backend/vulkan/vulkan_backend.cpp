#include "vulkan_backend.h"
#include <set>
#include <GLFW/glfw3.h>
#include "math/math_header.h"
#include "log/logger.h"

#define KP_VULKAN_BACKEND_LOG_NAME "VulkanBackendLog"
namespace kpengine::graphics
{
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

    void VulkanBackend::Initialize()
    {
        CreateInstance();
        CreateDebugMessager();
    }
    void VulkanBackend::BeginFrame()
    {
    }
    void VulkanBackend::EndFrame()
    {
    }

    void VulkanBackend::Present()
    {
    }

    void VulkanBackend::Cleanup()
    {
#ifdef NDEBUG
        const bool enable_validation_layers = false;
#else
        const bool enable_validation_layers = true;
#endif
        if(enable_validation_layers)
        {
            vkDestroyDebugUtilsMessengerEXT(instance_, debug_messager_, nullptr);
        }
        vkDestroyInstance(instance_, nullptr);
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

        const std::vector<const char *> extensions = GetRequiredExtensions();
        instance_create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instance_create_info.ppEnabledExtensionNames = extensions.data();

        // setting validation layer for instance
#ifdef NDEBUG
        const bool enable_validation_layers = false;
#else
        const bool enable_validation_layers = true;
#endif

        if (enable_validation_layers)
        {
            if (CheckValidationLayerSupport(validation_layers))
            {
                VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
                PopulateDebugMessengerCreateInfo(debug_create_info);
                instance_create_info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
                instance_create_info.ppEnabledLayerNames = validation_layers.data();
                instance_create_info.pNext = &debug_create_info;
            }
            {
                instance_create_info.enabledLayerCount = 0;
                instance_create_info.pNext = nullptr;
            }
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
        if(!enable_validation_layers)
        {
            return ;
        }

        VkDebugUtilsMessengerCreateInfoEXT debug_messager_create_info{};
        PopulateDebugMessengerCreateInfo(debug_messager_create_info);
        if(vkCreateDebugUtilsMessengerEXT(instance_, &debug_messager_create_info, nullptr, &debug_messager_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create debug messager");
            throw std::runtime_error("Failed to create debug messager");
        }
    }

    std::vector<const char *> VulkanBackend::GetRequiredExtensions() const
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
        std::set<const char *> required_layers(validation_layers.begin(), validation_layers.end());

        uint32_t prop_count{};
        vkEnumerateInstanceLayerProperties(&prop_count, nullptr);
        std::vector<VkLayerProperties> available_layer_props(prop_count);
        vkEnumerateInstanceLayerProperties(&prop_count, available_layer_props.data());

        // earse item from required_layer, if layername both appear in required and avial
        for (const auto &layer_prop : available_layer_props)
        {
            required_layers.erase(layer_prop.layerName);
        }

        return required_layers.empty();
    }
}