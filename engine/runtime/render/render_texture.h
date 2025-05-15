#ifndef KPENGINE_RUNTIME_RENDER_TEXTURE_H
#define KPENGINE_RUNTIME_RENDER_TEXTURE_H

#include <string>
#include <unordered_map>
#include <filesystem>
#include <array>

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
        enum class CubemapSlotName{
        Right,
        Left,
        Top,
        Bottom,
        Front,
        Back
    };

    
    class RenderTextureCubeMap: public RenderTexture
    {
    public:
        RenderTextureCubeMap(const std::string &image_directory,const std::unordered_map<CubemapSlotName, std::string>& slots);

        virtual bool Initialize() override;
    private:
        std::unordered_map<CubemapSlotName, std::string> cubemap_slots_;

    };


    class RenderTextureHDR: public RenderTexture
    {
    public:
        RenderTextureHDR(const std::string& image_directory);

        virtual bool Initialize() override;
        
    };
}

#endif