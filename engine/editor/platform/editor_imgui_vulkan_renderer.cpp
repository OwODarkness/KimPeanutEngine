#include "editor/platform/editor_imgui_vulkan_renderer.h"
#include <array>
#include <stdexcept>
#include "imgui_impl_vulkan.h"
#include "log/logger.h"
#include "graphics/backend/vulkan/vulkan_editor_bridge.h"
namespace kpengine::editor
{
    constexpr const char* LogName = "EditorImguiVulkanRendererLog";
    bool EditorImguiVulkanRenderer::Initialize(
        graphics::IEditorPresentationBridge *presentation_bridge)
    {
        auto *const vulkan_bridge = dynamic_cast<graphics::VulkanEditorBridge *>(
            presentation_bridge);
        if (vulkan_bridge == nullptr)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "Vulkan editor presentation bridge is unavailable");
            throw std::runtime_error("Vulkan editor presentation bridge is unavailable");
        }
        try
        {
            editor_bridge_ = vulkan_bridge;
            const graphics::VulkanEditorBridgeInfo bridge_info = editor_bridge_->GetInfo();
            logical_device_ = bridge_info.logical_device;
            CreateDescriptorPool();
            CreateSceneSampler();

            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.Instance = bridge_info.instance;
            init_info.PhysicalDevice = bridge_info.physical_device;
            init_info.Device = bridge_info.logical_device;
            init_info.QueueFamily = bridge_info.graphics_queue_family;
            init_info.Queue = bridge_info.graphics_queue;
            init_info.DescriptorPool = descriptor_pool_;
            init_info.MinImageCount = 2;
            init_info.ImageCount = bridge_info.image_count;
            init_info.UseDynamicRendering = true;
#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
            const VkFormat color_format = bridge_info.color_format;
            init_info.PipelineRenderingCreateInfo.sType =
                VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_format;
#endif

            if (!ImGui_ImplVulkan_Init(&init_info))
            {
                throw std::runtime_error("Failed to initialize ImGui Vulkan renderer");
            }
            imgui_backend_initialized_ = true;
            return true;
        }
        catch (...)
        {
            Shutdown();
            throw;
        }
    }

    void EditorImguiVulkanRenderer::Shutdown()
    {
        // The most recently submitted frame can still reference ImGui's scene
        // descriptor and transient vertex/index buffers. Retire it before any
        // ImGui Vulkan destruction; RenderSystem's later shutdown wait is too
        // late because this renderer owns the resources being released here.
        if (editor_bridge_)
        {
            editor_bridge_->WaitIdle();
        }
        ReleaseSceneTextures();
        if (imgui_backend_initialized_)
        {
            ImGui_ImplVulkan_Shutdown();
            imgui_backend_initialized_ = false;
        }
        if (descriptor_pool_ != VK_NULL_HANDLE && logical_device_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(logical_device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
        if (scene_sampler_ != VK_NULL_HANDLE && logical_device_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(logical_device_, scene_sampler_, nullptr);
            scene_sampler_ = VK_NULL_HANDLE;
        }
        logical_device_ = VK_NULL_HANDLE;
        editor_bridge_ = nullptr;
    }

    void EditorImguiVulkanRenderer::NewFrame()
    {
        ImGui_ImplVulkan_NewFrame();
    }

    void EditorImguiVulkanRenderer::Render()
    {
        if (!editor_bridge_)
        {
            return;
        }
        ImDrawData *draw_data = ImGui::GetDrawData();
        editor_bridge_->Record([draw_data](VkCommandBuffer command_buffer)
        {
            ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
        });
    }

    void EditorImguiVulkanRenderer::SetBackgroundColor(const LogColor &color)
    {
        background_color_ = color;
    }

    ImTextureID EditorImguiVulkanRenderer::GetTextureID(const graphics::RenderTargetView &view)
    {
        if (!view.IsValid() || !scene_sampler_)
        {
            return ImTextureID{};
        }

        const uintptr_t image_view_key = view.native_image_view;
        const auto existing_texture = scene_textures_.find(image_view_key);
        if (existing_texture == scene_textures_.end())
        {
            const VkImageView image_view = reinterpret_cast<VkImageView>(view.native_image_view);
            const VkDescriptorSet descriptor_set = ImGui_ImplVulkan_AddTexture(
                scene_sampler_, image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            scene_textures_.emplace(image_view_key, descriptor_set);
            return reinterpret_cast<ImTextureID>(descriptor_set);
        }
        return reinterpret_cast<ImTextureID>(existing_texture->second);
    }

    void EditorImguiVulkanRenderer::DrawSceneImage(ImTextureID texture_id, const ImVec2 &size)
    {
        ImGui::Image(texture_id, size);
    }

    void EditorImguiVulkanRenderer::CreateDescriptorPool()
    {
        constexpr uint32_t kMaxDescriptorSets = 1'000;
        const std::array<VkDescriptorPoolSize, 11> pool_sizes{{
            {VK_DESCRIPTOR_TYPE_SAMPLER, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, kMaxDescriptorSets},
        }};
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = kMaxDescriptorSets * static_cast<uint32_t>(pool_sizes.size());
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        if (vkCreateDescriptorPool(logical_device_, &pool_info, nullptr,
                                   &descriptor_pool_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create ImGui Vulkan descriptor pool");
        }
    }

    void EditorImguiVulkanRenderer::CreateSceneSampler()
    {
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 0.0f;
        if (vkCreateSampler(logical_device_, &sampler_info, nullptr,
                            &scene_sampler_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create ImGui scene sampler");
        }
    }

    void EditorImguiVulkanRenderer::ReleaseSceneTextures()
    {
        for (const auto &[image_view, descriptor_set] : scene_textures_)
        {
            static_cast<void>(image_view);
            if (descriptor_set != VK_NULL_HANDLE)
            {
                ImGui_ImplVulkan_RemoveTexture(descriptor_set);
            }
        }
        scene_textures_.clear();
    }

} // namespace kpengine::editor
