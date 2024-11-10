#ifndef KPENGINE_RUNTIME_RENDER_TEXTURE_H
#define KPENGINE_RUNTIME_RENDER_TEXTURE_H

#include <string>
#include <filesystem>
namespace kpengine
{

    class RenderTexture
    {
    public:
        RenderTexture(const std::string &image_path);

        virtual void Initialize() = 0;

        inline unsigned int GetTexture() const { return texture_handle_; }

    protected:
        unsigned int texture_handle_;

    public:
        std::string image_path_;
    };

    class RenderTexture2D: public RenderTexture
    {
    public:
        RenderTexture2D(const std::string &image_path);

        virtual void Initialize() override;
    };

    class RenderTextureCubeMap: public RenderTexture
    {
    public:
        RenderTextureCubeMap(const std::string &image_path);

        virtual void Initialize() override;
    };

}

#endif