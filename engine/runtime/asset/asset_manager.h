#ifndef KPENGINE_RUNTIME_ASSET_MANAGER_H
#define KPENGINE_RUNTIME_ASSET_MANAGER_H

#include <memory>
#include <mutex>
#include <future>
#include <unordered_map>
#include "asset.h"
#include "base/handle.h"

namespace kpengine::asset{

    // Loaders are owned by the manager but only referenced here as unique_ptr;
    // their full definitions stay out of this header (see asset_manager.cpp).
    class IModelLoader;
    class ShaderProgramLoader;
    class IAudioLoader;
    class MaterialLoader;

    using AssetHandle = Handle<Asset>;

    struct AssetCache{
        HandleSystem<AssetHandle> handles;

        std::vector<std::unique_ptr<Asset>> assets;

        std::unordered_map<std::string, AssetID> path_index;
    };

    class AssetManager{
    public:
        static AssetManager& GetInstance(){return instance_;}
        // Defined in the .cpp so the unique_ptr<loader> deleters see complete types.
        ~AssetManager();
    public:
        AssetID LoadSync(const std::string& path);
        std::future<AssetID> LoadAsync(const std::string& path);

        AssetID RegisterAsset(AssetRegisterInfo& info);
        void UnRegisterAsset(const AssetID& id);

        Asset* GetAsset(const AssetID& id);

        //Get Resource From Asset(AssetData)
        template<typename T>
        std::shared_ptr<T> GetResource(const AssetID& id)
        {
            Asset* asset = GetInstance().GetAsset(id);
            if(!asset)
            {
                return nullptr;
            }
            return asset->GetResource<T>();
        }

        void AddReferences(const AssetID& from, const std::vector<AssetID>& to_list);
        void RemoveReferences(const AssetID& from, const std::vector<AssetID>& to_list);
    private:
        bool CanDelete(const Asset* asset);

        const AssetCache* FindCache(AssetType type) const;
        AssetCache* FindCache(AssetType type);
        AssetCache& Cache(AssetType type);

    private:
        AssetManager();
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;
        AssetManager(AssetManager&& ) = delete;
        AssetManager& operator=(AssetManager&& ) = delete;

        // Canonical path key for the path index: uniform separators + case-fold,
        // so lookup/insert/erase always agree on the same file.
        static std::string Key(const std::string& path);

        bool LoadByExtension(const std::string& path, AssetType type, AssetRegisterInfo& info);

    private:
        static AssetManager instance_;
        std::unique_ptr<IModelLoader> model_loader_;
        std::unique_ptr<ShaderProgramLoader> shader_program_loader_;
        std::unique_ptr<IAudioLoader> audio_loader_;
        std::unique_ptr<MaterialLoader> material_loader_;
        std::unordered_map<AssetType, AssetCache> caches_;

        std::recursive_mutex state_mutex_;  // guards caches_ and path_index
        std::mutex load_mutex_;             // serializes shared loader access
        
    };
}

#endif
