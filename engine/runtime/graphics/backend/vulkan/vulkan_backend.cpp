#include "vulkan_backend.h"
#include <algorithm>
#include <array>
#include <type_traits>
#include <GLFW/glfw3.h>
#include "log/logger.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_bindless_texture_table.h"
#include "vulkan_memory_manager.h"
#include "vulkan_swapchain.h"
#include "vulkan_frame_context.h"
#include "vulkan_command_recorder.h"
#include "vulkan_render_target_manager.h"
#include "vulkan_render_target_readback.h"
#include "vulkan_editor_bridge.h"
#include "vulkan_upload_context.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_descriptor_set_manager.h"
#include "vulkan_image_memory_manager.h"
#include "vulkan_texture.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "common/mesh_manager.h"
#include "common/pipeline_validation.h"
#include "vulkan_mesh.h"

namespace kpengine::graphics
{
    static_assert(!std::is_base_of_v<IRenderTargetReadback, VulkanBackend>);


#define KP_VULKAN_BACKEND_LOG_NAME "VulkanBackendLog"
    namespace
    {
        constexpr uint32_t kProfilePassCount = 8;
        constexpr uint32_t kProfileQueriesPerFrame = kProfilePassCount * 2;
    }

    VulkanBackend::VulkanBackend() : pipeline_manager_(std::make_unique<VulkanPipelineManager>()),
                                     descriptor_set_manager_(std::make_unique<VulkanDescriptorSetManager>()),
                                     texture_manager_(std::make_unique<TextureManager>()),
                                     sampler_manager_(std::make_unique<SamplerManager>()),
                                     mesh_manager_(std::make_unique<MeshManager>())
    {
    }

    IRenderTargetReadback *VulkanBackend::GetRenderTargetReadback()
    {
        return render_target_readback_.get();
    }

    void VulkanBackend::Initialize(WindowHandle native_window)
    {
        // The native window (WindowHandle = void*) is cast back to GLFW here — the
        // Vulkan surface + swapchain need it; the common facade never sees GLFW.
        GLFWwindow *window = static_cast<GLFWwindow *>(native_window);

        device_ = std::make_unique<VulkanDevice>();
        device_->Initialize(window);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device_->GetPhysicalDevice(), &properties);
        profile_timestamp_period_ns_ = properties.limits.timestampPeriod;
        memory_manager_ = std::make_unique<VulkanMemoryManager>(
            device_->GetPhysicalDevice(), device_->GetLogicalDevice());
        image_memory_manager_ = std::make_unique<VulkanImageMemoryManager>(*memory_manager_);
        buffer_manager_ = std::make_unique<VulkanBufferManager>(*memory_manager_);

        swapchain_ = std::make_unique<VulkanSwapchain>();
        swapchain_->Initialize(device_.get(), window);
        msaa_sampe_count_ = swapchain_->GetMaxUsableSampleCount();

