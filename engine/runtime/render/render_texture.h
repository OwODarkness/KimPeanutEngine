#ifndef KPENGINE_RUNTIME_RENDER_TEXTURE_H
#define KPENGINE_RUNTIME_RENDER_TEXTURE_H

#include <string>
#include <filesystem>
namespace kpengine
{

    class RenderTexture
    {
    public:
        /*  pass the relative path under engine/asset
        /   for example, a texture resource's path is engine/asset/texture/bunny/bunny_normal.jpg
        /   send texture/bunny/bunny_normal.jpg
        */  
        RenderTexture(const std::string &image_relative_path);

        virtual bool Initialize() = 0;

        inline unsigned int GetTexture() const { return texture_handle_; }

    protected:
        unsigned int texture_handle_;

    public:
        std::string image_id_;
    };
    //texture2d
    class RenderTexture2D: public RenderTexture
    {
    public:
        RenderTexture2D(const std::string &image_path);

        virtual bool Initialize() override;
    };
    //cubemap type
    class RenderTextureCubeMap: public RenderTexture
    {
    public:
        RenderTextureCubeMap(const std::string &image_directory, const std::vector<std::string>& face_names);

        virtual bool Initialize() override;
    private:
        std::vector<std::string> face_names_;
    };

}

#endif