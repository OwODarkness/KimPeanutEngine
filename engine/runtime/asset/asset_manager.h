#ifndef KPENGINE_RUNTIME_ASSET_MANAGER_H
#define KPENGINE_RUNTIME_ASSET_MANAGER_H

#include <memory>
#include <mutex>
#include <future>
#include <optional>
#include <unordered_map>
#include "asset.h"
#include "asset_load_observation.h"
#include "asset_type_registry.h"
#include "base/handle.h"
#include "texture_variant_profile.h"

namespace kpengine::asset{

    // Loaders are owned by the manager but only referenced here as unique_ptr;
    // their full definitions stay out of this header (see asset_manager.cpp).
    class IModelLoader;
    class NativeModelLoader;
    class NativeTextureLoader;
    class ShaderProgramLoader;
    class IAudioLoader;
    class MaterialLoader;
    class LevelLoader;

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
        AssetLoadSession BeginLoadObservation();
        AssetID LoadSync(const std::string& path,
                         const AssetLoadSession &session);
        // Selects the one material texture product declared to Asset loaders
        // before startup dependency resolution. The default is portable.
        void SetTextureVariantProfile(TextureVariantProfile profile);
        std::future<AssetID> LoadAsync(const std::string& path);
        std::future<AssetID> LoadAsync(const std::string& path,
                                       AssetLoadSession session);

        AssetID RegisterAsset(AssetRegisterInfo& info);
        bool RegisterAssetType(AssetTypeDescriptor descriptor, std::string &diagnostic);
        void UnRegisterAsset(const AssetID& id);
        std::size_t GetLiveAssetCount(AssetType type);
        std::size_t GetTotalLiveAssetCount();

        Asset* GetAsset(const AssetID& id);

        // Resolve an ordered, still-live dependency without exposing the
        // owning Asset's storage to later runtime stages.
        AssetID ResolveDependency(const AssetID& owner, size_t dependency_index,
                                  AssetType expected_type);

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

        bool RegisterBuiltInAssetTypes(std::string &diagnostic);
        AssetType ResolveAssetType(std::string_view extension,
                                   std::string &diagnostic);
        AssetID RegisterAssetLocked(AssetRegisterInfo &info,
                                    std::vector<AssetID> owned_children);
        bool LoadByExtension(const std::string& path, AssetType type, AssetRegisterInfo& info);
        bool LoadBuiltInAsset(const std::string &path, AssetType type,
                              AssetRegisterInfo &info);
        bool ValidateRegistration(const AssetRegisterInfo &info) const;
        AssetID LoadSyncInternal(
            const std::string &path,
            const std::shared_ptr<detail::AssetLoadSessionState> &session_state,
            std::optional<AssetLoadOperationID> parent_operation,
            std::optional<AssetLoadOperationID> reserved_operation);

    private:
        static AssetManager instance_;
        std::unique_ptr<IModelLoader> model_loader_;
        std::unique_ptr<NativeModelLoader> native_model_loader_;
        std::unique_ptr<NativeTextureLoader> native_texture_loader_;
        std::unique_ptr<ShaderProgramLoader> shader_program_loader_;
        std::unique_ptr<IAudioLoader> audio_loader_;
        std::unique_ptr<MaterialLoader> material_loader_;
        std::unique_ptr<LevelLoader> level_loader_;
        AssetTypeRegistry type_registry_;
        std::unordered_map<AssetType, AssetCache> caches_;

        std::recursive_mutex state_mutex_;  // guards caches_ and path_index
        std::mutex load_mutex_;             // serializes shared loader access
        
    };
}

#endif
