#ifndef KPENGINE_RUNTIME_ASSET_MANAGER_H
#define KPENGINE_RUNTIME_ASSET_MANAGER_H

#include <memory>
#include <unordered_map>
#include "asset.h"
#include "base/handle.h"
#include "model_loader.h"
#include "image_loader.h"
#include "shader_meta_loader.h"
#define DEBUG

namespace kpengine::asset{

    using AssetHandle = Handle<Asset>;

    struct AssetCache{
        std::vector<std::shared_ptr<Asset>> assets;
        HandleSystem<AssetHandle> handles;
        std::unordered_map<std::string, std::weak_ptr<Asset>> path_map;
    };

    class AssetManager{
    public:
        static AssetManager& GetInstance(){return instance_;}
        AssetID LoadSync(const std::string& path);
        AssetID RegisterAsset(AssetRegisterInfo& info);
        std::shared_ptr<Asset> GetAsset(const AssetID& id);
        void UnRegisterAsset(const AssetID& id);
        bool CanDelete(const std::shared_ptr<Asset>& asset);

        //Get Resource From Asset(AssetData)
        template<typename T>
        std::shared_ptr<T> GetResource(const AssetID id)
        {
            std::shared_ptr<asset::Asset> asset = GetInstance().GetAsset(id);
            if(!asset)
            {
                return nullptr;
            }
            return asset->GetResource<T>();
        }
        void AddReferences(const AssetID& from, const std::vector<AssetID>& to_list);
        void RemoveReferences(const AssetID& from, const std::vector<AssetID>& to_list);
    private:
        AssetManager();
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;
        AssetManager(AssetManager&& ) = delete;
        AssetManager& operator=(AssetManager&& ) = delete;

        bool LoadByExtension(const std::string& path, AssetType type, AssetRegisterInfo& info);

    private:
        static AssetManager instance_;
        std::unique_ptr<IModelLoader> model_loader_;
        std::unique_ptr<ImageLoader> image_loader_;
        std::unique_ptr<ShaderMetaLoader> shader_meta_loader_;
        std::unordered_map<AssetType, AssetCache> caches_;
    };
}

#endif