#include "vulkan_backend.h"
#include <array>
#include <GLFW/glfw3.h>
#include "log/logger.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_swapchain.h"
#include "vulkan_frame_context.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_image_memory_manager.h"
#include "vulkan_texture.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "common/mesh_manager.h"

namespace kpengine::graphics
{

#define KP_VULKAN_BACKEND_LOG_NAME "VulkanBackendLog"

    VulkanBackend::VulkanBackend() : buffer_manager_(std::make_unique<VulkanBufferManager>()),
                                     pipeline_manager_(std::make_unique<VulkanPipelineManager>()),
                                     image_memory_manager_(std::make_unique<VulkanImageMemoryManager>()),
                                     texture_manager_(std::make_unique<TextureManager>()),
                                     sampler_manager_(std::make_unique<SamplerManager>()),
                                     mesh_manager_(std::make_unique<MeshManager>())
    {
    }

    void VulkanBackend::Initialize(const PipelineDesc &pipeline_desc, WindowHandle native_window)
    {
        // The native window (WindowHandle = void*) is cast back to GLFW here — the
        // Vulkan surface + swapchain need it; the common facade never sees GLFW.
        GLFWwindow *window = static_cast<GLFWwindow *>(native_window);

        device_ = std::make_unique<VulkanDevice>();
        device_->Initialize(window);

        swapchain_ = std::make_unique<VulkanSwapchain>();
        swapchain_->Initialize(device_.get(), window);
        msaa_sampe_count_ = swapchain_->GetMaxUsableSampleCount();
        InitVulkanContext();

        frame_context_ = std::make_unique<VulkanFrameContext>();
        frame_context_->Initialize(device_.get(), static_cast<uint32_t>(swapchain_->GetImageCount()));

        CreateGraphicsPipeline(pipeline_desc);

        // Swapchain-bound render targets — RHI-owned, sized to the swapchain.
        CreateDepthResource();
        CreateColorResource();
    }

    void VulkanBackend::BeginFrame()
    {
        // 1. wait for last frame to finish
        // 2. acquire RT + prepare the frame's scene command buffer
        // 3. caller records draws, EndFrame submits and presents

        frame_context_->WaitForInFlightFence();

        uint32_t image_index;
        VkResult acquire_image_res = frame_context_->AcquireNextImage(swapchain_->GetSwapchain(), image_index);

        if (acquire_image_res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchain();
            frame_active_ = false;
            return;
        }
        else if (acquire_image_res != VK_SUCCESS && acquire_image_res != VK_SUBOPTIMAL_KHR)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to acquire image");
            throw std::runtime_error("Failed to acquire image");
        }

        frame_context_->ResetInFlightFence();
        frame_context_->ResetCurrentSceneCommandBuffer();

        VkCommandBuffer scene_command_buffer = frame_context_->GetCurrentSceneCommandBuffer();
        BeginSceneFrame(scene_command_buffer, image_index);

