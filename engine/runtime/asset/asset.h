#ifndef KPENGINE_RUNTIME_ASSET_H
#define KPENGINE_RUNTIME_ASSET_H

#include <string>
#include <variant>
#include <vector>
#include <memory>

#include "common.h"
namespace kpengine::asset
{
    struct MeshResource;
    struct ModelResource;
    struct TextureResource;
    struct ShaderResource;
    struct ShaderMetaResource;

    using ModelPtr = std::shared_ptr<ModelResource>;
    using MeshPtr = std::shared_ptr<MeshResource>;
    using TexturePtr = std::shared_ptr<TextureResource>;
    using ShaderPtr = std::shared_ptr<ShaderResource>;
    using ShaderMetaPtr = std::shared_ptr<ShaderMetaResource>;

    using AssetResource = std::variant<ModelPtr, MeshPtr, TexturePtr, ShaderPtr, ShaderMetaPtr>;

    struct AssetRegisterInfo
    {
        AssetResource resource;
        std::string path;
        std::string name;
        std::vector<AssetID> ref_assets;
        std::vector<AssetID> dependencies;
        AssetType type;
        uint32_t flags = 0;
    };

    class Asset
    {
    public:
        AssetID GetID() const { return id; }
        AssetType GetType() const { return id.type; }
        std::string GetName() const { return name; }
        std::string GetPath() const { return abs_path; }
        std::vector<AssetID> GetRefs() const{return ref_assets;}
        std::vector<AssetID> GetDependencies() const{return dependencies;}
        template <typename T>
        std::shared_ptr<T> GetResource()
        {
            if (auto ptr = std::get_if<std::shared_ptr<T>>(&resource))
            {
                return *ptr;
            }
            return nullptr;
        }
        friend class AssetManager;

    private:
        AssetID id;
        std::string name;
        std::string abs_path;
        AssetResource resource;
        std::vector<AssetID> ref_assets;//used by
        std::vector<AssetID> dependencies;//uses
    };
}

#endif