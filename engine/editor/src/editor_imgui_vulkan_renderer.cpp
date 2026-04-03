#include "editor_imgui_vulkan_renderer.h"
#include <cassert>
#include "imgui/imgui_impl_vulkan.h"
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
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = vulkan_ctx->instance;
        init_info.Device =vulkan_ctx->logical_device;

        ImGui_ImplVulkan_Init(&init_info);
    }

    void EditorImguiVulkanRenderer::Shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
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

} // namespace kpengine::editor
