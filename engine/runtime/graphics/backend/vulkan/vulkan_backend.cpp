#include "vulkan_backend.h"
#include <algorithm>
#include <array>
#include <GLFW/glfw3.h>
#include "log/logger.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_memory_manager.h"
#include "vulkan_swapchain.h"
#include "vulkan_frame_context.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_descriptor_set_manager.h"
#include "vulkan_image_memory_manager.h"
#include "vulkan_texture.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "common/mesh_manager.h"
#include "vulkan_mesh.h"

namespace kpengine::graphics
{

#define KP_VULKAN_BACKEND_LOG_NAME "VulkanBackendLog"

    VulkanBackend::VulkanBackend() : pipeline_manager_(std::make_unique<VulkanPipelineManager>()),
                                     descriptor_set_manager_(std::make_unique<VulkanDescriptorSetManager>()),
                                     image_memory_manager_(std::make_unique<VulkanImageMemoryManager>()),
                                     texture_manager_(std::make_unique<TextureManager>()),
                                     sampler_manager_(std::make_unique<SamplerManager>()),
                                     mesh_manager_(std::make_unique<MeshManager>())
    {
    }

    void VulkanBackend::Initialize(WindowHandle native_window)
    {
        // The native window (WindowHandle = void*) is cast back to GLFW here — the
        // Vulkan surface + swapchain need it; the common facade never sees GLFW.
        GLFWwindow *window = static_cast<GLFWwindow *>(native_window);

        device_ = std::make_unique<VulkanDevice>();
        device_->Initialize(window);
        memory_manager_ = std::make_unique<VulkanMemoryManager>(
            device_->GetPhysicalDevice(), device_->GetLogicalDevice());
        buffer_manager_ = std::make_unique<VulkanBufferManager>(*memory_manager_);

        swapchain_ = std::make_unique<VulkanSwapchain>();
        swapchain_->Initialize(device_.get(), window);
        swapchain_image_layouts_.assign(swapchain_->GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        msaa_sampe_count_ = swapchain_->GetMaxUsableSampleCount();
        InitVulkanContext();

        frame_context_ = std::make_unique<VulkanFrameContext>();
        frame_context_->Initialize(device_.get(), static_cast<uint32_t>(swapchain_->GetImageCount()));

        // Swapchain-bound render targets — RHI-owned, sized to the swapchain.
        CreateDepthResource();
        CreateColorResource();
    }

    void VulkanBackend::BeginFrame()
    {
        // 1. wait for last frame to finish
        // 2. acquire the swapchain image and prepare the frame command buffer
        // 3. caller selects render targets and records draws, then EndFrame submits

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
        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(frame_context_->GetCurrentSceneCommandBuffer(),
                                 &command_buffer_begin_info) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to begin command buffer");
            throw std::runtime_error("Failed to begin command buffer");
        }

        current_image_index_ = image_index;
        frame_active_ = true;
        render_target_active_ = false;
        editor_ui_active_ = false;
        active_render_target_ = {};
    }

