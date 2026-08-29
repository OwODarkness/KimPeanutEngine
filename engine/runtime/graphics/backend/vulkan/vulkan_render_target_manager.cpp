#include "vulkan_render_target_manager.h"

#include "common/texture.h"
#include "common/texture_manager.h"
#include "vulkan_frame_context.h"
#include "vulkan_texture.h"

namespace kpengine::graphics
{
    VulkanRenderTargetManager::VulkanRenderTargetManager(
        VkDevice logical_device, GraphicsContext context, VulkanFrameContext &frame_context,
        TextureManager &texture_manager)
        : logical_device_(logical_device), context_(context), frame_context_(&frame_context),
          texture_manager_(&texture_manager)
    {
    }

    RenderTargetHandle VulkanRenderTargetManager::Create(const RenderTargetDesc &desc)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            return {};
        }
        const RenderTargetHandle handle = handles_.Create();
        if (handle.id == targets_.size())
        {
            targets_.emplace_back();
            states_.emplace_back();
        }

        TextureData data{};
        data.width = desc.width;
        data.height = desc.height;
        TextureSettings color_settings{};
        color_settings.format = desc.color_format;
        color_settings.usage = TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT |
                               TextureUsage::TEXTURE_USAGE_SAMPLE |
                               TextureUsage::TEXTURE_USAGE_TRANSFER_SRC;
        color_settings.aspect = ImageAspect::IMAGE_ASPECT_COLOR;
        color_settings.mutable_format = desc.color_format == TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        TextureSettings depth_settings{};
        depth_settings.format = desc.depth_format;
        depth_settings.usage = TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT;
        depth_settings.aspect = ImageAspect::IMAGE_ASPECT_DEPTH;

        RenderTargetResource target{};
        target.desc = desc;
        target.color = texture_manager_->CreateTexture(context_, data, color_settings);
        target.depth = texture_manager_->CreateTexture(context_, data, depth_settings);
        if (!target.color.IsValid() || !target.depth.IsValid())
        {
            if (target.color.IsValid()) texture_manager_->DestroyTexture(context_, target.color);
            if (target.depth.IsValid()) texture_manager_->DestroyTexture(context_, target.depth);
            handles_.Destroy(handle);
            return {};
        }

        TargetState state{};
        if (color_settings.mutable_format)
        {
            Texture *color_texture = texture_manager_->GetTexture(target.color);
            const VulkanTextureResource color_resource = color_texture
                ? ConvertToVulkanTextureResource(color_texture->GetTextueHandle())
                : VulkanTextureResource{};
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = color_resource.image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;
            if (color_resource.image == VK_NULL_HANDLE ||
                vkCreateImageView(logical_device_, &view_info, nullptr,
                                  &state.editor_preview_view) != VK_SUCCESS)
            {
                texture_manager_->DestroyTexture(context_, target.color);
                texture_manager_->DestroyTexture(context_, target.depth);
                handles_.Destroy(handle);
                return {};
            }
        }
        targets_[handle.id] = target;
        states_[handle.id] = state;
        return handle;
    }

    bool VulkanRenderTargetManager::Destroy(RenderTargetHandle handle)
    {
        const uint32_t index = handles_.Get(handle);
        if (index >= targets_.size() || !targets_[index].color.IsValid())
        {
            return false;
        }
        TargetState &state = states_[index];
        if (state.editor_preview_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(logical_device_, state.editor_preview_view, nullptr);
        }
        texture_manager_->DestroyTexture(context_, targets_[index].color);
        texture_manager_->DestroyTexture(context_, targets_[index].depth);
        targets_[index] = {};
        state = {};
        return handles_.Destroy(handle);
    }

    TextureHandle VulkanRenderTargetManager::GetColor(RenderTargetHandle handle) const
    {
        const uint32_t index = handles_.Get(handle);
        return index < targets_.size() ? targets_[index].color : TextureHandle{};
    }

    RenderTargetView VulkanRenderTargetManager::GetView(RenderTargetHandle handle) const
    {
        const uint32_t index = handles_.Get(handle);
        if (index >= targets_.size()) return {};
        const RenderTargetResource &target = targets_[index];
        Texture *color_texture = texture_manager_->GetTexture(target.color);
        if (!color_texture) return {};
        const VulkanTextureResource resource =
            ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
        const VkImageView view = states_[index].editor_preview_view != VK_NULL_HANDLE
            ? states_[index].editor_preview_view : resource.view;
        return {target.desc.width, target.desc.height,
                reinterpret_cast<uintptr_t>(resource.image), reinterpret_cast<uintptr_t>(view)};
    }

    bool VulkanRenderTargetManager::CanReadback(RenderTargetHandle handle) const
    {
        const uint32_t index = handles_.Get(handle);
        return index < targets_.size() && targets_[index].color.IsValid() &&
               targets_[index].desc.color_format == TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
    }

    bool VulkanRenderTargetManager::GetReadbackSource(RenderTargetHandle handle,
                                                       ReadbackSource &out_source) const
    {
        out_source = {};
        const uint32_t index = handles_.Get(handle);
        if (index >= targets_.size() ||
            targets_[index].desc.color_format != TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB)
        {
            return false;
        }
        const Texture *const color_texture = texture_manager_->GetTexture(targets_[index].color);
        if (color_texture == nullptr)
        {
            return false;
        }
        const VulkanTextureResource color =
            ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
        if (color.image == VK_NULL_HANDLE || states_[index].color_layout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            return false;
        }
        out_source = {color.image, states_[index].color_layout,
                      targets_[index].desc.width, targets_[index].desc.height};
        return true;
    }

    bool VulkanRenderTargetManager::BeginRendering(VkCommandBuffer command_buffer,
                                                    RenderTargetHandle handle)
    {
        if (command_buffer == VK_NULL_HANDLE || active_target_.IsValid()) return false;
        const uint32_t index = handles_.Get(handle);
        if (index >= targets_.size()) return false;
        const RenderTargetResource &target = targets_[index];
        Texture *color_texture = texture_manager_->GetTexture(target.color);
        Texture *depth_texture = texture_manager_->GetTexture(target.depth);
        if (!color_texture || !depth_texture) return false;
        const VulkanTextureResource color = ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
        const VulkanTextureResource depth = ConvertToVulkanTextureResource(depth_texture->GetTextueHandle());
        if (color.image == VK_NULL_HANDLE || color.view == VK_NULL_HANDLE ||
            depth.image == VK_NULL_HANDLE || depth.view == VK_NULL_HANDLE) return false;

        TargetState &state = states_[index];
        frame_context_->TransitionImageLayout(command_buffer, color.image, state.color_layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            state.color_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
        if (state.depth_layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
        {
            frame_context_->TransitionImageLayout(command_buffer, depth.image, state.depth_layout,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                state.depth_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1);
            state.depth_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }
        VkClearValue color_clear{};
        color_clear.color = {{0.f, 0.f, 0.f, 1.f}};
        VkRenderingAttachmentInfo color_attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color_attachment.imageView = color.view;
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = color_clear;
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.f, 0};
        VkRenderingAttachmentInfo depth_attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depth_attachment.imageView = depth.view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.clearValue = depth_clear;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = {target.desc.width, target.desc.height};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;
        rendering.pDepthAttachment = &depth_attachment;
        vkCmdBeginRendering(command_buffer, &rendering);
        const VkViewport viewport{0.f, 0.f, static_cast<float>(target.desc.width),
                                  static_cast<float>(target.desc.height), 0.f, 1.f};
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        const VkRect2D scissor{{0, 0}, {target.desc.width, target.desc.height}};
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
        state.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        active_target_ = handle;
        return true;
    }

    void VulkanRenderTargetManager::EndRendering(VkCommandBuffer command_buffer)
    {
        if (command_buffer == VK_NULL_HANDLE || !active_target_.IsValid()) return;
        vkCmdEndRendering(command_buffer);
        const uint32_t index = handles_.Get(active_target_);
        if (index < targets_.size())
        {
            Texture *color_texture = texture_manager_->GetTexture(targets_[index].color);
            if (color_texture)
            {
                const VulkanTextureResource color =
                    ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
                frame_context_->TransitionImageLayout(command_buffer, color.image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
                states_[index].color_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        active_target_ = {};
    }

    void VulkanRenderTargetManager::CreateSwapchainAttachments(
        uint32_t width, uint32_t height, uint32_t color_sample_count)
    {
        DestroySwapchainAttachments();
        TextureData data{};
        data.width = width;
        data.height = height;
        TextureSettings depth{};
        depth.format = TextureFormat::TEXTURE_FORMAT_D32;
        depth.usage = TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT;
        depth.aspect = ImageAspect::IMAGE_ASPECT_DEPTH;
        TextureSettings color{};
        color.format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        color.usage = TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT;
        color.aspect = ImageAspect::IMAGE_ASPECT_COLOR;
        color.sample_count = color_sample_count;
        swapchain_depth_ = texture_manager_->CreateTexture(context_, data, depth);
        swapchain_color_ = texture_manager_->CreateTexture(context_, data, color);
    }

    void VulkanRenderTargetManager::DestroySwapchainAttachments()
    {
        if (swapchain_depth_.IsValid()) texture_manager_->DestroyTexture(context_, swapchain_depth_);
        if (swapchain_color_.IsValid()) texture_manager_->DestroyTexture(context_, swapchain_color_);
        swapchain_depth_ = {};
        swapchain_color_ = {};
    }

    void VulkanRenderTargetManager::DestroyAll()
    {
        for (uint32_t index = 0; index < targets_.size(); ++index)
        {
            if (targets_[index].color.IsValid())
            {
                if (states_[index].editor_preview_view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(logical_device_, states_[index].editor_preview_view, nullptr);
                }
                texture_manager_->DestroyTexture(context_, targets_[index].color);
                texture_manager_->DestroyTexture(context_, targets_[index].depth);
            }
        }
        targets_.clear();
        states_.clear();
        active_target_ = {};
        DestroySwapchainAttachments();
    }
}
