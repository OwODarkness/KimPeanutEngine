#include "vulkan_render_target_manager.h"

#include "common/render_target_validation.h"
#include "common/texture.h"
#include "common/texture_manager.h"
#include "vulkan_frame_context.h"
#include "vulkan_texture.h"

namespace kpengine::graphics
{
    namespace
    {
        VkAttachmentLoadOp ToLoadOp(RenderTargetLoadOp op)
        {
            switch (op)
            {
            case RenderTargetLoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case RenderTargetLoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
            case RenderTargetLoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }

        VkAttachmentStoreOp ToStoreOp(RenderTargetStoreOp op)
        {
            switch (op)
            {
            case RenderTargetStoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
            case RenderTargetStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_STORE_OP_STORE;
        }

        bool IsLive(const RenderTargetResource &target)
        {
            return !target.color_attachments.empty() || target.depth.IsValid();
        }
    }

    VulkanRenderTargetManager::VulkanRenderTargetManager(
        VkDevice logical_device, GraphicsContext context, VulkanFrameContext &frame_context,
        TextureManager &texture_manager)
        : logical_device_(logical_device), context_(context), frame_context_(&frame_context),
          texture_manager_(&texture_manager)
    {
    }

    RenderTargetHandle VulkanRenderTargetManager::Create(const RenderTargetDesc &desc)
    {
        if (!ValidateRenderTargetDesc(desc))
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
        RenderTargetResource target{};
        target.desc = desc;
        for (const RenderTargetColorAttachment &attachment : desc.color_attachments)
        {
            TextureSettings color_settings{};
            color_settings.sample_count = desc.sample_count;
            color_settings.format = attachment.format;
            color_settings.usage = TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT |
                                   TextureUsage::TEXTURE_USAGE_SAMPLE |
                                   TextureUsage::TEXTURE_USAGE_TRANSFER_SRC;
            color_settings.aspect = ImageAspect::IMAGE_ASPECT_COLOR;
            color_settings.mutable_format =
                attachment.format == TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
            target.color_attachments.push_back(
                texture_manager_->CreateTexture(context_, data, color_settings));
            if (!target.color_attachments.back().IsValid())
            {
                break;
            }
        }
        bool success = target.color_attachments.size() == desc.color_attachments.size();
        if (success && desc.depth.has_value())
        {
            TextureSettings depth_settings{};
            depth_settings.sample_count = desc.sample_count;
            depth_settings.format = desc.depth->format;
            depth_settings.usage = TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT;
            if (desc.depth->shader_readable)
            {
                depth_settings.usage = depth_settings.usage | TextureUsage::TEXTURE_USAGE_SAMPLE;
            }
            depth_settings.aspect = ImageAspect::IMAGE_ASPECT_DEPTH;
            target.depth = texture_manager_->CreateTexture(context_, data, depth_settings);
            success = target.depth.IsValid();
        }
        if (!success)
        {
            for (const TextureHandle &color : target.color_attachments)
            {
                if (color.IsValid()) texture_manager_->DestroyTexture(context_, color);
            }
            if (target.depth.IsValid()) texture_manager_->DestroyTexture(context_, target.depth);
            handles_.Destroy(handle);
            return {};
        }

        TargetState state{};
        state.color_layouts.assign(target.color_attachments.size(), VK_IMAGE_LAYOUT_UNDEFINED);
        if (!target.color_attachments.empty() &&
            desc.color_attachments[0].format == TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB)
        {
            Texture *color_texture = texture_manager_->GetTexture(target.color_attachments[0]);
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
                for (const TextureHandle &color : target.color_attachments)
                {
                    texture_manager_->DestroyTexture(context_, color);
                }
                if (target.depth.IsValid()) texture_manager_->DestroyTexture(context_, target.depth);
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
        if (index >= targets_.size() || !IsLive(targets_[index]))
        {
            return false;
        }
        TargetState &state = states_[index];
        if (state.editor_preview_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(logical_device_, state.editor_preview_view, nullptr);
        }
        for (const TextureHandle &color : targets_[index].color_attachments)
        {
            texture_manager_->DestroyTexture(context_, color);
        }
        if (targets_[index].depth.IsValid())
        {
            texture_manager_->DestroyTexture(context_, targets_[index].depth);
        }
        targets_[index] = {};
        state = {};
        return handles_.Destroy(handle);
    }

    TextureHandle VulkanRenderTargetManager::GetColor(RenderTargetHandle handle) const
    {
        return GetColorAttachment(handle, 0);
    }

    TextureHandle VulkanRenderTargetManager::GetColorAttachment(RenderTargetHandle handle,
                                                                uint32_t index) const
    {
        const uint32_t slot = handles_.Get(handle);
        if (slot >= targets_.size() || index >= targets_[slot].color_attachments.size())
        {
            return {};
        }
        return targets_[slot].color_attachments[index];
    }

    TextureHandle VulkanRenderTargetManager::GetDepthAttachment(RenderTargetHandle handle) const
    {
        const uint32_t slot = handles_.Get(handle);
        return slot < targets_.size() ? targets_[slot].depth : TextureHandle{};
    }

    TextureHandle VulkanRenderTargetManager::GetSampledDepthAttachment(
        RenderTargetHandle handle) const
    {
        const uint32_t slot = handles_.Get(handle);
        if (slot >= targets_.size() || !targets_[slot].desc.depth.has_value() ||
            !targets_[slot].desc.depth->shader_readable)
        {
            return {};
        }
        return targets_[slot].depth;
    }

    RenderTargetView VulkanRenderTargetManager::GetView(RenderTargetHandle handle) const
    {
        const uint32_t index = handles_.Get(handle);
        if (index >= targets_.size() || targets_[index].color_attachments.empty())
        {
            return {};
        }
        const RenderTargetResource &target = targets_[index];
        Texture *color_texture = texture_manager_->GetTexture(target.color_attachments[0]);
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
        return index < targets_.size() && !targets_[index].color_attachments.empty() &&
               targets_[index].color_attachments[0].IsValid() &&
               targets_[index].desc.color_attachments[0].format ==
                   TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
    }

    bool VulkanRenderTargetManager::GetReadbackSource(RenderTargetHandle handle,
                                                       ReadbackSource &out_source) const
    {
        out_source = {};
        const uint32_t index = handles_.Get(handle);
        if (index >= targets_.size() || targets_[index].color_attachments.empty() ||
            targets_[index].desc.color_attachments[0].format !=
                TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB)
        {
            return false;
        }
        const Texture *const color_texture =
            texture_manager_->GetTexture(targets_[index].color_attachments[0]);
        if (color_texture == nullptr)
        {
            return false;
        }
        const VulkanTextureResource color =
            ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
        if (color.image == VK_NULL_HANDLE ||
            states_[index].color_layouts.empty() ||
            states_[index].color_layouts[0] == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            return false;
        }
        out_source = {color.image, states_[index].color_layouts[0],
                      targets_[index].desc.width, targets_[index].desc.height};
        return true;
    }

    const RenderTargetDesc *VulkanRenderTargetManager::GetDesc(RenderTargetHandle handle) const
    {
        const uint32_t index = handles_.Get(handle);
        return index < targets_.size() ? &targets_[index].desc : nullptr;
    }

    bool VulkanRenderTargetManager::BeginRendering(VkCommandBuffer command_buffer,
                                                    RenderTargetHandle handle)
    {
        if (command_buffer == VK_NULL_HANDLE || active_target_.IsValid()) return false;
        const uint32_t index = handles_.Get(handle);
        if (index >= targets_.size()) return false;
        const RenderTargetResource &target = targets_[index];
        if (target.color_attachments.empty() && !target.depth.IsValid()) return false;

        TargetState &state = states_[index];
        std::vector<VkRenderingAttachmentInfo> color_attachments;
        color_attachments.reserve(target.color_attachments.size());
        for (uint32_t i = 0; i < target.color_attachments.size(); ++i)
        {
            Texture *color_texture = texture_manager_->GetTexture(target.color_attachments[i]);
            const VulkanTextureResource color = color_texture
                ? ConvertToVulkanTextureResource(color_texture->GetTextueHandle())
                : VulkanTextureResource{};
            if (color.image == VK_NULL_HANDLE || color.view == VK_NULL_HANDLE) return false;
            frame_context_->TransitionImageLayout(command_buffer, color.image,
                state.color_layouts[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                state.color_layouts[i] == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
            state.color_layouts[i] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            const RenderTargetColorAttachment &attachment = target.desc.color_attachments[i];
            VkRenderingAttachmentInfo info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            info.imageView = color.view;
            info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            info.loadOp = ToLoadOp(attachment.load_op);
            info.storeOp = ToStoreOp(attachment.store_op);
            VkClearValue clear{};
            clear.color = {{attachment.clear_color[0], attachment.clear_color[1],
                            attachment.clear_color[2], attachment.clear_color[3]}};
            info.clearValue = clear;
            color_attachments.push_back(info);
        }

        std::optional<VkRenderingAttachmentInfo> depth_attachment;
        if (target.depth.IsValid() && target.desc.depth.has_value())
        {
            Texture *depth_texture = texture_manager_->GetTexture(target.depth);
            const VulkanTextureResource depth = depth_texture
                ? ConvertToVulkanTextureResource(depth_texture->GetTextueHandle())
                : VulkanTextureResource{};
            if (depth.image == VK_NULL_HANDLE || depth.view == VK_NULL_HANDLE) return false;
            if (state.depth_layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
            {
                frame_context_->TransitionImageLayout(command_buffer, depth.image, state.depth_layout,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                    state.depth_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1);
                state.depth_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            }
            const RenderTargetDepthAttachment &depth_desc = *target.desc.depth;
            VkRenderingAttachmentInfo info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            info.imageView = depth.view;
            info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            info.loadOp = ToLoadOp(depth_desc.load_op);
            info.storeOp = ToStoreOp(depth_desc.store_op);
            info.clearValue.depthStencil = {depth_desc.clear_depth, depth_desc.clear_stencil};
            depth_attachment = info;
        }

        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = {target.desc.width, target.desc.height};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = static_cast<uint32_t>(color_attachments.size());
        rendering.pColorAttachments = color_attachments.empty() ? VK_NULL_HANDLE
                                                                : color_attachments.data();
        rendering.pDepthAttachment = depth_attachment ? &*depth_attachment : VK_NULL_HANDLE;
        vkCmdBeginRendering(command_buffer, &rendering);
        const VkViewport viewport{0.f, static_cast<float>(target.desc.height),
                                  static_cast<float>(target.desc.width),
                                  -static_cast<float>(target.desc.height), 0.f, 1.f};
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        const VkRect2D scissor{{0, 0}, {target.desc.width, target.desc.height}};
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
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
            TargetState &state = states_[index];
            for (uint32_t i = 0; i < targets_[index].color_attachments.size(); ++i)
            {
                Texture *color_texture = texture_manager_->GetTexture(
                    targets_[index].color_attachments[i]);
                if (!color_texture) continue;
                const VulkanTextureResource color =
                    ConvertToVulkanTextureResource(color_texture->GetTextueHandle());
                frame_context_->TransitionImageLayout(command_buffer, color.image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
                state.color_layouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            // Sampled depth becomes readable; write-only depth stays in its
            // attachment layout so the next pass reuses it without a barrier.
            if (targets_[index].depth.IsValid() &&
                targets_[index].desc.depth.has_value() &&
                targets_[index].desc.depth->shader_readable)
            {
                Texture *depth_texture = texture_manager_->GetTexture(targets_[index].depth);
                if (depth_texture)
                {
                    const VulkanTextureResource depth =
                        ConvertToVulkanTextureResource(depth_texture->GetTextueHandle());
                    frame_context_->TransitionImageLayout(command_buffer, depth.image,
                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1);
                    state.depth_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
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
            if (!IsLive(targets_[index])) continue;
            if (states_[index].editor_preview_view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(logical_device_, states_[index].editor_preview_view, nullptr);
            }
            for (const TextureHandle &color : targets_[index].color_attachments)
            {
                texture_manager_->DestroyTexture(context_, color);
            }
            if (targets_[index].depth.IsValid())
            {
                texture_manager_->DestroyTexture(context_, targets_[index].depth);
            }
        }
        targets_.clear();
        states_.clear();
        active_target_ = {};
        DestroySwapchainAttachments();
    }
}