        frame_context_ = std::make_unique<VulkanFrameContext>();
        frame_context_->Initialize(device_.get(), static_cast<uint32_t>(swapchain_->GetImageCount()));
        descriptor_set_manager_->Initialize(VulkanFrameContext::MAX_FRAMES_IN_FLIGHT);
        if (device_->SupportsBindlessTextures())
        {
            auto candidate = std::make_unique<VulkanBindlessTextureTable>();
            if (candidate->Initialize(device_->GetLogicalDevice(),
                                      device_->GetBindlessTextureTableCapacity(),
                                      VulkanFrameContext::MAX_FRAMES_IN_FLIGHT))
            {
                bindless_texture_table_ = std::move(candidate);
            }
            else
            {
                KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_WARNING,
                       "Vulkan descriptor indexing is available, but bindless table creation failed; using bound resources");
            }
        }
        InitializeCapabilities();
        VkQueryPoolCreateInfo query_pool_info{};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_pool_info.queryCount = VulkanFrameContext::MAX_FRAMES_IN_FLIGHT *
                                     kProfileQueriesPerFrame;
        if (vkCreateQueryPool(device_->GetLogicalDevice(), &query_pool_info, nullptr,
                              &profile_query_pool_) != VK_SUCCESS)
        {
            profile_query_pool_ = VK_NULL_HANDLE;
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_WARNING,
                   "GPU pass timestamp query pool unavailable; CPU profile remains active");
        }
        editor_bridge_ = std::make_unique<VulkanEditorBridge>(*device_, *swapchain_, *frame_context_);
        upload_context_ = std::make_unique<VulkanUploadContext>();
        upload_context_->Initialize(device_.get(), frame_context_.get(), buffer_manager_.get());
        InitVulkanContext();
        render_target_manager_ = std::make_unique<VulkanRenderTargetManager>(
            device_->GetLogicalDevice(), CreateGraphicsContext(), *frame_context_, *texture_manager_);
        render_target_readback_ = std::make_unique<VulkanRenderTargetReadback>(
            device_->GetLogicalDevice(), *frame_context_, *buffer_manager_, *render_target_manager_);
        const VkExtent2D extent = swapchain_->GetExtent();
        render_target_manager_->CreateSwapchainAttachments(extent.width, extent.height,
                                                           msaa_sampe_count_);
    }

    void VulkanBackend::BeginFrame()
    {
        // 1. wait for last frame to finish
        // 2. acquire the swapchain image and prepare the frame command buffer
        // 3. caller selects render targets and records draws, then EndFrame submits

        frame_context_->WaitForInFlightFence();
        ResetCurrentFrameGeometryBuffers(frame_context_->GetCurrentFrameIndex());
        descriptor_set_manager_->BeginFrame(device_->GetLogicalDevice(),
                                             frame_context_->GetCurrentFrameIndex());
        CollectCompletedGpuProfileTimings();
        if (render_target_readback_)
        {
            render_target_readback_->CollectCompletedReadbacks();
        }
        if (bindless_texture_table_)
        {
            bindless_texture_table_->PrepareFrame(
                device_->GetLogicalDevice(), frame_context_->GetCurrentFrameIndex(),
                frame_context_->GetCompletedSubmissionSerial(), *texture_manager_, *sampler_manager_);
        }

        uint32_t image_index;
        VkResult acquire_image_res = frame_context_->AcquireNextImage(swapchain_->GetSwapchain(), image_index);

        if (acquire_image_res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchain();
            descriptor_set_manager_->EndFrame();
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
        ResetBackendProfileCounters();
        if (profile_query_pool_ != VK_NULL_HANDLE)
        {
            const uint32_t base_query = frame_context_->GetCurrentFrameIndex() *
                                        kProfileQueriesPerFrame;
            vkCmdResetQueryPool(frame_context_->GetCurrentSceneCommandBuffer(),
                                profile_query_pool_, base_query, kProfileQueriesPerFrame);
        }

        current_image_index_ = image_index;
        editor_bridge_->BeginFrame(image_index);
        command_recorder_.reset();
        command_recorder_ = std::make_unique<VulkanCommandRecorder>();
        command_recorder_->Begin(frame_context_->GetCurrentSceneCommandBuffer(), *pipeline_manager_,
                                 *descriptor_set_manager_, *buffer_manager_, *mesh_manager_,
                                 *render_target_manager_, bindless_texture_table_.get(),
                                 frame_context_->GetCurrentFrameIndex(), editor_bridge_.get());
        const uint32_t frame_index = frame_context_->GetCurrentFrameIndex();
        command_recorder_->SetGeometryBufferResolvers(
            [this, frame_index](BufferHandle handle) {
                return GetGeometryBufferDesc(handle, frame_index);
            },
            [this, frame_index](BufferHandle handle) {
                return GetGeometryBufferHandle(handle, frame_index);
            });
        frame_active_ = true;
    }

    void VulkanBackend::EndFrame()
    {
        if (!frame_active_)
        {
            return;
        }

        command_recorder_->EndRenderTarget();
        AccumulateCommandRecorderProfileCounters(command_recorder_.get());
        AccumulateDescriptorProfileCounters(descriptor_set_manager_->GetProfileCounters());
        command_recorder_.reset();
        VkCommandBuffer scene_command_buffer = frame_context_->GetCurrentSceneCommandBuffer();
        render_target_readback_->RecordPendingCopies(
            scene_command_buffer, frame_context_->GetCurrentPendingSubmissionSerial());
        FinishFrame(scene_command_buffer, current_image_index_);
        editor_bridge_->EndFrame();

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
        descriptor_set_manager_->EndFrame();
        frame_active_ = false;
    }

    CommandRecorder *VulkanBackend::GetCommandRecorder()
    {
        return frame_active_ ? command_recorder_.get() : nullptr;
    }

    void VulkanBackend::BeginGpuProfilePass(const uint32_t pass_id)
    {
        if (profile_query_pool_ == VK_NULL_HANDLE || !frame_active_ ||
            pass_id >= kProfilePassCount)
        {
            return;
        }
        const uint32_t query = frame_context_->GetCurrentFrameIndex() * kProfileQueriesPerFrame +
                               pass_id * 2;
        vkCmdWriteTimestamp2(frame_context_->GetCurrentSceneCommandBuffer(),
                             VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, profile_query_pool_, query);
    }

    void VulkanBackend::EndGpuProfilePass(const uint32_t pass_id)
    {
        if (profile_query_pool_ == VK_NULL_HANDLE || !frame_active_ ||
            pass_id >= kProfilePassCount)
        {
            return;
        }
        const uint32_t query = frame_context_->GetCurrentFrameIndex() * kProfileQueriesPerFrame +
                               pass_id * 2 + 1;
        vkCmdWriteTimestamp2(frame_context_->GetCurrentSceneCommandBuffer(),
                             VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, profile_query_pool_, query);
    }

    std::vector<GpuProfileTiming> VulkanBackend::ConsumeCompletedGpuProfileTimings()
    {
        std::vector<GpuProfileTiming> result;
        result.swap(completed_gpu_profile_timings_);
        return result;
    }

    const char *VulkanBackend::GetPresentModeName() const
    {
        return swapchain_ ? swapchain_->GetPresentModeName() : "unknown";
    }

    void VulkanBackend::CollectCompletedGpuProfileTimings()
    {
        completed_gpu_profile_timings_.clear();
        if (profile_query_pool_ == VK_NULL_HANDLE || profile_timestamp_period_ns_ <= 0.0f ||
            frame_context_->GetCompletedSubmissionSerial() == 0)
        {
            return;
        }
        const uint32_t base_query = frame_context_->GetCurrentFrameIndex() *
                                    kProfileQueriesPerFrame;
        std::array<uint64_t, kProfileQueriesPerFrame> timestamps{};
        const VkResult result = vkGetQueryPoolResults(
            device_->GetLogicalDevice(), profile_query_pool_, base_query,
            kProfileQueriesPerFrame, sizeof(timestamps), timestamps.data(), sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (result != VK_SUCCESS)
        {
            return;
        }
        for (uint32_t pass_id = 0; pass_id < kProfilePassCount; ++pass_id)
        {
            const uint64_t begin = timestamps[pass_id * 2];
            const uint64_t end = timestamps[pass_id * 2 + 1];
            if (end >= begin && end != 0)
            {
                completed_gpu_profile_timings_.push_back(
                    {pass_id, static_cast<uint64_t>(
                                  static_cast<double>(end - begin) *
                                  static_cast<double>(profile_timestamp_period_ns_))});
            }
        }
    }

    IEditorPresentationBridge *VulkanBackend::GetEditorPresentationBridge()
    {
        return editor_bridge_.get();
    }

    void VulkanBackend::Cleanup()
    {
        vkDeviceWaitIdle(device_->GetLogicalDevice());
        if (profile_query_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device_->GetLogicalDevice(), profile_query_pool_, nullptr);
            profile_query_pool_ = VK_NULL_HANDLE;
        }
        command_recorder_.reset();

        if (render_target_readback_)
        {
            render_target_readback_->DrainPendingReadbacks("Vulkan backend shutdown");
        }

        render_target_manager_->DestroyAll();

        CleanupSwapchain();

        // The demo scene owns its mesh/texture/UBO/descriptor handles and destroys
        // them before the backend; only backend-owned GPU state lives here now.
        descriptor_set_manager_->DestroyAll(device_->GetLogicalDevice());
        pipeline_manager_->DestroyAll(device_->GetLogicalDevice());
        if (bindless_texture_table_)
        {
            bindless_texture_table_->Destroy(device_->GetLogicalDevice());
            bindless_texture_table_.reset();
        }

        DestroyGeometryBuffers();
        buffer_manager_->DestroyAll(device_->GetLogicalDevice());
        image_memory_manager_.reset();
        memory_manager_->Destroy();

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

    void VulkanBackend::InitializeCapabilities()
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device_->GetPhysicalDevice(), &properties);
        capabilities_.max_sampled_textures_per_shader_stage =
            properties.limits.maxPerStageDescriptorSampledImages;

        capabilities_.bindless_textures = bindless_texture_table_ && bindless_texture_table_->IsReady();
        capabilities_.bindless_texture_table_capacity = capabilities_.bindless_textures
                                                             ? device_->GetBindlessTextureTableCapacity()
                                                             : 0;

        const auto supports_sampled_format = [physical_device = device_->GetPhysicalDevice()](
                                                 VkFormat format)
        {
            VkFormatProperties format_properties{};
            vkGetPhysicalDeviceFormatProperties(physical_device, format, &format_properties);
            return (format_properties.optimalTilingFeatures &
                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
        };
        capabilities_.bc4_unorm_textures =
            supports_sampled_format(VK_FORMAT_BC4_UNORM_BLOCK);
        capabilities_.bc5_unorm_textures =
            supports_sampled_format(VK_FORMAT_BC5_UNORM_BLOCK);
        capabilities_.bc3_unorm_textures =
            supports_sampled_format(VK_FORMAT_BC3_UNORM_BLOCK);
        capabilities_.bc3_srgb_textures =
            supports_sampled_format(VK_FORMAT_BC3_SRGB_BLOCK);
    }

    void VulkanBackend::InitVulkanContext()
    {
        context_.buffer_manager = buffer_manager_.get();
        context_.upload_context = upload_context_.get();
        context_.image_memory_manager = image_memory_manager_.get();
        context_.editor_bridge = editor_bridge_.get();
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
        // pipeline description. Attachment formats are stated explicitly at the
        // call site; missing color means a depth-only pipeline and UNKNOW depth
        // means no depth.
        if (!ValidatePipelineDesc(pipeline_desc, GraphicsAPIType::GRAPHICS_API_VULKAN))
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR,
                   "Rejected invalid Vulkan pipeline descriptor");
            return {};
        }
        if (bindless_texture_table_ &&
            pipeline_desc.descriptor_binding_descs.size() > BindlessTextureTableLayout::descriptor_set &&
            !pipeline_desc.descriptor_binding_descs[BindlessTextureTableLayout::descriptor_set].empty())
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR,
                   "Vulkan bindless ABI reserves descriptor set 1 for the sampled-texture table");
            return {};
        }
        return pipeline_manager_->CreatePipelineResource(
            device_->GetLogicalDevice(), pipeline_desc,
            bindless_texture_table_ ? bindless_texture_table_->GetLayout() : VK_NULL_HANDLE);
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
            UploadTexturePixels(handle, data);
        }
        return handle;
    }

    bool VulkanBackend::DestroyTexture(TextureHandle handle)
    {
        if (bindless_texture_table_ && bindless_texture_table_->ReferencesTexture(handle))
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_WARNING,
                   "Cannot destroy a texture referenced by the Vulkan bindless table");
            return false;
        }
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
        if (bindless_texture_table_ && bindless_texture_table_->ReferencesSampler(handle))
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_WARNING,
                   "Cannot destroy a sampler referenced by the Vulkan bindless table");
            return false;
        }
        return sampler_manager_->DestroySampler(CreateGraphicsContext(), handle);
    }

    RenderTargetHandle VulkanBackend::CreateRenderTarget(const RenderTargetDesc &desc)
    {
        return render_target_manager_->Create(desc);
    }

    bool VulkanBackend::DestroyRenderTarget(RenderTargetHandle handle)
    {
        render_target_readback_->CancelTarget(handle, "Render target was destroyed before readback completed");
        return render_target_manager_->Destroy(handle);
    }

    TextureHandle VulkanBackend::GetRenderTargetColor(RenderTargetHandle handle)
    {
        return render_target_manager_->GetColor(handle);
    }

    TextureHandle VulkanBackend::GetRenderTargetColorAttachment(RenderTargetHandle handle,
                                                                uint32_t index)
    {
        return render_target_manager_->GetColorAttachment(handle, index);
    }

    TextureHandle VulkanBackend::GetRenderTargetDepthAttachment(RenderTargetHandle handle)
    {
        return render_target_manager_->GetDepthAttachment(handle);
    }

    TextureHandle VulkanBackend::GetRenderTargetSampledDepthAttachment(
        RenderTargetHandle handle)
    {
        return render_target_manager_->GetSampledDepthAttachment(handle);
    }

    RenderTargetView VulkanBackend::GetRenderTargetView(RenderTargetHandle handle)
    {
        return render_target_manager_->GetView(handle);
    }

    DescriptorSetHandle VulkanBackend::CreateResourceBindingSet(
        PipelineHandle pipeline, const ResourceBindingSetDesc &desc)
    {
        VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline);
        if (!pipeline_resource)
        {
            return {};
        }
        bool pool_created = false;
        const DescriptorSetHandle handle = descriptor_set_manager_->CreateResourceBindingSet(
            device_->GetLogicalDevice(), *pipeline_resource, desc, *buffer_manager_,
            *texture_manager_, *sampler_manager_, &pool_created);
        if (handle.IsValid())
        {
            RecordDescriptorSetCreated(pool_created);
        }
        return handle;
    }

    bool VulkanBackend::DestroyResourceBindingSet(DescriptorSetHandle handle)
    {
        return descriptor_set_manager_->DestroyResourceBindingSet(device_->GetLogicalDevice(), handle);
    }

    BindlessTextureHandle VulkanBackend::AcquireBindlessTexture(TextureHandle texture,
                                                                 SamplerHandle sampler)
    {
        return bindless_texture_table_
                   ? bindless_texture_table_->Acquire(texture, sampler, *texture_manager_, *sampler_manager_)
                   : BindlessTextureHandle{};
    }

    bool VulkanBackend::ReleaseBindlessTexture(BindlessTextureHandle handle)
    {
        if (!bindless_texture_table_)
        {
            return false;
        }
        const BindlessSubmissionSerial retire_after = frame_active_
                                                          ? frame_context_->GetCurrentPendingSubmissionSerial()
                                                          : frame_context_->GetLastSubmittedSerial();
        return bindless_texture_table_->Release(handle, retire_after);
    }

    void VulkanBackend::BindResourceBindingSet(PipelineHandle pipeline, DescriptorSetHandle handle)
    {
        VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline);
        const VkDescriptorSet descriptor_set = descriptor_set_manager_->GetDescriptorSet(handle);
        if (!pipeline_resource || descriptor_set == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdBindDescriptorSets(frame_context_->GetCurrentSceneCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_resource->layout, 0, 1, &descriptor_set, 0, nullptr);
    }

    bool VulkanBackend::DestroyBufferResource(BufferHandle handle)
    {
        const auto geometry_it = geometry_buffers_.find(handle);
        if (geometry_it != geometry_buffers_.end())
        {
            for (const BufferHandle native : geometry_it->second->native_buffers)
            {
                buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), native);
            }
            geometry_buffers_.erase(geometry_it);
            return geometry_buffer_handles_.Destroy({handle.id & 0x7fffffffu,
                                                     handle.generation});
        }
        if ((handle.id & 0x80000000u) != 0)
        {
            return false;
        }
        return buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), handle);
    }

    BufferHandle VulkanBackend::CreateBuffer(const void *data, size_t size, VkBufferUsageFlags usage)
    {
        VkBufferCreateInfo dst_buffer_create_info{};
        dst_buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dst_buffer_create_info.size = size;
        dst_buffer_create_info.usage = usage;
        dst_buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        BufferHandle dst_handle = buffer_manager_->CreateBufferResource(device_->GetLogicalDevice(), &dst_buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_DEVICE);
        try
        {
            upload_context_->UploadBuffer(dst_handle, size, data);
        }
        catch (...)
        {
            DestroyBufferResource(dst_handle);
            throw;
        }
        return dst_handle;
    }

    BufferHandle VulkanBackend::CreateBuffer(const BufferDesc &desc, const void *initial_data,
                                             const size_t initial_size)
    {
        if (!ValidateBufferDesc(desc, initial_data, initial_size))
        {
            return {};
        }

        auto resource = std::make_unique<GeometryBufferResource>();
        resource->desc = desc;
        const uint32_t slot_count = desc.update_mode == BufferUpdateMode::PerFrame
                                        ? GetFramesInFlight() : 1u;
        resource->written_slots.assign(slot_count,
                                       desc.update_mode == BufferUpdateMode::Immutable);
        const VkBufferUsageFlags usage = desc.role == BufferRole::Index
                                             ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                             : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        try
        {
            for (uint32_t slot = 0; slot < slot_count; ++slot)
            {
                VkBufferCreateInfo create_info{};
                create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                create_info.size = static_cast<VkDeviceSize>(desc.capacity_bytes);
                create_info.usage = usage;
                if (desc.update_mode == BufferUpdateMode::Immutable)
                {
                    create_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                }
                create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                const BufferHandle native = buffer_manager_->CreateBufferResource(
                    device_->GetLogicalDevice(), &create_info,
                    desc.update_mode == BufferUpdateMode::PerFrame
                        ? VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM
                        : VulkanMemoryUsageType::MEMORY_USAGE_DEVICE);
                resource->native_buffers.push_back(native);
                if (desc.update_mode == BufferUpdateMode::Immutable && initial_size != 0)
                {
                    upload_context_->UploadBuffer(native, initial_size, initial_data);
                }
            }
        }
        catch (...)
        {
            for (const BufferHandle native : resource->native_buffers)
            {
                buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), native);
            }
            throw;
        }

        const BufferHandle internal = geometry_buffer_handles_.Create();
        const BufferHandle public_handle{internal.id | 0x80000000u, internal.generation};
        geometry_buffers_.emplace(public_handle, std::move(resource));
        return public_handle;
    }

    bool VulkanBackend::WriteFrameBuffer(BufferHandle buffer, const size_t offset,
                                         const void *data, const size_t size)
    {
        const auto it = geometry_buffers_.find(buffer);
        if (!frame_active_ || it == geometry_buffers_.end() ||
            it->second->desc.update_mode != BufferUpdateMode::PerFrame ||
            (size != 0 && data == nullptr) || offset > it->second->desc.capacity_bytes ||
            size > it->second->desc.capacity_bytes - offset)
        {
            return false;
        }
        const uint32_t frame_index = GetCurrentFrameIndex();
        if (frame_index >= it->second->native_buffers.size())
        {
            return false;
        }
        const BufferHandle native = it->second->native_buffers[frame_index];
        try
        {
            if (size != 0)
            {
                buffer_manager_->UploadData(native, static_cast<VkDeviceSize>(size), data,
                                             static_cast<VkDeviceSize>(offset));
                it->second->written_slots[frame_index] = true;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<BufferDesc> VulkanBackend::GetGeometryBufferDesc(
        BufferHandle handle, const uint32_t frame_index) const
    {
        const auto it = geometry_buffers_.find(handle);
        if (it == geometry_buffers_.end())
        {
            return std::nullopt;
        }
        if (it->second->desc.update_mode == BufferUpdateMode::PerFrame &&
            (frame_index >= it->second->written_slots.size() ||
             !it->second->written_slots[frame_index]))
        {
            return std::nullopt;
        }
        return it->second->desc;
    }

    BufferHandle VulkanBackend::GetGeometryBufferHandle(BufferHandle handle,
                                                         const uint32_t frame_index) const
    {
        const auto it = geometry_buffers_.find(handle);
        if (it == geometry_buffers_.end() || it->second->native_buffers.empty())
        {
            return {};
        }
        const uint32_t slot = it->second->desc.update_mode == BufferUpdateMode::PerFrame
                                  ? frame_index : 0u;
        if (slot >= it->second->native_buffers.size() ||
            (it->second->desc.update_mode == BufferUpdateMode::PerFrame &&
             (slot >= it->second->written_slots.size() ||
              !it->second->written_slots[slot])))
        {
            return {};
        }
        return it->second->native_buffers[slot];
    }

    void VulkanBackend::ResetCurrentFrameGeometryBuffers(const uint32_t frame_index) noexcept
    {
        for (const auto &[handle, resource] : geometry_buffers_)
        {
            (void)handle;
            if (resource && resource->desc.update_mode == BufferUpdateMode::PerFrame &&
                frame_index < resource->written_slots.size())
            {
                resource->written_slots[frame_index] = false;
            }
        }
    }

    void VulkanBackend::DestroyGeometryBuffers()
    {
        for (const auto &[handle, resource] : geometry_buffers_)
        {
            (void)handle;
            if (!resource)
            {
                continue;
            }
            for (const BufferHandle native : resource->native_buffers)
            {
                buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), native);
            }
        }
        geometry_buffers_.clear();
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

    TextureFormat VulkanBackend::GetPresentationColorFormat() const
    {
        return ConvertFromVulkanTextureFormat(swapchain_->GetImageFormat());
    }

    void VulkanBackend::WaitIdle()
    {
        if (device_)
        {
            vkDeviceWaitIdle(device_->GetLogicalDevice());
            if (bindless_texture_table_)
            {
                bindless_texture_table_->CollectCompletedSubmissions(
                    frame_context_->GetLastSubmittedSerial());
            }
            if (render_target_readback_ && frame_context_)
            {
                render_target_readback_->CollectCompleted(frame_context_->GetLastSubmittedSerial());
            }
        }
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

    void VulkanBackend::UploadTexturePixels(TextureHandle texture, const data::TextureData &data)
    {
        Texture *texture_entity = texture_manager_->GetTexture(texture);
        if (!texture_entity)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "UploadTexturePixels: invalid texture handle");
            return;
        }
        VkImage image = ConvertToVulkanTextureResource(texture_entity->GetTextueHandle()).image;

        upload_context_->UploadTexture(image, data);
    }

    void VulkanBackend::RecreateSwapchain()
    {
        while (height_ == 0 || width_ == 0)
        {
            glfwWaitEvents();
        }

        render_target_manager_->DestroySwapchainAttachments();
        swapchain_->Recreate(width_, height_);
        editor_bridge_->OnSwapchainRecreated();
        frame_context_->OnSwapchainRecreated(static_cast<uint32_t>(swapchain_->GetImageCount()));
        const VkExtent2D extent = swapchain_->GetExtent();
        render_target_manager_->CreateSwapchainAttachments(extent.width, extent.height,
                                                           msaa_sampe_count_);
    }

    void VulkanBackend::CleanupSwapchain()
    {
        render_target_manager_->DestroySwapchainAttachments();
        swapchain_->Cleanup();
    }

    void VulkanBackend::FinishFrame(VkCommandBuffer commandbuffer, uint32_t image_index)
    {
        editor_bridge_->EnsurePresentLayout(commandbuffer, image_index);

        if (vkEndCommandBuffer(commandbuffer) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to end record command buffer");
            throw std::runtime_error("Failed to end record command buffer");
        }
    }

    VulkanBackend::~VulkanBackend() = default;
}
