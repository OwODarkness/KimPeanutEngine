#include "editor/platform/editor_imgui_vulkan_renderer.h"
#include <array>
#include <cassert>
#include "imgui_impl_vulkan.h"
#include "log/logger.h"
#include "graphics/backend/vulkan/vulkan_context.h"
#include "graphics/backend/vulkan/vulkan_backend.h"
namespace kpengine::editor
{
    constexpr const char* LogName = "EditorImguiVulkanRendererLog";
    void EditorImguiVulkanRenderer::Initialize(GraphicsContext context)
    {
        if(context.type != GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "Graphics api mismatch, current type is not OpenGL");
            throw std::runtime_error("Graphics api mismatch, current type is not OpenGL");
        }
        vulkan_ctx = static_cast<graphics::VulkanContext*>(context.native);
        assert(vulkan_ctx);
        CreateDescriptorPool();

        const graphics::VulkanQueue &graphics_queue = vulkan_ctx->backend->GetGraphicsQueue();
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = vulkan_ctx->instance;
        init_info.PhysicalDevice = vulkan_ctx->physical_device;
        init_info.Device = vulkan_ctx->logical_device;
        init_info.QueueFamily = graphics_queue.index;
        init_info.Queue = graphics_queue.queue;
        init_info.DescriptorPool = descriptor_pool_;
        init_info.MinImageCount = 2;
        init_info.ImageCount = vulkan_ctx->backend->GetSwapchainImageCount();
        init_info.UseDynamicRendering = true;
#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
        const VkFormat color_format = vulkan_ctx->backend->GetSwapchainImageFormat();
        init_info.PipelineRenderingCreateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_format;
#endif

        ImGui_ImplVulkan_Init(&init_info);
    }

    void EditorImguiVulkanRenderer::Shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
        if (descriptor_pool_ != VK_NULL_HANDLE && vulkan_ctx)
        {
            vkDestroyDescriptorPool(vulkan_ctx->logical_device, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
        vulkan_ctx = nullptr;
    }

    void EditorImguiVulkanRenderer::NewFrame()
    {
        ImGui_ImplVulkan_NewFrame();
    }

    void EditorImguiVulkanRenderer::Render()
    {
        VkCommandBuffer cmd_buf = vulkan_ctx->backend->GetCurrentUICommandBuffer();
        vkResetCommandBuffer(cmd_buf, 0);
        VkCommandBufferBeginInfo cmd_buf_begin_info{};
        cmd_buf_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmd_buf_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmd_buf, &cmd_buf_begin_info);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_buf);
    
        vkEndCommandBuffer(cmd_buf);

    }

    void EditorImguiVulkanRenderer::SetBackgroundColor(const LogColor &color)
    {
        background_color_ = color;
    }

    ImTextureID EditorImguiVulkanRenderer::GetTextureID(const graphics::RenderTargetView &view)
    {
        // Vulkan needs an ImGui descriptor set that owns the image-view/sampler
        // registration. That bridge is not initialized yet, so do not expose a
        // raw VkImageView as ImTextureID.
        (void)view;
        return ImTextureID{};
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
        if (vkCreateDescriptorPool(vulkan_ctx->logical_device, &pool_info, nullptr,
                                   &descriptor_pool_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create ImGui Vulkan descriptor pool");
        }
    }

} // namespace kpengine::editor
