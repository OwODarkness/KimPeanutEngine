#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_TEXTURE_H

#include <glad/glad.h>
#include "opengl_enum.h"
#include "common/texture.h"

namespace kpengine::graphics
{
    struct OpenglTextureResource{
        GLuint image = 0;

    };

    inline OpenglTextureResource ConvertToOpenglTextureResource(const TextureResource& handle)
    {
        OpenglTextureResource res{};
        res.image = static_cast<GLuint>(reinterpret_cast<uintptr_t>(handle.image));
        return res;
    }

    inline TextureResource ConvertToTextureResource(const OpenglTextureResource& handle)
    {
        TextureResource res{};
        res.image = reinterpret_cast<TextureImage>(static_cast<uintptr_t>(handle.image));
        return res;
    }

    class OpenglTexture : public Texture
    {
    public:
        void Initialize(GraphicsContext context, const TextureData& data, const TextureSettings& settings) override;
        void Destroy(GraphicsContext context)override;
        TextureResource GetTextueHandle() const override{return ConvertToTextureResource(resource_);}
    private:
        OpenglTextureResource resource_;
    };
}

#endif