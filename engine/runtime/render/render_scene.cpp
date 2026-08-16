#include "render_scene.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "log/logger.h"
#include "config/path.h"
#include "graphics/backend/common/mesh.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/texture.h"
#include "graphics/backend/common/texture_manager.h"
#include "graphics/backend/common/sampler_manager.h"
#include "graphics/backend/common/mesh_manager.h"
#include "graphics/backend/vulkan/vulkan_backend.h"
#include "graphics/backend/vulkan/vulkan_buffer_manager.h"
#include "graphics/backend/vulkan/vulkan_mesh.h"
#include "graphics/backend/vulkan/vulkan_pipeline_manager.h"
#include "graphics/backend/vulkan/vulkan_sampler.h"
#include "graphics/backend/vulkan/vulkan_texture.h"
#include "asset/asset_manager.h"
#include "asset/model.h"
#include "asset/mesh.h"
#include "asset/texture.h"
#include "math/math_header.h"

namespace kpengine::render
{
#define KP_RENDER_SCENE_LOG_NAME "RenderSceneLog"

    namespace
    {
        // Matches VulkanFrameContext::MAX_FRAMES_IN_FLIGHT — one UBO + descriptor
        // set per frame in flight.
        constexpr uint32_t kMaxFramesInFlight = 2;
    }

    void RenderScene::Initialize(graphics::VulkanBackend *backend)
    {
        backend_ = backend;
        CreateTexture();
        CreateMesh();
        CreateUniformBuffers();
        CreateDescriptorSets();
    }

