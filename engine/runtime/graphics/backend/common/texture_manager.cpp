#include "texture_manager.h"
#if KPENGINE_GRAPHICS_ENABLE_VULKAN
#include "vulkan/vulkan_texture.h"
#endif
#if KPENGINE_GRAPHICS_ENABLE_OPENGL
#include "opengl/opengl_texture.h"
#endif
#include "log/logger.h"
#include "data/texture_mipmap.h"
namespace kpengine::graphics
{
    namespace
    {
        bool IsGpuAllocationOnlyTexture(const TextureData &data) noexcept
        {
            return data.width != 0 && data.height != 0 && data.depth == 1 &&
                   data.array_layers == 1 && data.pixels.empty() &&
                   data.mip_subresources.empty();
        }
    }

    TextureHandle TextureManager::CreateTexture(GraphicsContext context, const TextureData& data,  const TextureSettings& settings)
    {
        // Render targets intentionally provide dimensions without CPU pixels:
        // the backend allocates and initializes their GPU storage directly.
        // File-backed textures must still provide a complete validated chain.
        if (!IsGpuAllocationOnlyTexture(data) && !data::IsTextureMipChainValid(data))
        {
            KP_LOG("TextureManagerLog", LOG_LEVEL_ERROR,
                   "texture payload contains an invalid mip chain");
            return {};
        }
        // A texture image may only declare levels for which the CPU payload
        // contains initialized subresources. Native products and the loose
        // fallback both express level zero plus the explicit chain in data.
        TextureSettings resolved_settings = settings;
        resolved_settings.mip_levels = data.GetMipLevelCount();
        TextureHandle handle = handle_system_.Create();
        if(handle.id == resources_.size())
        {
            resources_.emplace_back();
        }
        TextureSlot& resource = resources_[handle.id];

        if (context.type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
#if KPENGINE_GRAPHICS_ENABLE_OPENGL
            resource.texture = std::make_unique<OpenglTexture>();
#endif
        }
        else if (context.type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
#if KPENGINE_GRAPHICS_ENABLE_VULKAN
            resource.texture = std::make_unique<VulkanTexture>();
#endif
        }

        if (!resource.texture)
        {
            handle_system_.Destroy(handle);
            return {};
        }

        resource.texture->Initialize(context, data, resolved_settings);

        return handle;
    }

    TextureSlot* TextureManager::GetTextureSlot(TextureHandle handle)
    {
        uint32_t index = handle_system_.Get(handle);

        if(!handle.IsValid())
        {
            KP_LOG("TextureManagerLog", LOG_LEVEL_ERROR, "Invalid handle");
            return nullptr;
        }
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
