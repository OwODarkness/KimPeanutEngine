#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_TEXTURE_H

#include <glad/glad.h>
#include "opengl_enum.h"
#include "common/texture.h"

namespace kpengine::graphics
{
    struct OpenglTextureHandle{
        GLuint image = 0;
        GLuint sampler = 0;
    };

    inline OpenglTextureHandle ConvertToOpenglTextureHandle(const TextureHandle& handle)
    {
        OpenglTextureHandle res{};
        res.image = static_cast<GLuint>(reinterpret_cast<uintptr_t>(handle.image));
        res.sampler = static_cast<GLuint>(reinterpret_cast<uintptr_t>(handle.sampler));
        return res;
    }

    inline TextureHandle ConvertToTextureHandle(const OpenglTextureHandle& handle)
    {
        TextureHandle res{};
        res.image = reinterpret_cast<TextureImage>(static_cast<uintptr_t>(handle.image));
        res.sampler = reinterpret_cast<TextureImage>(static_cast<uintptr_t>(handle.sampler));
        return res;
    }

    class OpenglTexture : public Texture
    {
    public:
        void Initialize(const TextureDesc &desc) override;
        void Destroy() override;
        TextureHandle GetTextueHandle() const override{return ConvertToTextureHandle(handle_);}
    private:
        OpenglTextureHandle handle_;
    };
}

#endif