#include "asset_manager.h"
#include <magic_enum/magic_enum.hpp>
#include "assimp_model_loader.h"
#include "stb_image_loader.h"
#include "utility.h"
#include "model.h"
#include "log/logger.h"
namespace kpengine::asset
{

    AssetManager AssetManager::instance_;
    AssetManager::AssetManager() : 
    model_loader_(std::make_unique<AssimpModelLoader>()),
    image_loader_(std::make_unique<StbImageLoader>()),
    shader_meta_loader_(std::make_unique<ShaderMetaLoader>())
    {
    }

    AssetID AssetManager::LoadSync(const std::string &path)
    {

        std::string extension = GetFileExtension(path);

        if (extension.empty())
        {
            return AssetID();
        }

        AssetType type = ExtractAssetType(extension);
        if(type == AssetType::Undefined)
        {
            KP_LOG("AssetManagerLog", LOG_LEVEL_WARNNING, "Unrecognize asset extension: %s ", extension.c_str());
            return AssetID();
        }

        auto cache_it = caches_.find(type);
        if (cache_it != caches_.end())
        {
            auto &path_map = cache_it->second.path_map;
            auto asset_it = path_map.find(path);

            if (asset_it != path_map.end())
            {
                auto asset = asset_it->second.lock();
                if (asset)
                {
                    return asset->GetID();
                }
            }
        }

        AssetRegisterInfo register_info{};
        bool bsucceed = LoadByExtension(path, type, register_info);
        if (bsucceed)
        {
            return RegisterAsset(register_info);
        }

        return AssetID();
    }

    AssetID AssetManager::RegisterAsset(AssetRegisterInfo &info)
    {
        bool valid = std::visit([](auto &&ptr)
                                { return ptr != nullptr; }, info.resource);

        if (!valid)
        {
            return AssetID();
        }

        AssetType type = info.type;

        AssetHandle handle = caches_[type].handles.Create();

        if (handle.id == caches_[type].assets.size())
        {
            caches_[type].assets.emplace_back();
        }

        std::shared_ptr<Asset> asset = std::make_shared<Asset>();
        asset->resource = std::move(info.resource);
        asset->id.type = type;
        asset->id.id = handle.id;
        asset->id.generation = handle.generation;
        asset->abs_path = info.path;
        asset->name = info.name;
        asset->ref_assets = info.ref_assets;
        asset->dependencies = info.dependencies;
        caches_[type].assets[handle.id] = asset;



        AssetID id = AssetID(handle.id, handle.generation, type);
        std::string type_name = std::string(magic_enum::enum_name(type));
        KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG, "Register Aseset [%s|%s|%llu] from %s successfully", 
            type_name.c_str(),
            asset->GetName().c_str(), 
            id.Pack(),
            asset->GetPath().c_str()
        );
        AddReferences(id, asset->dependencies);
        return id;
    }

    std::shared_ptr<Asset> AssetManager::GetAsset(const AssetID &id)
    {
        if (!id.IsValid())
        {
            return nullptr;
        }
        AssetType type = id.type;
        uint32_t index = id.id;
        auto cache_it = caches_.find(type);
        if (cache_it != caches_.end())
        {
            auto &assets = cache_it->second.assets;
            if (assets.size() <= index)
            {
                return nullptr;
            }
            return assets[index];
        }
        return nullptr;
    }

    void AssetManager::UnRegisterAsset(const AssetID &id)
    {
        AssetHandle handle(id.id, id.generation);
        AssetType type = id.type;
        auto cache_it = caches_.find(type);
        if (cache_it != caches_.end())
        {
            auto &cache = cache_it->second;
            auto &handle_sys = cache.handles;
            auto &assets = cache.assets;

            auto asset = assets[handle.id];
            if (!asset)
                return;
            CanDelete(asset);
            RemoveReferences(asset->GetID(), asset->GetDependencies());

            cache.path_map.erase(asset->GetPath());

            KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG,
                "Unregister asset[%s, %llu] successfully",
                asset->GetName().c_str(), id.Pack());

            assets[handle.id].reset();

            handle_sys.Destroy(handle);
        }
    }

    bool AssetManager::CanDelete(const std::shared_ptr<Asset>& asset)
    {
        if (!asset)
            return true;

        if (!asset->GetRefs().empty())
        {
            KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG,
                "Asset [%s, %llu] is still referenced by %zu assets",
                asset->GetName().c_str(),
                asset->GetID().Pack(),
                asset->GetRefs().size());

            // Print who references it (very useful for debugging)
            for (const auto& ref_id : asset->GetRefs())
            {
                KP_LOG("AssetManagerLog",  LOG_LEVEL_DEBUG,
                    "  Referenced by AssetID: %llu", ref_id.Pack());
            }

            return false;
        }

        return true;
    }

    void AssetManager::AddReferences(const AssetID &from, const std::vector<AssetID> &to_list)
    {

        for (const auto &to : to_list)
        {
            auto asset = GetAsset(to);
            if (!asset)
                continue;

            if (std::find(asset->ref_assets.begin(), asset->ref_assets.end(), from) == asset->ref_assets.end())
            {
                asset->ref_assets.push_back(from);
#ifdef DEBUG
                KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG, "asset[%s] ref %llu", asset->GetName().c_str(), from.Pack());
#endif

            }
        }
    }
    void AssetManager::RemoveReferences(const AssetID &from, const std::vector<AssetID> &to_list)
    {
        for (const auto &to_id : to_list)
        {
            std::shared_ptr<Asset> target_asset = GetAsset(to_id);
            if (!target_asset)
            {
                continue;
            }

            auto &refs = target_asset->ref_assets;

            refs.erase(
                std::remove_if(refs.begin(), refs.end(), [&from](const AssetID &id)
                               { return id == from; }),
                refs.end());
#ifdef DEBUG
                KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG, "asset[%s, %llu] unref %llu", target_asset->GetName().c_str(), target_asset->GetID().Pack(), from.Pack());
#endif
        }
    }

    bool AssetManager::LoadByExtension(const std::string &path, AssetType type, AssetRegisterInfo &info)
    {
        if (type == AssetType::KPAT_Model)
        {
            assert(model_loader_);
           return  model_loader_->Load(path, ModelGeometryType::KPMG_Mesh, info);
            
        }
        else if(type == AssetType::KPAT_Texture)
        {
            assert(image_loader_);
            return image_loader_->Load(path, info);
        }
        else if(type == AssetType::KPAT_ShaderMeta)
        {
            assert(shader_meta_loader_);
            return shader_meta_loader_->Load(path, info);
        }
        std::string name = std::string(magic_enum::enum_name(type));
        KP_LOG("AssetManagerLog", LOG_LEVEL_WARNNING, "Failed to found suitable loader for Assettype: %s", name.c_str());
        return false;
    }

}