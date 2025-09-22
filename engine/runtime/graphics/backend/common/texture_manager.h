#ifndef KPENGINE_RUNTIME_GRAPHICS_TEXTURE_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_TEXTURE_MANAGER_H

#include <memory>
#include <vector>
#include <string>
#include "api.h"
#include "texture.h"
#include "graphics_context.h"

namespace kpengine::graphics{
struct TextureSlot{
    std::unique_ptr<Texture> texture;
    uint32_t generation = 0;
};
class TextureManager{
public:
    TextureManager();
    ~TextureManager();

    TextureHandle CreateTexture(GraphicsContext context, const TextureData& data,  const TextureSettings& settings);
    Texture* GetTexture(TextureHandle handle);
    bool DestroyTexture(GraphicsContext context, TextureHandle handle);
private:
    TextureSlot& GetTextureSlot(TextureHandle handle);
    uint32_t CreateTextureSlot();
private:
    std::vector<TextureSlot> textures_;
    std::vector<uint32_t> free_slots_;
};

};

#endif