#include "vulkan_backend.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <GLFW/glfw3.h>
#include "log/logger.h"
#include "config/path.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_swapchain.h"
#include "vulkan_frame_context.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_texture.h"
#include "vulkan_sampler.h"
#include "vulkan_image_memory_manager.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "common/mesh_manager.h"
#include "vulkan_mesh.h"
#include "asset/asset_manager.h"
#include "asset/model.h"
#include "asset/mesh.h"
#include "asset/texture.h"

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

    void VulkanBackend::Initialize(const PipelineDesc &pipeline_desc)
    {
        device_ = std::make_unique<VulkanDevice>();
        device_->Initialize(window_);

        swapchain_ = std::make_unique<VulkanSwapchain>();
        swapchain_->Initialize(device_.get(), window_);
        msaa_sampe_count_ = swapchain_->GetMaxUsableSampleCount();
        InitVulkanContext();

        frame_context_ = std::make_unique<VulkanFrameContext>();
        frame_context_->Initialize(device_.get(), static_cast<uint32_t>(swapchain_->GetImageCount()));

        CreateGraphicsPipeline(pipeline_desc);

        SetupResource();

        CreateVertexBuffers();
        CreateUniformBuffers();
        CreateDescriptorPool();
        CreateDescriptorSets();
    }

    void VulkanBackend::BeginFrame()
    {
        // 1. wait for last frame to finish
        // 2. acquire RT
        // 3. submit draw command buffer
        // 3. wait for render and present

        frame_context_->WaitForInFlightFence();

        uint32_t image_index;
        VkResult acquire_image_res = frame_context_->AcquireNextImage(swapchain_->GetSwapchain(), image_index);

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
        frame_context_->ResetInFlightFence();
        UpdateUniformBuffer(frame_context_->GetCurrentFrameIndex());

        frame_context_->ResetCurrentSceneCommandBuffer();
        VkCommandBuffer scene_command_buffer = frame_context_->GetCurrentSceneCommandBuffer();
        RecordCommandBuffer(scene_command_buffer, image_index);

        frame_context_->Submit(scene_command_buffer, image_index);

        VkResult present_res = frame_context_->Present(swapchain_->GetSwapchain(), image_index);
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
    }

    void VulkanBackend::EndFrame()
    {
        frame_context_->AdvanceFrame();
    }

    void VulkanBackend::Present()
    {
    }

    void VulkanBackend::Cleanup()
    {
        vkDeviceWaitIdle(device_->GetLogicalDevice());

        CleanupSwapchain();

        for (size_t i = 0; i < per_pass_ubo_.buffer_handles_.size(); i++)
        {
            buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), per_pass_ubo_.buffer_handles_[i]);
        }

        for (size_t i = 0; i < per_object_ubo_.buffer_handles_.size(); i++)
        {
            buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), per_object_ubo_.buffer_handles_[i]);
        }

        vkDestroyDescriptorPool(device_->GetLogicalDevice(), descriptor_pool_, nullptr);
        pipeline_manager_->DestroyPipelineResource(device_->GetLogicalDevice(), pipeline_handle_);

        GraphicsContext context;
        context.native = static_cast<void *>(&context_);
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        texture_manager_->DestroyTexture(context, texture_handle_);
        sampler_manager_->DestroySampler(context, sampler_handle_);

        mesh_manager_->DestroyMesh(context, mesh_handle_);
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

    void VulkanBackend::SetupResource()
    {
        std::string texture_path = GetTextureDirectory() + "wallpaper.jpg";
        asset::AssetID id = asset::AssetManager::GetInstance().LoadSync(texture_path);
        auto texture_ptr = asset::AssetManager::GetInstance().GetResource<asset::TextureResource>(id);
        if(texture_ptr == nullptr)
        {
            return ;
        }
        TextureData& texture_data = *(texture_ptr->data);

        CreateTextures(texture_data);

        CreateDepthResource();
        CreateColorResource();

        Texture *texture = texture_manager_->GetTexture(texture_handle_);
        Texture *depth = texture_manager_->GetTexture(depth_handle_);
        Texture *color = texture_manager_->GetTexture(color_handle_);

        assert(texture);
        assert(depth);
        assert(color);

        VkImage tex_image = ConvertToVulkanTextureResource(texture->GetTextueHandle()).image;
        VkImage depth_image = ConvertToVulkanTextureResource(depth->GetTextueHandle()).image;
        VkImage color_image = ConvertToVulkanTextureResource(color->GetTextueHandle()).image;

        size_t image_size = texture_data.pixels.size();

        BufferHandle stage_handle;
        if (!texture_data.pixels.empty())
        {
            stage_handle = CreateUploadStageBufferResource(image_size);
            UploadDataToBuffer(stage_handle, image_size, texture_data.pixels.data());
        }

        // one-shot upload: begin, record, submit, wait — buffer freed in End
        VkCommandBuffer command_buffer = frame_context_->BeginSingleTimeCommands(frame_context_->GetGraphicsCommandPool());
        frame_context_->TransitionImageLayout(command_buffer, tex_image, TextureUsage::None, TextureUsage::TEXTURE_USAGE_TRANSFER_DST, 0, texture->settings_.mip_levels);
        frame_context_->TransitionImageLayout(command_buffer, depth_image, TextureUsage::None, TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT, 0, 1);
        frame_context_->TransitionImageLayout(command_buffer, color_image, TextureUsage::None, TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT, 0, 1);

        CopyBufferToImage(command_buffer, stage_handle, tex_image, texture_data.width, texture_data.height);

        frame_context_->TransitionImageLayout(command_buffer, tex_image, TextureUsage::TEXTURE_USAGE_TRANSFER_DST, TextureUsage::TEXTURE_USAGE_SAMPLE, 0, texture->settings_.mip_levels);
        frame_context_->EndSingleTimeCommands(command_buffer, frame_context_->GetGraphicsCommandPool(), device_->GetGraphicsQueue().queue);

        DestroyBufferResource(stage_handle);
        asset::AssetManager::GetInstance().UnRegisterAsset(id);
    }

    void VulkanBackend::CreateTextures(TextureData& texture_data)
    {
        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = static_cast<void *>(&context_);
        TextureSettings texture_settings{};
        texture_settings.mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(texture_data.width, texture_data.height)))) + 1;
        texture_settings.format = texture_data.format;
        texture_settings.usage = TextureUsage::TEXTURE_USAGE_TRANSFER_DST | TextureUsage::TEXTURE_USAGE_SAMPLE;
        texture_settings.sample_count = 1;
        texture_handle_ = texture_manager_->CreateTexture(context, texture_data, texture_settings);

        SamplerSettings sampler_settings{};
        sampler_settings.address_mode_u = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.address_mode_v = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.address_mode_w = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.enable_anisotropy = true;

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device_->GetPhysicalDevice(), &properties);
        sampler_settings.max_anisotropy = properties.limits.maxSamplerAnisotropy;
        sampler_settings.mag_filter = SamplerFilterType::SAMPLER_FILTER_LINEAR;
        sampler_settings.min_filter = SamplerFilterType::SAMPLER_FILTER_LINEAR;
        sampler_settings.mip_lod_bias = 0.f;
        sampler_settings.min_lod = 0.f;
        sampler_settings.max_lod = 0.f;

        sampler_handle_ = sampler_manager_->CreateSampler(context, sampler_settings);
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

    void VulkanBackend::CreateVertexBuffers()
    {
        std::string model_path = GetModelDirectory() + "sphere/sphere.obj";
        asset::AssetID model_id = asset::AssetManager::GetInstance().LoadSync(model_path);
        std::shared_ptr<asset::ModelResource> model_ptr = asset::AssetManager::GetInstance().GetResource<asset::ModelResource>(model_id);
        if (model_ptr)
        {
            std::shared_ptr<asset::MeshResource> mesh_ptr = model_ptr->GetMesh();
            if (mesh_ptr)
            {
                GraphicsContext context;
                context.native = &context_;
                context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;

                mesh_handle_ = mesh_manager_->CreateMesh(context, *mesh_ptr->data);
                asset::AssetManager::GetInstance().UnRegisterAsset(model_id);
                asset::AssetManager::GetInstance().UnRegisterAsset(model_ptr->GetData(asset::ModelGeometryType::KPMG_Mesh));

            }
        }
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

    void VulkanBackend::CreateUniformBuffers()
    {
        CreateUniformBuffer(sizeof(PerPassData), VulkanFrameContext::MAX_FRAMES_IN_FLIGHT, per_pass_ubo_);
        CreateUniformBuffer(sizeof(PerObjectData), VulkanFrameContext::MAX_FRAMES_IN_FLIGHT, per_object_ubo_);

        VkPhysicalDeviceProperties physical_device_props;
        vkGetPhysicalDeviceProperties(device_->GetPhysicalDevice(), &physical_device_props);
        VkDeviceSize alignment = physical_device_props.limits.minUniformBufferOffsetAlignment;

        std::vector<BufferHandle> all_buffers;
        std::vector<UniformBufferData *> all_ubos = {&per_pass_ubo_, &per_object_ubo_};
        VkDeviceSize total_size = 0;
        for (auto *ubo : all_ubos)
        {
            for (auto handle : ubo->buffer_handles_)
            {
                all_buffers.push_back(handle);
            }
        }

        for (auto *ubo : all_ubos)
        {
            VkDeviceSize aligned_size = (ubo->element_size + alignment - 1) & ~(alignment - 1);
            for (size_t i = 0; i < ubo->buffer_handles_.size(); i++)
            {
                total_size += aligned_size;
            }
        }

        void *base_mapped_ptr = nullptr;
        buffer_manager_->MapBuffer(
            device_->GetLogicalDevice(),
            all_buffers.back(),
            total_size,
            &base_mapped_ptr);

        // Calculate offsets and assign mapped pointers
        VkDeviceSize base_offset = buffer_manager_->GetBufferResource(all_buffers.back())->allocation.offset;
        for (auto *ubo : all_ubos)
        {
            for (size_t i = 0; i < ubo->buffer_handles_.size(); i++)
            {
                auto *resource = buffer_manager_->GetBufferResource(ubo->buffer_handles_[i]);
                assert(resource->allocation.offset >= base_offset);
                ubo->buffer_mapped_ptr_[i] = reinterpret_cast<char *>(base_mapped_ptr) + resource->allocation.offset - base_offset;
            }
        }
    }

    void VulkanBackend::CreateUniformBuffer(uint32_t size, uint32_t element_count, UniformBufferData &ubo_data)
    {
        VkDeviceSize buffer_size = size;

        ubo_data.element_size = size;
        ubo_data.buffer_handles_.resize(element_count);
        ubo_data.buffer_mapped_ptr_.resize(element_count);

        VkBufferCreateInfo buffer_create_info{};
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        buffer_create_info.queueFamilyIndexCount = 0;
        buffer_create_info.pQueueFamilyIndices = nullptr;
        buffer_create_info.size = buffer_size;

        for (size_t i = 0; i < VulkanFrameContext::MAX_FRAMES_IN_FLIGHT; i++)
        {
            ubo_data.buffer_handles_[i] = buffer_manager_->CreateBufferResource(device_->GetPhysicalDevice(), device_->GetLogicalDevice(), &buffer_create_info, VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM);
        }
    }

    void VulkanBackend::CreateDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> pool_sizes;
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = static_cast<uint32_t>(VulkanFrameContext::MAX_FRAMES_IN_FLIGHT * 2);
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = static_cast<uint32_t>(VulkanFrameContext::MAX_FRAMES_IN_FLIGHT);

        VkDescriptorPoolCreateInfo descriptor_pool_create_info{};
        descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool_create_info.maxSets = static_cast<uint32_t>(VulkanFrameContext::MAX_FRAMES_IN_FLIGHT);
        descriptor_pool_create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        descriptor_pool_create_info.pPoolSizes = pool_sizes.data();

        if (vkCreateDescriptorPool(device_->GetLogicalDevice(), &descriptor_pool_create_info, nullptr, &descriptor_pool_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create descriptor pool");
            throw std::runtime_error("Failed to create descriptor pool");
        }
    }

    void VulkanBackend::CreateDescriptorSets()
    {
        VulkanPipelineResource *resource = pipeline_manager_->GetPipelineResource(pipeline_handle_);
        std::vector<VkDescriptorSetLayout> layouts(VulkanFrameContext::MAX_FRAMES_IN_FLIGHT, resource->descriptor_set_layouts[0].layout);
        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = descriptor_pool_;
        allocate_info.descriptorSetCount = static_cast<uint32_t>(VulkanFrameContext::MAX_FRAMES_IN_FLIGHT);
        allocate_info.pSetLayouts = layouts.data();

        descriptor_sets_.resize(VulkanFrameContext::MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateDescriptorSets(device_->GetLogicalDevice(), &allocate_info, descriptor_sets_.data()) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_BACKEND_LOG_NAME, LOG_LEVEL_ERROR, "Failed to allocate descriptor set");
            throw std::runtime_error("Failed to allocate descriptor set");
        }

        for (uint32_t i = 0; i < VulkanFrameContext::MAX_FRAMES_IN_FLIGHT; i++)
        {
            std::array<VkWriteDescriptorSet, 3> descriptor_writes{};
            VkDescriptorBufferInfo pass_descriptor_buffer_info{};
            VkDescriptorBufferInfo object_descriptor_buffer_info{};
            VkDescriptorImageInfo image_info{};

            WriteUniformBufferDescriptorSet(descriptor_writes[0], pass_descriptor_buffer_info, descriptor_sets_[i], per_pass_ubo_, resource->descriptor_set_layouts[0].bindings[0], i);
            WriteUniformBufferDescriptorSet(descriptor_writes[1], object_descriptor_buffer_info, descriptor_sets_[i], per_object_ubo_, resource->descriptor_set_layouts[0].bindings[1], i);
            WriteImageDescriptorSet(descriptor_writes[2], image_info, descriptor_sets_[i], texture_handle_, sampler_handle_, resource->descriptor_set_layouts[0].bindings[2], i);

            vkUpdateDescriptorSets(device_->GetLogicalDevice(), static_cast<uint32_t>(descriptor_writes.size()), descriptor_writes.data(), 0, nullptr);
        }
    }

    void VulkanBackend::WriteUniformBufferDescriptorSet(VkWriteDescriptorSet &out, VkDescriptorBufferInfo &desc_info, VkDescriptorSet descriptor_set, const UniformBufferData &ubo_data, VkDescriptorSetLayoutBinding binding, uint32_t frame_index)
    {
        VulkanBufferResource *buffer_resource = buffer_manager_->GetBufferResource(ubo_data.buffer_handles_[frame_index]);
        desc_info.buffer = buffer_resource->buffer;
        desc_info.offset = 0;
        desc_info.range = ubo_data.element_size;

        out.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out.dstSet = descriptor_set;
        out.dstBinding = binding.binding;
        out.dstArrayElement = 0;
        out.descriptorType = binding.descriptorType;
        out.descriptorCount = binding.descriptorCount;
        out.pBufferInfo = &desc_info;
    }

    void VulkanBackend::WriteImageDescriptorSet(VkWriteDescriptorSet &out, VkDescriptorImageInfo &image_info, VkDescriptorSet descriptor_set, TextureHandle texture_handle, SamplerHandle sampler_handle, VkDescriptorSetLayoutBinding binding, uint32_t frame_index)
    {

        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Texture *texture = texture_manager_->GetTexture(texture_handle);
        VulkanTextureResource texture_resource = ConvertToVulkanTextureResource(texture->GetTextueHandle());
        image_info.imageView = texture_resource.view;
        Sampler *sampler = sampler_manager_->GetSampler(sampler_handle);
        VulkanSamplerResource sample_resource = ConvertToVulkanSamplerResource(sampler->GetSampleHandle());
        image_info.sampler = sample_resource.sampler;

        out.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out.dstSet = descriptor_set;
        out.dstBinding = binding.binding;
        out.dstArrayElement = 0;
        out.descriptorType = binding.descriptorType;
        out.descriptorCount = binding.descriptorCount;
        out.pImageInfo = &image_info;
    }

    void VulkanBackend::UpdateUniformBuffer(uint32_t current_image)
    {
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

        Vector3f camera = {0.f, 0.f, 2.f};
        Vector3f target = {0.f, 0.f, 0.f};
        Vector3f dir = target - camera;

        PerPassData per_pass_data{};
        per_pass_data.camera_data.view = Matrix4f::MakeCameraMatrix(camera, dir, {0.f, 1.f, 0.f}).Transpose();
        float aspect = swapchain_->GetExtent().width / (float)swapchain_->GetExtent().height;
        per_pass_data.camera_data.proj = Matrix4f::MakePerProjMatrix(math::DegreeToRadian(45.f), aspect, 0.1f, 10.f).Transpose();
        CopyToUniformBuffer(per_pass_ubo_.buffer_mapped_ptr_[current_image], &per_pass_data, per_pass_ubo_.element_size);

        Transform3f model{};
        model.scale_ = {0.5f, 0.5f, 0.5f};
        model.rotator_.pitch_ = time * 90.f;

        PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(model).Transpose();

        CopyToUniformBuffer(per_object_ubo_.buffer_mapped_ptr_[current_image], &per_object_data, per_object_ubo_.element_size);
    }

    void VulkanBackend::CopyToUniformBuffer(void *buffer_mapped_ptr, const void *data, uint32_t size)
    {
        assert(buffer_mapped_ptr);
        memcpy(buffer_mapped_ptr, data, size);
    }

    VkCommandBuffer VulkanBackend::GetCurrentUICommandBuffer() const
    {
        return frame_context_->GetCurrentUICommandBuffer();
    }

    void VulkanBackend::CopyBufferToImage(VkCommandBuffer cmd, BufferHandle buffer_handle, VkImage image, uint32_t width, uint32_t height)
    {
        VulkanBufferResource *buffer_resource = buffer_manager_->GetBufferResource(buffer_handle);
        frame_context_->CopyBufferToImage(cmd, buffer_resource->buffer, image, width, height);
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

        // TODO: transition image layout for presentation

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
            VK_IMAGE_ASPECT_DEPTH_BIT,
            0, 1);

        vkCmdBeginRendering(commandbuffer, &render_info);
        {
            VulkanPipelineResource *pipeline_resource = pipeline_manager_->GetPipelineResource(pipeline_handle_);
            vkCmdBindPipeline(commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_resource->pipeline);

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

            MeshResource mesh_resource = mesh_manager_->GetMesh(mesh_handle_)->GetMeshHandle();
            const VulkanMeshResource *vk_mesh_resource = static_cast<const VulkanMeshResource *>(mesh_resource.native);
            BufferHandle vertex_handle = vk_mesh_resource->vertex_handle;
            BufferHandle index_handle = vk_mesh_resource->index_handle;

            VulkanBufferResource *index_buffer_resource = buffer_manager_->GetBufferResource(index_handle);
            VulkanBufferResource *vertex_buffer_resource = buffer_manager_->GetBufferResource(vertex_handle);

            VkBuffer vertexBuffers[] = {vertex_buffer_resource->buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandbuffer, 0, 1, vertexBuffers, offsets);

            uint32_t index_count = static_cast<uint32_t>(vk_mesh_resource->sections[0].index_count);
            vkCmdBindIndexBuffer(commandbuffer, index_buffer_resource->buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdBindDescriptorSets(commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_resource->layout, 0, 1, &descriptor_sets_[frame_context_->GetCurrentFrameIndex()], 0, nullptr);

            vkCmdDrawIndexed(commandbuffer, index_count, 1, 0, 0, 0);
        }
        vkCmdEndRendering(commandbuffer);
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
