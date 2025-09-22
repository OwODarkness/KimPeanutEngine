#include "texture_manager.h"
#include "vulkan/vulkan_texture.h"
#include "vulkan/vulkan_context.h"
#include "opengl/opengl_texture.h"
#include "opengl/opengl_context.h"
#include "log/logger.h"
namespace kpengine::graphics
{

    TextureManager::TextureManager()
    {
    }

    TextureHandle TextureManager::CreateTexture(GraphicsContext context, const TextureData& data,  const TextureSettings& settings)
    {
        uint32_t id = CreateTextureSlot();
        if (context.type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            OpenglContext *ol_context = static_cast<OpenglContext *>(context.native);
            textures_[id].texture = std::make_unique<OpenglTexture>();
        }
        else if (context.type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            VulkanContext *vulkan_context = static_cast<VulkanContext *>(context.native);
            textures_[id].texture = std::make_unique<VulkanTexture>();
        }

        textures_[id].texture->Initialize(context, data, settings);

        return {id, textures_[id].generation};
    }

    TextureSlot &TextureManager::GetTextureSlot(TextureHandle handle)
    {
        if (handle.id >= textures_.size())
        {
            KP_LOG("TextureManagerLog", LOG_LEVEL_ERROR, "Failed to get texture, out of range");
            throw std::runtime_error("Failed to get texture, out of range");
        }

        return textures_[handle.id];
    }

    Texture *TextureManager::GetTexture(TextureHandle handle)
    {
        return GetTextureSlot(handle).texture.get();
    }

    bool TextureManager::DestroyTexture(GraphicsContext context, TextureHandle handle)
    {
        TextureSlot &slot = GetTextureSlot(handle);
        if (slot.texture == nullptr)
        {
            return false;
        }

        slot.texture->Destroy(context);

        free_slots_.push_back(handle.id);
        slot.generation++;
        return true;
    }

    uint32_t TextureManager::CreateTextureSlot()
    {
        uint32_t id{};
        if (!free_slots_.empty())
        {
            id = free_slots_.back();
            free_slots_.pop_back();
        }
        else
        {
            id = static_cast<uint32_t>(textures_.size());
            textures_.emplace_back();
        }
        return id;
    }

    TextureManager::~TextureManager() = default;
}