#include "texture_manager.h"
#include "vulkan/vulkan_texture.h"
#include "vulkan/vulkan_context.h"
#include "opengl/opengl_texture.h"
#include "opengl/opengl_context.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    TextureHandle TextureManager::CreateTexture(GraphicsContext context, const TextureData& data,  const TextureSettings& settings)
    {
        TextureHandle handle = handle_system_.Create();
        if(handle.id == resources_.size())
        {
            resources_.emplace_back();
        }
        TextureSlot& resource = resources_[handle.id];

        if (context.type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            OpenglContext *ol_context = static_cast<OpenglContext *>(context.native);
            resource.texture = std::make_unique<OpenglTexture>();
        }
        else if (context.type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            VulkanContext *vulkan_context = static_cast<VulkanContext *>(context.native);
            resource.texture = std::make_unique<VulkanTexture>();
        }

        resource.texture->Initialize(context, data, settings);

        return handle;
    }

    TextureSlot* TextureManager::GetTextureSlot(TextureHandle handle)
    {
        uint32_t index = handle_system_.Get(handle);

        if (index >= resources_.size())
        {
            KP_LOG("TextureManagerLog", LOG_LEVEL_ERROR, "Failed to get texture, out of range");
            throw std::runtime_error("Failed to get texture, out of range");
        }

        return &resources_[index];
    }

    Texture *TextureManager::GetTexture(TextureHandle handle)
    {
        TextureSlot* resource = GetTextureSlot(handle);
        if(resource)
        {
            return resource->texture.get();
        }
        return nullptr;
    }

    bool TextureManager::DestroyTexture(GraphicsContext context, TextureHandle handle)
    {
        Texture * texture = GetTexture(handle);
        if (texture == nullptr)
        {
            return false;
        }
        texture->Destroy(context);
        return handle_system_.Destroy(handle);
    }


}