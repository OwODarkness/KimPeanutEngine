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
    struct ShaderProgramResource;
    struct AudioResource;

    using ModelPtr = std::shared_ptr<ModelResource>;
    using MeshPtr = std::shared_ptr<MeshResource>;
    using TexturePtr = std::shared_ptr<TextureResource>;
    using ShaderPtr = std::shared_ptr<ShaderResource>;
    using ShaderProgramPtr = std::shared_ptr<ShaderProgramResource>;
    using AudioPtr = std::shared_ptr<AudioResource>;
    using AssetPayload = std::variant<ModelPtr, MeshPtr, TexturePtr, AudioPtr, ShaderPtr, ShaderProgramPtr>;

    inline bool IsValidResource(const AssetPayload &resource)
    {
        return std::visit([](auto &&ptr)
                          { return ptr != nullptr; }, resource);
    }

    struct AssetRegisterInfo
    {
        AssetPayload resource;
        std::string path;
        std::string name;
        std::vector<AssetID> ref_assets;
        std::vector<AssetID> dependencies;
        AssetType type;
        uint32_t flags = 0;
    };

    /**
     * AssetWrapper
     */
    class Asset
    {
    public:
        AssetID GetID() const { return id; }
        AssetType GetType() const { return id.type; }
        std::string GetName() const { return name; }
        std::string GetPath() const { return abs_path; }
        std::vector<AssetID> GetRefs() const{return ref_assets;}
        std::vector<AssetID> GetDependencies() const{return dependencies;}
        bool IsValid() const { return IsValidResource(resource); }
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
        AssetPayload resource;
        std::vector<AssetID> ref_assets;//used by
        std::vector<AssetID> dependencies;//uses
    };
}

#endif