    void VulkanBackend::EndFrame()
    {
        if (!frame_active_)
        {
            return;
        }

        if (render_target_active_)
        {
            EndRenderTarget();
        }
        if (editor_ui_active_)
        {
            EndEditorUiRendering();
        }

        VkCommandBuffer scene_command_buffer = frame_context_->GetCurrentSceneCommandBuffer();
        FinishFrame(scene_command_buffer, current_image_index_);

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

    CommandRecorder *VulkanBackend::GetCommandRecorder()
    {
        return frame_active_ ? static_cast<CommandRecorder *>(this) : nullptr;
    }

    GraphicsContext VulkanBackend::GetGraphicsContext()
    {
        return CreateGraphicsContext();
    }

    void VulkanBackend::BeginRenderTarget(RenderTargetHandle target)
    {
        if (!frame_active_ || render_target_active_)
        {
            return;
        }
        const uint32_t index = render_target_handles_.Get(target);
        if (index >= render_targets_.size() || index >= render_target_states_.size())
        {
            return;
        }
        const RenderTargetResource &target_resource = render_targets_[index];
        Texture *color_texture = texture_manager_->GetTexture(target_resource.color);
        Texture *depth_texture = texture_manager_->GetTexture(target_resource.depth);
        if (!color_texture || !depth_texture)
        {
            return;
        }
        const VulkanTextureResource color_resource =
            ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
        const VulkanTextureResource depth_resource =
            ConvertToVulkanTextureResource(depth_texture->GetTextueHandle());
        if (color_resource.image == VK_NULL_HANDLE || color_resource.view == VK_NULL_HANDLE ||
            depth_resource.image == VK_NULL_HANDLE || depth_resource.view == VK_NULL_HANDLE)
        {
            return;
        }

        VkCommandBuffer command_buffer = GetCurrentSceneCommandBuffer();
        VulkanRenderTargetState &state = render_target_states_[index];
        frame_context_->TransitionImageLayout(
            command_buffer, color_resource.image, state.color_layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            state.color_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
        if (state.depth_layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
        {
            frame_context_->TransitionImageLayout(
                command_buffer, depth_resource.image, state.depth_layout,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                state.depth_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1);
            state.depth_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }

        VkClearValue color_clear{};
        color_clear.color = {{0.f, 0.f, 0.f, 1.f}};
        VkRenderingAttachmentInfo color_attachment{};
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = color_resource.view;
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = color_clear;

        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.f, 0};
        VkRenderingAttachmentInfo depth_attachment{};
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = depth_resource.view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.clearValue = depth_clear;

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.extent = {target_resource.desc.width, target_resource.desc.height};
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        rendering_info.pDepthAttachment = &depth_attachment;
        vkCmdBeginRendering(command_buffer, &rendering_info);

        VkViewport viewport{};
        viewport.width = static_cast<float>(target_resource.desc.width);
        viewport.height = static_cast<float>(target_resource.desc.height);
        viewport.maxDepth = 1.f;
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = {target_resource.desc.width, target_resource.desc.height};
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
        state.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        active_render_target_ = target;
        render_target_active_ = true;
    }

    void VulkanBackend::EndRenderTarget()
    {
        if (!frame_active_ || !render_target_active_)
        {
            return;
        }
        vkCmdEndRendering(GetCurrentSceneCommandBuffer());
        const uint32_t index = render_target_handles_.Get(active_render_target_);
        if (index < render_targets_.size() && index < render_target_states_.size())
        {
            Texture *color_texture = texture_manager_->GetTexture(render_targets_[index].color);
            if (color_texture)
            {
                const VulkanTextureResource color_resource =
                    ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
                frame_context_->TransitionImageLayout(
                    GetCurrentSceneCommandBuffer(), color_resource.image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
                render_target_states_[index].color_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        active_render_target_ = {};
        render_target_active_ = false;
    }

    void VulkanBackend::BindPipeline(PipelineHandle pipeline)
    {
        const VulkanPipelineResource *resource = GetPipelineResource(pipeline);
        if (resource)
        {
            vkCmdBindPipeline(GetCurrentSceneCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              resource->pipeline);
        }
    }

    void VulkanBackend::BindMesh(MeshHandle mesh)
    {
        Mesh *mesh_object = mesh_manager_->GetMesh(mesh);
        if (!mesh_object)
        {
            return;
        }
        const auto *mesh_resource = static_cast<const VulkanMeshResource *>(
            mesh_object->GetMeshHandle().native);
        if (!mesh_resource)
        {
            return;
        }

        VulkanBufferResource *vertex = GetBufferResource(mesh_resource->vertex_handle);
        VulkanBufferResource *index = GetBufferResource(mesh_resource->index_handle);
        if (!vertex || !index || mesh_resource->sections.empty())
        {
            return;
        }

        VkBuffer vertex_buffers[] = {vertex->buffer};
        VkDeviceSize offsets[] = {0};
        VkCommandBuffer command_buffer = GetCurrentSceneCommandBuffer();
        vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
        vkCmdBindIndexBuffer(command_buffer, index->buffer, 0, VK_INDEX_TYPE_UINT32);
        recorded_mesh_ = mesh;
        recorded_index_count_ = static_cast<uint32_t>(mesh_resource->sections[0].index_count);
    }

    void VulkanBackend::BindResourceBindings(PipelineHandle pipeline,
                                             DescriptorSetHandle bindings)
    {
        BindResourceBindingSet(pipeline, bindings);
    }

    void VulkanBackend::SetViewport(const Viewport &viewport)
    {
        VkViewport native{};
        native.x = viewport.x;
        native.y = viewport.y;
        native.width = viewport.width;
        native.height = viewport.height;
        native.minDepth = viewport.min_depth;
        native.maxDepth = viewport.max_depth;
        vkCmdSetViewport(GetCurrentSceneCommandBuffer(), 0, 1, &native);
    }

    void VulkanBackend::SetScissor(const Scissor &scissor)
    {
        VkRect2D native{};
        native.offset = {scissor.x, scissor.y};
        native.extent = {scissor.width, scissor.height};
        vkCmdSetScissor(GetCurrentSceneCommandBuffer(), 0, 1, &native);
    }

    void VulkanBackend::DrawIndexed(uint32_t index_count, uint32_t instance_count,
                                    uint32_t first_index, int32_t vertex_offset,
                                    uint32_t first_instance)
    {
        if (index_count == 0)
        {
            index_count = recorded_index_count_;
        }
        if (index_count != 0)
        {
            vkCmdDrawIndexed(GetCurrentSceneCommandBuffer(), index_count, instance_count,
                             first_index, vertex_offset, first_instance);
        }
    }

    void VulkanBackend::Cleanup()
    {
        vkDeviceWaitIdle(device_->GetLogicalDevice());

        for (size_t index = 0; index < render_targets_.size(); ++index)
        {
            RenderTargetResource &target = render_targets_[index];
            if (target.color.IsValid())
            {
                if (index < render_target_states_.size() &&
                    render_target_states_[index].editor_preview_view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(device_->GetLogicalDevice(),
                                       render_target_states_[index].editor_preview_view, nullptr);
                }
                DestroyTexture(target.color);
                DestroyTexture(target.depth);
                target = {};
            }
        }
        render_targets_.clear();
        render_target_states_.clear();

        CleanupSwapchain();

        // The demo scene owns its mesh/texture/UBO/descriptor handles and destroys
        // them before the backend; only backend-owned GPU state lives here now.
        descriptor_set_manager_->DestroyAll(device_->GetLogicalDevice());
        pipeline_manager_->DestroyAll(device_->GetLogicalDevice());

        buffer_manager_->DestroyAll(device_->GetLogicalDevice());
        memory_manager_->Destroy();
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

    GraphicsContext VulkanBackend::CreateGraphicsContext() const
    {
        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = const_cast<VulkanContext *>(&context_);
        return context;
    }

    PipelineHandle VulkanBackend::CreatePipelineResource(const PipelineDesc &pipeline_desc)
    {
        // Pipelines render to offscreen targets by default. Presentation is a
        // separate ImGui pass, so a swapchain format must never leak into this
        // pipeline description. Non-default render targets must state their
        // attachment formats explicitly at the call site.
        PipelineDesc desc = pipeline_desc;
        if (desc.color_attachment_formats.empty())
        {
            desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
        }
        if (desc.depth_attachment_format == TextureFormat::TEXTURE_FORMAT_UNKNOW)
        {
            desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
        }
        return pipeline_manager_->CreatePipelineResource(device_->GetLogicalDevice(), desc);
    }

    bool VulkanBackend::DestroyPipelineResource(PipelineHandle handle)
    {
        return pipeline_manager_->DestroyPipelineResource(device_->GetLogicalDevice(), handle);
    }

    MeshHandle VulkanBackend::CreateMesh(const data::MeshData &data)
    {
        return mesh_manager_->CreateMesh(CreateGraphicsContext(), data);
    }

    bool VulkanBackend::DestroyMesh(MeshHandle handle)
    {
        return mesh_manager_->DestroyMesh(CreateGraphicsContext(), handle);
    }

    TextureHandle VulkanBackend::CreateTexture(const data::TextureData &data,
                                               const TextureSettings &settings)
    {
        const TextureHandle handle = texture_manager_->CreateTexture(CreateGraphicsContext(), data, settings);
        if (handle.IsValid() && !data.pixels.empty())
        {
            UploadTexturePixels(handle, data.pixels.data(), data.pixels.size(),
                                data.width, data.height, settings.mip_levels);
        }
        return handle;
    }

    bool VulkanBackend::DestroyTexture(TextureHandle handle)
    {
        return texture_manager_->DestroyTexture(CreateGraphicsContext(), handle);
    }

    SamplerHandle VulkanBackend::CreateSampler(const SamplerSettings &settings)
    {
        SamplerSettings effective_settings = settings;
        if (effective_settings.enable_anisotropy)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device_->GetPhysicalDevice(), &properties);
            effective_settings.max_anisotropy = std::min(effective_settings.max_anisotropy,
                                                         properties.limits.maxSamplerAnisotropy);
        }
        return sampler_manager_->CreateSampler(CreateGraphicsContext(), effective_settings);
    }

    bool VulkanBackend::DestroySampler(SamplerHandle handle)
    {
        return sampler_manager_->DestroySampler(CreateGraphicsContext(), handle);
    }

    RenderTargetHandle VulkanBackend::CreateRenderTarget(const RenderTargetDesc &desc)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            return {};
        }
        const RenderTargetHandle handle = render_target_handles_.Create();
        if (handle.id == render_targets_.size())
        {
            render_targets_.emplace_back();
            render_target_states_.emplace_back();
        }

        TextureData target_data{};
        target_data.width = desc.width;
        target_data.height = desc.height;
        TextureSettings color_settings{};
        color_settings.format = desc.color_format;
        color_settings.usage = TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT | TextureUsage::TEXTURE_USAGE_SAMPLE;
        color_settings.aspect = ImageAspect::IMAGE_ASPECT_COLOR;
        color_settings.mutable_format = desc.color_format == TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        TextureSettings depth_settings{};
        depth_settings.format = desc.depth_format;
        depth_settings.usage = TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT;
        depth_settings.aspect = ImageAspect::IMAGE_ASPECT_DEPTH;

        RenderTargetResource target{};
        target.desc = desc;
        target.color = CreateTexture(target_data, color_settings);
        target.depth = CreateTexture(target_data, depth_settings);
        if (!target.color.IsValid() || !target.depth.IsValid())
        {
            if (target.color.IsValid()) DestroyTexture(target.color);
            if (target.depth.IsValid()) DestroyTexture(target.depth);
            render_target_handles_.Destroy(handle);
            return {};
        }
        VulkanRenderTargetState state{};
        if (color_settings.mutable_format)
        {
            Texture *color_texture = texture_manager_->GetTexture(target.color);
            const VulkanTextureResource color_resource = color_texture
                ? ConvertToVulkanTextureResource(color_texture->GetTextueHandle())
                : VulkanTextureResource{};
            VkImageViewCreateInfo preview_view_info{};
            preview_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            preview_view_info.image = color_resource.image;
            preview_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            preview_view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            preview_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            preview_view_info.subresourceRange.baseMipLevel = 0;
            preview_view_info.subresourceRange.levelCount = 1;
            preview_view_info.subresourceRange.baseArrayLayer = 0;
            preview_view_info.subresourceRange.layerCount = 1;
            if (color_resource.image == VK_NULL_HANDLE ||
                vkCreateImageView(device_->GetLogicalDevice(), &preview_view_info, nullptr,
                                  &state.editor_preview_view) != VK_SUCCESS)
            {
                DestroyTexture(target.color);
                DestroyTexture(target.depth);
                render_target_handles_.Destroy(handle);
                return {};
            }
        }
        render_targets_[handle.id] = target;
        render_target_states_[handle.id] = state;
        return handle;
    }

    bool VulkanBackend::DestroyRenderTarget(RenderTargetHandle handle)
    {
        const uint32_t index = render_target_handles_.Get(handle);
        if (index >= render_targets_.size()) return false;
        RenderTargetResource &target = render_targets_[index];
        if (!target.color.IsValid() || !target.depth.IsValid()) return false;
        if (index < render_target_states_.size() &&
            render_target_states_[index].editor_preview_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_->GetLogicalDevice(),
                               render_target_states_[index].editor_preview_view, nullptr);
        }
        DestroyTexture(target.color);
        DestroyTexture(target.depth);
        target = {};
        if (index < render_target_states_.size())
        {
            render_target_states_[index] = {};
        }
        return render_target_handles_.Destroy(handle);
    }

    TextureHandle VulkanBackend::GetRenderTargetColor(RenderTargetHandle handle)
    {
        const uint32_t index = render_target_handles_.Get(handle);
        return index < render_targets_.size() ? render_targets_[index].color : TextureHandle{};
    }

    RenderTargetView VulkanBackend::GetRenderTargetView(RenderTargetHandle handle)
    {
        const uint32_t index = render_target_handles_.Get(handle);
        if (index >= render_targets_.size())
        {
            return {};
        }
        const RenderTargetResource &target = render_targets_[index];
        Texture *color_texture = texture_manager_->GetTexture(target.color);
        if (!color_texture)
        {
            return {};
        }
        const VulkanTextureResource resource =
            ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
        const VkImageView preview_view = index < render_target_states_.size() &&
                                                render_target_states_[index].editor_preview_view != VK_NULL_HANDLE
                                            ? render_target_states_[index].editor_preview_view
                                            : resource.view;
        return {target.desc.width, target.desc.height,
                reinterpret_cast<uintptr_t>(resource.image),
                reinterpret_cast<uintptr_t>(preview_view)};
    }

    DescriptorSetHandle VulkanBackend::CreateResourceBindingSet(
        PipelineHandle pipeline, const ResourceBindingSetDesc &desc)
    {
        VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline);
        if (!pipeline_resource)
        {
            return {};
        }
        return descriptor_set_manager_->CreateResourceBindingSet(
            device_->GetLogicalDevice(), *pipeline_resource, desc, *buffer_manager_,
            *texture_manager_, *sampler_manager_);
    }