    void RenderScene::CreateTexture()
    {
        std::string texture_path = GetTextureDirectory() + "wallpaper.jpg";
        asset::AssetID id = asset::AssetManager::GetInstance().LoadSync(texture_path);
        auto texture_ptr = asset::AssetManager::GetInstance().GetResource<asset::TextureResource>(id);
        if (texture_ptr == nullptr)
        {
            return;
        }
        data::TextureData &texture_data = *(texture_ptr->data);

        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = static_cast<void *>(&backend_->GetVulkanContext());

        graphics::TextureSettings texture_settings{};
        texture_settings.mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(texture_data.width, texture_data.height)))) + 1;
        texture_settings.format = texture_data.format;
        texture_settings.usage = graphics::TextureUsage::TEXTURE_USAGE_TRANSFER_DST | graphics::TextureUsage::TEXTURE_USAGE_SAMPLE;
        texture_settings.sample_count = 1;
        texture_handle_ = backend_->GetTextureManager()->CreateTexture(context, texture_data, texture_settings);

        backend_->UploadTexturePixels(texture_handle_, texture_data.pixels.data(), texture_data.pixels.size(),
                                      texture_data.width, texture_data.height, texture_settings.mip_levels);

        graphics::SamplerSettings sampler_settings{};
        sampler_settings.address_mode_u = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.address_mode_v = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.address_mode_w = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.enable_anisotropy = true;

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(backend_->GetVulkanContext().physical_device, &properties);
        sampler_settings.max_anisotropy = properties.limits.maxSamplerAnisotropy;
        sampler_settings.mag_filter = graphics::SamplerFilterType::SAMPLER_FILTER_LINEAR;
        sampler_settings.min_filter = graphics::SamplerFilterType::SAMPLER_FILTER_LINEAR;
        sampler_settings.mip_lod_bias = 0.f;
        sampler_settings.min_lod = 0.f;
        sampler_settings.max_lod = 0.f;
        sampler_handle_ = backend_->GetSamplerManager()->CreateSampler(context, sampler_settings);

        asset::AssetManager::GetInstance().UnRegisterAsset(id);
    }

    void RenderScene::CreateMesh()
    {
        std::string model_path = GetModelDirectory() + "sphere/sphere.obj";
        asset::AssetID model_id = asset::AssetManager::GetInstance().LoadSync(model_path);
        std::shared_ptr<asset::ModelResource> model_ptr = asset::AssetManager::GetInstance().GetResource<asset::ModelResource>(model_id);
        if (model_ptr)
        {
            std::shared_ptr<asset::MeshResource> mesh_ptr = model_ptr->GetMesh();
            if (mesh_ptr)
            {
                GraphicsContext context{};
                context.native = static_cast<void *>(&backend_->GetVulkanContext());
                context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;

                mesh_handle_ = backend_->GetMeshManager()->CreateMesh(context, *mesh_ptr->data);
                asset::AssetManager::GetInstance().UnRegisterAsset(model_id);
                asset::AssetManager::GetInstance().UnRegisterAsset(model_ptr->GetData(asset::ModelGeometryType::KPMG_Mesh));
            }
        }
    }

    void RenderScene::CreateUniformBuffers()
    {
        per_pass_ubo_.element_size = static_cast<uint32_t>(sizeof(graphics::PerPassData));
        per_pass_ubo_.handles.resize(kMaxFramesInFlight);
        per_pass_ubo_.mapped.resize(kMaxFramesInFlight);
        per_object_ubo_.element_size = static_cast<uint32_t>(sizeof(graphics::PerObjectData));
        per_object_ubo_.handles.resize(kMaxFramesInFlight);
        per_object_ubo_.mapped.resize(kMaxFramesInFlight);

        for (uint32_t i = 0; i < kMaxFramesInFlight; i++)
        {
            per_pass_ubo_.handles[i] = backend_->CreateUniformBuffer(per_pass_ubo_.element_size);
            per_pass_ubo_.mapped[i] = backend_->MapUniformBuffer(per_pass_ubo_.handles[i], per_pass_ubo_.element_size);
            per_object_ubo_.handles[i] = backend_->CreateUniformBuffer(per_object_ubo_.element_size);
            per_object_ubo_.mapped[i] = backend_->MapUniformBuffer(per_object_ubo_.handles[i], per_object_ubo_.element_size);
        }
    }

    void RenderScene::CreateDescriptorSets()
    {
        const graphics::VulkanPipelineResource *pipeline_resource = backend_->GetPipelineResource();

        std::array<VkDescriptorPoolSize, 2> pool_sizes;
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = kMaxFramesInFlight * 2;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = kMaxFramesInFlight;

        VkDescriptorPoolCreateInfo descriptor_pool_create_info{};
        descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool_create_info.maxSets = kMaxFramesInFlight;
        descriptor_pool_create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        descriptor_pool_create_info.pPoolSizes = pool_sizes.data();

        if (vkCreateDescriptorPool(backend_->GetVulkanContext().logical_device, &descriptor_pool_create_info, nullptr, &descriptor_pool_) != VK_SUCCESS)
        {
            KP_LOG(KP_RENDER_SCENE_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create descriptor pool");
            throw std::runtime_error("Failed to create descriptor pool");
        }

        std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, pipeline_resource->descriptor_set_layouts[0].layout);
        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = descriptor_pool_;
        allocate_info.descriptorSetCount = kMaxFramesInFlight;
        allocate_info.pSetLayouts = layouts.data();

        descriptor_sets_.resize(kMaxFramesInFlight);
        if (vkAllocateDescriptorSets(backend_->GetVulkanContext().logical_device, &allocate_info, descriptor_sets_.data()) != VK_SUCCESS)
        {
            KP_LOG(KP_RENDER_SCENE_LOG_NAME, LOG_LEVEL_ERROR, "Failed to allocate descriptor set");
            throw std::runtime_error("Failed to allocate descriptor set");
        }

        for (uint32_t i = 0; i < kMaxFramesInFlight; i++)
        {
            std::array<VkWriteDescriptorSet, 3> descriptor_writes{};
            VkDescriptorBufferInfo pass_info{};
            VkDescriptorBufferInfo object_info{};
            VkDescriptorImageInfo image_info{};

            WriteUniformBufferDescriptor(descriptor_writes[0], pass_info, descriptor_sets_[i], per_pass_ubo_,
                                         pipeline_resource->descriptor_set_layouts[0].bindings[0], i);
            WriteUniformBufferDescriptor(descriptor_writes[1], object_info, descriptor_sets_[i], per_object_ubo_,
                                         pipeline_resource->descriptor_set_layouts[0].bindings[1], i);
            WriteImageDescriptor(descriptor_writes[2], image_info, descriptor_sets_[i],
                                 pipeline_resource->descriptor_set_layouts[0].bindings[2], i);

            vkUpdateDescriptorSets(backend_->GetVulkanContext().logical_device,
                                   static_cast<uint32_t>(descriptor_writes.size()), descriptor_writes.data(), 0, nullptr);
        }
    }

    void RenderScene::WriteUniformBufferDescriptor(VkWriteDescriptorSet &out, VkDescriptorBufferInfo &info,
                                                   VkDescriptorSet set, const UniformBuffer &ubo,
                                                   VkDescriptorSetLayoutBinding binding, uint32_t frame_index)
    {
        graphics::VulkanBufferResource *buffer_resource = backend_->GetBufferResource(ubo.handles[frame_index]);
        info.buffer = buffer_resource->buffer;
        info.offset = 0;
        info.range = ubo.element_size;

        out.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out.dstSet = set;
        out.dstBinding = binding.binding;
        out.dstArrayElement = 0;
        out.descriptorType = binding.descriptorType;
        out.descriptorCount = binding.descriptorCount;
        out.pBufferInfo = &info;
    }

    void RenderScene::WriteImageDescriptor(VkWriteDescriptorSet &out, VkDescriptorImageInfo &info,
                                           VkDescriptorSet set, VkDescriptorSetLayoutBinding binding,
                                           uint32_t frame_index)
    {
        (void)frame_index;
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        graphics::Texture *texture = backend_->GetTextureManager()->GetTexture(texture_handle_);
        graphics::VulkanTextureResource texture_resource = graphics::ConvertToVulkanTextureResource(texture->GetTextueHandle());
        info.imageView = texture_resource.view;
        graphics::Sampler *sampler = backend_->GetSamplerManager()->GetSampler(sampler_handle_);
        graphics::VulkanSamplerResource sampler_resource = graphics::ConvertToVulkanSamplerResource(sampler->GetSampleHandle());
        info.sampler = sampler_resource.sampler;

        out.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out.dstSet = set;
        out.dstBinding = binding.binding;
        out.dstArrayElement = 0;
        out.descriptorType = binding.descriptorType;
        out.descriptorCount = binding.descriptorCount;
        out.pImageInfo = &info;
    }

    void RenderScene::UpdateUniformBuffers(uint32_t frame_index)
    {
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

        Vector3f camera = {0.f, 0.f, 2.f};
        Vector3f target = {0.f, 0.f, 0.f};
        Vector3f dir = target - camera;

        graphics::PerPassData per_pass_data{};
        per_pass_data.camera_data.view = Matrix4f::MakeCameraMatrix(camera, dir, {0.f, 1.f, 0.f}).Transpose();
        float aspect = backend_->GetSwapchainExtent().width / (float)backend_->GetSwapchainExtent().height;
        per_pass_data.camera_data.proj = Matrix4f::MakePerProjMatrix(math::DegreeToRadian(45.f), aspect, 0.1f, 10.f).Transpose();

        Transform3f model{};
        model.scale_ = {0.5f, 0.5f, 0.5f};
        model.rotator_.pitch_ = time * 90.f;

        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(model).Transpose();

        if (per_pass_ubo_.mapped[frame_index])
        {
            std::memcpy(per_pass_ubo_.mapped[frame_index], &per_pass_data, per_pass_ubo_.element_size);
        }
        if (per_object_ubo_.mapped[frame_index])
        {
            std::memcpy(per_object_ubo_.mapped[frame_index], &per_object_data, per_object_ubo_.element_size);
        }
    }

    void RenderScene::Tick(float delta_time)
    {
        (void)delta_time;
        UpdateUniformBuffers(backend_->GetCurrentFrameIndex());
    }

    void RenderScene::Record()
    {
        VkCommandBuffer commandbuffer = backend_->GetCurrentSceneCommandBuffer();

        const graphics::VulkanPipelineResource *pipeline_resource = backend_->GetPipelineResource();
        vkCmdBindPipeline(commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_resource->pipeline);

        graphics::MeshResource mesh_resource = backend_->GetMeshManager()->GetMesh(mesh_handle_)->GetMeshHandle();
        const graphics::VulkanMeshResource *vk_mesh_resource = static_cast<const graphics::VulkanMeshResource *>(mesh_resource.native);
        graphics::VulkanBufferResource *vertex_buffer = backend_->GetBufferResource(vk_mesh_resource->vertex_handle);
        graphics::VulkanBufferResource *index_buffer = backend_->GetBufferResource(vk_mesh_resource->index_handle);

        VkBuffer vertex_buffers[] = {vertex_buffer->buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandbuffer, 0, 1, vertex_buffers, offsets);

        uint32_t index_count = static_cast<uint32_t>(vk_mesh_resource->sections[0].index_count);
        vkCmdBindIndexBuffer(commandbuffer, index_buffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_resource->layout, 0, 1,
                                &descriptor_sets_[backend_->GetCurrentFrameIndex()], 0, nullptr);
        vkCmdDrawIndexed(commandbuffer, index_count, 1, 0, 0, 0);
    }

    void RenderScene::Cleanup()
    {
        if (!backend_)
        {
            return;
        }

        GraphicsContext context{};
        context.type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        context.native = static_cast<void *>(&backend_->GetVulkanContext());

        if (descriptor_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(backend_->GetVulkanContext().logical_device, descriptor_pool_, nullptr);
        }
        for (auto handle : per_pass_ubo_.handles)
        {
            backend_->DestroyBufferResource(handle);
        }
        for (auto handle : per_object_ubo_.handles)
        {
            backend_->DestroyBufferResource(handle);
        }
        if (mesh_handle_.IsValid())
        {
            backend_->GetMeshManager()->DestroyMesh(context, mesh_handle_);
        }
        if (sampler_handle_.IsValid())
        {
            backend_->GetSamplerManager()->DestroySampler(context, sampler_handle_);
        }
        if (texture_handle_.IsValid())
        {
            backend_->GetTextureManager()->DestroyTexture(context, texture_handle_);
        }
    }
}