        current_image_index_ = image_index;
        frame_active_ = true;
    }

    void VulkanBackend::EndFrame()
    {
        if (!frame_active_)
        {
            return;
        }

        VkCommandBuffer scene_command_buffer = frame_context_->GetCurrentSceneCommandBuffer();
        EndSceneFrame(scene_command_buffer, current_image_index_);

        frame_context_->Submit(scene_command_buffer, current_image_index_);

        VkResult present_res = frame_context_->Present(swapchain_->GetSwapchain(), current_image_index_);
        if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR || swapchain_->HasResized())
        {
            RecreateSwapchain();
            swapchain_->ClearResized();
        }
        else if (present_res != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to present");
            throw std::runtime_error("Failed to present");
        }

        frame_context_->AdvanceFrame();
        frame_active_ = false;
    }

    void VulkanBackend::Present()
    {
    }

    void VulkanBackend::Cleanup()
    {
        vkDeviceWaitIdle(device_->GetLogicalDevice());

        CleanupSwapchain();

        // The demo scene owns its mesh/texture/UBO/descriptor handles and destroys
        // them before the backend; only backend-owned GPU state lives here now.
        pipeline_manager_->DestroyPipelineResource(device_->GetLogicalDevice(), pipeline_handle_);

        buffer_manager_->FreeMemory(device_->GetLogicalDevice());
        image_memory_manager_->Destroy(device_->GetLogicalDevice());

        frame_context_->Destroy();

        device_->Destroy();
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
        swapchain_->MarkResized();
    }

    void VulkanBackend::InitVulkanContext()
    {
        context_.backend = this;
        context_.instance = device_->GetInstance();
        context_.physical_device = device_->GetPhysicalDevice();
        context_.logical_device = device_->GetLogicalDevice();
    }

    void VulkanBackend::CreateGraphicsPipeline(const PipelineDesc &pipeline_desc)
    {
        // The caller owns shaders/stage/layout/bindings; the backend bakes only. The
        // swapchain is RHI-owned, so its format completes a desc that left the
        // attachment formats unset — a swapchain-bound pipeline must match them.
        PipelineDesc desc = pipeline_desc;
        if (desc.color_attachment_formats.empty())
        {
            desc.color_attachment_formats = {ConvertFromVulkanTextureFormat(swapchain_->GetImageFormat())};
        }
        if (desc.depth_attachment_format == TextureFormat::TEXTURE_FORMAT_UNKNOW)
        {
            desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
        }
        pipeline_handle_ = pipeline_manager_->CreatePipelineResource(device_->GetLogicalDevice(), desc);
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
        depth_settings.sample_count = 1;
        TextureData depth_data{};
        depth_data.width = swapchain_->GetExtent().width;
        depth_data.height = swapchain_->GetExtent().height;
        depth_handle_ = texture_manager_->CreateTexture(context, depth_data, depth_settings);
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
        color_data.width = swapchain_->GetExtent().width;
        color_data.height = swapchain_->GetExtent().height;
        color_handle_ = texture_manager_->CreateTexture(context, color_data, color_settings);
    }

    BufferHandle VulkanBackend::CreateUploadStageBufferResource(size_t size)
    {
        VkBufferCreateInfo stage_buffer_create_info{};
        stage_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stage_buffer_create_info.size = size;
        stage_buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stage_buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return buffer_manager_->CreateBufferResource(device_->GetPhysicalDevice(), device_->GetLogicalDevice(), &stage_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_STAGING);
    }

    BufferHandle VulkanBackend::CreateDownloadStageBufferResource(size_t size)
    {
        VkBufferCreateInfo stage_buffer_create_info{};
        stage_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stage_buffer_create_info.size = size;
        stage_buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        stage_buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return buffer_manager_->CreateBufferResource(device_->GetPhysicalDevice(), device_->GetLogicalDevice(), &stage_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_STAGING);
    }

    bool VulkanBackend::DestroyBufferResource(BufferHandle handle)
    {
        return buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), handle);
    }

    void VulkanBackend::UploadDataToBuffer(BufferHandle handle, size_t size, const void *data)
    {
        buffer_manager_->UploadData(device_->GetLogicalDevice(), handle, size, data);
    }

    VulkanBufferResource *VulkanBackend::GetBufferResource(BufferHandle handle)
    {
        return buffer_manager_->GetBufferResource(handle);
    }

    BufferHandle VulkanBackend::CreateBuffer(const void *data, size_t size, VkBufferUsageFlags usage)
    {
        BufferHandle stage_handle = CreateUploadStageBufferResource(size);

        UploadDataToBuffer(stage_handle, size, data);

        VkBufferCreateInfo dst_buffer_create_info{};
        dst_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dst_buffer_create_info.size = size;
        dst_buffer_create_info.usage = usage;
        dst_buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        BufferHandle dst_handle = buffer_manager_->CreateBufferResource(device_->GetPhysicalDevice(), device_->GetLogicalDevice(), &dst_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_DEVICE);

        // stage → device copy on the transfer queue, one-shot buffer
        VkCommandBuffer command_buffer = frame_context_->BeginSingleTimeCommands(frame_context_->GetTransferCommandPool());
        VkBufferCopy copy_region{};
        copy_region.size = size;
        vkCmdCopyBuffer(command_buffer, buffer_manager_->GetBufferResource(stage_handle)->buffer, buffer_manager_->GetBufferResource(dst_handle)->buffer, 1, &copy_region);
        frame_context_->EndSingleTimeCommands(command_buffer, frame_context_->GetTransferCommandPool(), device_->GetTransferQueue().queue);

        DestroyBufferResource(stage_handle);

        return dst_handle;
    }

    VkCommandBuffer VulkanBackend::GetCurrentUICommandBuffer() const
    {
        return frame_context_->GetCurrentUICommandBuffer();
    }

    VkCommandBuffer VulkanBackend::GetCurrentSceneCommandBuffer() const
    {
        return frame_context_->GetCurrentSceneCommandBuffer();
    }

    uint32_t VulkanBackend::GetCurrentFrameIndex() const
    {
        return frame_context_->GetCurrentFrameIndex();
    }

    VkExtent2D VulkanBackend::GetSwapchainExtent() const
    {
        return swapchain_->GetExtent();
    }

    const VulkanPipelineResource *VulkanBackend::GetPipelineResource() const
    {
        return pipeline_manager_->GetPipelineResource(pipeline_handle_);
    }

    BufferHandle VulkanBackend::CreateUniformBuffer(uint32_t size)
    {
        VkBufferCreateInfo buffer_create_info{};
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        buffer_create_info.queueFamilyIndexCount = 0;
        buffer_create_info.pQueueFamilyIndices = nullptr;
        buffer_create_info.size = size;
        return buffer_manager_->CreateBufferResource(device_->GetPhysicalDevice(), device_->GetLogicalDevice(), &buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM);
    }

    void *VulkanBackend::MapUniformBuffer(BufferHandle handle, size_t size)
    {
        void *mapped_ptr = nullptr;
        buffer_manager_->MapBuffer(device_->GetLogicalDevice(), handle, size, &mapped_ptr);
        return mapped_ptr;
    }

    void VulkanBackend::UploadTexturePixels(TextureHandle texture, const void *pixels, size_t pixel_size, uint32_t width, uint32_t height, uint32_t mip_levels)
    {
        Texture *texture_entity = texture_manager_->GetTexture(texture);
        if (!texture_entity)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "UploadTexturePixels: invalid texture handle");
            return;
        }
        VkImage image = ConvertToVulkanTextureResource(texture_entity->GetTextueHandle()).image;

        BufferHandle stage_handle = CreateUploadStageBufferResource(pixel_size);
        UploadDataToBuffer(stage_handle, pixel_size, pixels);

        // one-shot upload: begin, record, submit, wait — buffer freed in End
        VkCommandBuffer command_buffer = frame_context_->BeginSingleTimeCommands(frame_context_->GetGraphicsCommandPool());
        frame_context_->TransitionImageLayout(command_buffer, image, TextureUsage::None, TextureUsage::TEXTURE_USAGE_TRANSFER_DST, 0, mip_levels);
        frame_context_->CopyBufferToImage(command_buffer, buffer_manager_->GetBufferResource(stage_handle)->buffer, image, width, height);
        frame_context_->TransitionImageLayout(command_buffer, image, TextureUsage::TEXTURE_USAGE_TRANSFER_DST, TextureUsage::TEXTURE_USAGE_SAMPLE, 0, mip_levels);
        frame_context_->EndSingleTimeCommands(command_buffer, frame_context_->GetGraphicsCommandPool(), device_->GetGraphicsQueue().queue);

        DestroyBufferResource(stage_handle);
    }

    void VulkanBackend::RecreateSwapchain()
    {
        while (height_ == 0 || width_ == 0)
        {
            glfwWaitEvents();
        }

        DestroyAttachmentResources();
        swapchain_->Recreate(width_, height_);
        frame_context_->OnSwapchainRecreated(static_cast<uint32_t>(swapchain_->GetImageCount()));
        CreateDepthResource();
        CreateColorResource();
    }

    void VulkanBackend::CleanupSwapchain()
    {
        DestroyAttachmentResources();
        swapchain_->Cleanup();
    }

    void VulkanBackend::DestroyAttachmentResources()
    {
        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = static_cast<void *>(&context_);
        texture_manager_->DestroyTexture(context, depth_handle_);
        texture_manager_->DestroyTexture(context, color_handle_);
    }

    void VulkanBackend::BeginSceneFrame(VkCommandBuffer commandbuffer, uint32_t image_index)
    {
        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.pInheritanceInfo = nullptr;

        if (vkBeginCommandBuffer(commandbuffer, &command_buffer_begin_info) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to begin command buffer");
            throw std::runtime_error("Failed to begin command buffer");
        }

        std::array<VkClearValue, 2> clear_values;
        clear_values[0].color = {0.f, 0.f, 0.f, 1.f};
        clear_values[1].depthStencil = {1.f, 0};

        VkRenderingAttachmentInfo color_attachment_info{};
        color_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment_info.imageView = swapchain_->GetImageView(image_index);
        color_attachment_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment_info.clearValue = clear_values[0];

        Texture *depth_tex = texture_manager_->GetTexture(depth_handle_);
        VulkanTextureResource depth_resource = ConvertToVulkanTextureResource(depth_tex->GetTextueHandle());

        VkRenderingAttachmentInfo depth_attachment_info{};
        depth_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment_info.imageView = depth_resource.view;
        depth_attachment_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment_info.clearValue = clear_values[1];

        VkRect2D render_area{};
        render_area.extent = swapchain_->GetExtent();
        render_area.offset.x = 0;
        render_area.offset.y = 0;

        VkRenderingInfo render_info{};
        render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        render_info.renderArea = render_area;
        render_info.layerCount = 1;
        render_info.colorAttachmentCount = 1;
        render_info.pColorAttachments = &color_attachment_info;
        render_info.pDepthAttachment = &depth_attachment_info;

        // swapchain image + depth into their attachment layouts before rendering
        frame_context_->TransitionImageLayout(
            commandbuffer,
            swapchain_->GetImage(image_index),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            {}, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

        frame_context_->TransitionImageLayout(
            commandbuffer,
            depth_resource.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1);

        vkCmdBeginRendering(commandbuffer, &render_info);

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchain_->GetExtent().width);
        viewport.height = static_cast<float>(swapchain_->GetExtent().height);
        viewport.maxDepth = 1.f;
        viewport.minDepth = 0.f;
        viewport.x = 0.f;
        viewport.y = 0.f;
        vkCmdSetViewport(commandbuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = swapchain_->GetExtent();
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        vkCmdSetScissor(commandbuffer, 0, 1, &scissor);
    }

    void VulkanBackend::EndSceneFrame(VkCommandBuffer commandbuffer, uint32_t image_index)
    {
        vkCmdEndRendering(commandbuffer);

        // swapchain image back to presentable after the caller's draws
        frame_context_->TransitionImageLayout(
            commandbuffer,
            swapchain_->GetImage(image_index),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, {},
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

        if (vkEndCommandBuffer(commandbuffer) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to end record command buffer");
            throw std::runtime_error("Failed to end record command buffer");
        }
    }

    VulkanBackend::~VulkanBackend() = default;
}