    bool VulkanBackend::DestroyResourceBindingSet(DescriptorSetHandle handle)
    {
        return descriptor_set_manager_->DestroyResourceBindingSet(device_->GetLogicalDevice(), handle);
    }

    void VulkanBackend::BindResourceBindingSet(PipelineHandle pipeline, DescriptorSetHandle handle)
    {
        VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline);
        const VkDescriptorSet descriptor_set = descriptor_set_manager_->GetDescriptorSet(handle);
        if (!pipeline_resource || descriptor_set == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdBindDescriptorSets(GetCurrentSceneCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_resource->layout, 0, 1, &descriptor_set, 0, nullptr);
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
        return buffer_manager_->CreateBufferResource(device_->GetLogicalDevice(), &stage_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_STAGING);
    }

    BufferHandle VulkanBackend::CreateDownloadStageBufferResource(size_t size)
    {
        VkBufferCreateInfo stage_buffer_create_info{};
        stage_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stage_buffer_create_info.size = size;
        stage_buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        stage_buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return buffer_manager_->CreateBufferResource(device_->GetLogicalDevice(), &stage_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_STAGING);
    }

    bool VulkanBackend::DestroyBufferResource(BufferHandle handle)
    {
        return buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), handle);
    }

    void VulkanBackend::UploadDataToBuffer(BufferHandle handle, size_t size, const void *data)
    {
        buffer_manager_->UploadData(handle, size, data);
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

        BufferHandle dst_handle = buffer_manager_->CreateBufferResource(device_->GetLogicalDevice(), &dst_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_DEVICE);

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

    bool VulkanBackend::BeginEditorUiRendering()
    {
        if (!frame_active_ || editor_ui_active_ ||
            current_image_index_ >= swapchain_image_layouts_.size())
        {
            return false;
        }

        VkCommandBuffer command_buffer = GetCurrentSceneCommandBuffer();
        const VkImage image = swapchain_->GetImage(current_image_index_);
        frame_context_->TransitionImageLayout(
            command_buffer, image, swapchain_image_layouts_[current_image_index_],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            swapchain_image_layouts_[current_image_index_] == VK_IMAGE_LAYOUT_UNDEFINED
                ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

        VkClearValue clear_value{};
        clear_value.color = {{0.1f, 0.1f, 0.1f, 1.0f}};
        VkRenderingAttachmentInfo color_attachment{};
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = swapchain_->GetImageView(current_image_index_);
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = clear_value;

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.extent = swapchain_->GetExtent();
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        vkCmdBeginRendering(command_buffer, &rendering_info);
        swapchain_image_layouts_[current_image_index_] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        editor_ui_active_ = true;
        return true;
    }

    void VulkanBackend::EndEditorUiRendering()
    {
        if (!frame_active_ || !editor_ui_active_ ||
            current_image_index_ >= swapchain_image_layouts_.size())
        {
            return;
        }

        VkCommandBuffer command_buffer = GetCurrentSceneCommandBuffer();
        vkCmdEndRendering(command_buffer);
        frame_context_->TransitionImageLayout(
            command_buffer, swapchain_->GetImage(current_image_index_),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
        swapchain_image_layouts_[current_image_index_] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        editor_ui_active_ = false;
    }

    VkCommandBuffer VulkanBackend::GetCurrentSceneCommandBuffer() const
    {
        return frame_context_->GetCurrentSceneCommandBuffer();
    }

    uint32_t VulkanBackend::GetSwapchainImageCount() const
    {
        return static_cast<uint32_t>(swapchain_->GetImageCount());
    }

    VkFormat VulkanBackend::GetSwapchainImageFormat() const
    {
        return swapchain_->GetImageFormat();
    }

    const VulkanQueue &VulkanBackend::GetGraphicsQueue() const
    {
        return device_->GetGraphicsQueue();
    }

    uint32_t VulkanBackend::GetCurrentFrameIndex() const
    {
        return frame_context_->GetCurrentFrameIndex();
    }

    uint32_t VulkanBackend::GetFramesInFlight() const
    {
        return VulkanFrameContext::MAX_FRAMES_IN_FLIGHT;
    }

    size_t VulkanBackend::GetUniformBufferAlignment() const
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device_->GetPhysicalDevice(), &properties);
        return static_cast<size_t>(properties.limits.minUniformBufferOffsetAlignment);
    }

    Extent2D VulkanBackend::GetRenderExtent() const
    {
        const VkExtent2D extent = swapchain_->GetExtent();
        return {extent.width, extent.height};
    }

    void VulkanBackend::WaitIdle()
    {
        if (device_)
        {
            vkDeviceWaitIdle(device_->GetLogicalDevice());
        }
    }

    const VulkanPipelineResource *VulkanBackend::GetPipelineResource(PipelineHandle handle) const
    {
        return pipeline_manager_->GetPipelineResource(handle);
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
        return buffer_manager_->CreateBufferResource(device_->GetLogicalDevice(), &buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM);
    }

    void *VulkanBackend::MapUniformBuffer(BufferHandle handle, size_t size)
    {
        return buffer_manager_->GetMappedAddress(handle, size);
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
        swapchain_image_layouts_.assign(swapchain_->GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
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

    void VulkanBackend::FinishFrame(VkCommandBuffer commandbuffer, uint32_t image_index)
    {
        // Headless/RHI examples may not record editor UI. Present a valid image
        // layout in that case; editor frames transition it in EndEditorUiRendering.
        if (image_index < swapchain_image_layouts_.size() &&
            swapchain_image_layouts_[image_index] != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            frame_context_->TransitionImageLayout(
                commandbuffer, swapchain_->GetImage(image_index),
                swapchain_image_layouts_[image_index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                swapchain_image_layouts_[image_index] == VK_IMAGE_LAYOUT_UNDEFINED
                    ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
                0, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
            swapchain_image_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        if (vkEndCommandBuffer(commandbuffer) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to end record command buffer");
            throw std::runtime_error("Failed to end record command buffer");
        }
    }

    VulkanBackend::~VulkanBackend() = default;
}
