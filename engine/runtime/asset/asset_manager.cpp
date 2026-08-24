#include "asset_manager.h"
#include <algorithm>
#include <cctype>
#include <magic_enum/magic_enum.hpp>
#include "assimp_model_loader.h"
#include "stb_image_loader.h"
#include "shader_program_loader.h"
#include "miniaudio_audio_loader.h"
#include "utility.h"
#include "model.h"
#include "log/logger.h"

namespace kpengine::asset
{

    AssetManager AssetManager::instance_;
    AssetManager::~AssetManager() = default;
    AssetManager::AssetManager() : model_loader_(std::make_unique<Assimp_ModelLoader>()),
                                   image_loader_(std::make_unique<Stb_ImageLoader>()),
                                   shader_program_loader_(std::make_unique<ShaderProgramLoader>()),
                                   audio_loader_(std::make_unique<MiniAudio_AudioLoader>())
    {
    }

    std::string AssetManager::Key(const std::string &path)
    {
        std::string key = path;
        std::replace(key.begin(), key.end(), '\\', '/');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); });
        return key;
    }

    AssetID AssetManager::LoadSync(const std::string &path)
    {
        std::string extension = GetFileExtension(path);

        if (extension.empty())
        {
            return AssetID();
        }

        AssetType type = ExtractAssetType(extension);
        if (type == AssetType::Undefined)
        {
            KP_LOG("AssetManagerLog", LOG_LEVEL_WARNING, "Unrecognize asset extension: %s ", extension.c_str());
            return AssetID();
        }

        // Already loaded? Re-checked after loading too, so two concurrent
        // requests for the same file don't both register.
        auto find_cached = [this](AssetType type, const std::string &path) -> AssetID
        {
            const AssetCache *cache = FindCache(type);
            if (!cache)
            {
                return AssetID();
            }
            auto it = cache->path_index.find(Key(path));
            if (it == cache->path_index.end())
            {
                return AssetID();
            }
            return GetAsset(it->second) ? it->second : AssetID();
        };

        {
            std::lock_guard<std::recursive_mutex> lock(state_mutex_);
            if (AssetID cached = find_cached(type, path); cached.IsValid())
            {
                return cached;
            }
        }

        // Disk I/O + parse under the loader lock: the loaders are shared instances.
        AssetRegisterInfo register_info{};
        {
            std::lock_guard<std::mutex> lock(load_mutex_);
            if (!LoadByExtension(path, type, register_info))
            {
                return AssetID();
            }
        }

        {
            std::lock_guard<std::recursive_mutex> lock(state_mutex_);
            if (AssetID cached = find_cached(type, path); cached.IsValid())
            {
                return cached; // another thread loaded it while we were reading
            }
            AssetID id = RegisterAsset(register_info);
            if (id.IsValid())
            {
                Cache(type).path_index[Key(GetAsset(id)->GetPath())] = id;
            }
            return id;
        }
    }

    std::future<AssetID> AssetManager::LoadAsync(const std::string &path)
    {
        // Same pipeline as LoadSync, offloaded to a worker thread. Loads serialize
        // on load_mutex_, so concurrent calls never race the shared loaders.
        // Note: destroying this future without get()/wait() blocks until the load
        // finishes (std::async semantics).
        return std::async(std::launch::async, [this, path]()
                          { return LoadSync(path); });
    }

    const AssetCache *AssetManager::FindCache(AssetType type) const
    {
        auto it = caches_.find(type);
        if (it == caches_.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    AssetCache *AssetManager::FindCache(AssetType type)
    {
        auto it = caches_.find(type);
        if (it == caches_.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    AssetCache &AssetManager::Cache(AssetType type)
    {
        return caches_[type];
    }

    AssetID AssetManager::RegisterAsset(AssetRegisterInfo &info)
    {
        if (!IsValidResource(info.resource))
        {
            return AssetID();
        }

        std::lock_guard<std::recursive_mutex> lock(state_mutex_);

        AssetType type = info.type;
        AssetCache &cache = Cache(type);
        AssetHandle handle = cache.handles.Create();

        if (handle.id == cache.assets.size())
        {
            cache.assets.emplace_back();
        }

        std::unique_ptr<Asset> asset = std::make_unique<Asset>();
        asset->resource = std::move(info.resource);
        asset->id.type = type;
        asset->id.id = handle.id;
        asset->id.generation = handle.generation;
        asset->abs_path = std::move(info.path);
        asset->name = std::move(info.name);
        asset->ref_assets = std::move(info.ref_assets);
        asset->dependencies = std::move(info.dependencies);

        AssetID id(handle.id, handle.generation, type);
        cache.assets[handle.id] = std::move(asset);
        AddReferences(id, cache.assets[handle.id]->dependencies);

        std::string type_name = std::string(magic_enum::enum_name(type));
        KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG, "Register Aseset [%s|%s|%llu] from %s successfully",
               type_name.c_str(),
               cache.assets[handle.id]->GetName().c_str(),
               id.Pack(),
               cache.assets[handle.id]->GetPath().c_str());
        return id;
    }

    Asset *AssetManager::GetAsset(const AssetID &id)
    {
        if (!id.IsValid())
        {
            return nullptr;
        }
        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        const AssetCache *cache = FindCache(id.type);
        if (!cache || id.id >= cache->assets.size())
        {
            return nullptr;
        }
        // Stale id (recycled slot) must resolve to null, never to the new occupant.
        if (!cache->handles.IsHandleValid(AssetHandle(id.id, id.generation)))
        {
            return nullptr;
        }
        return cache->assets[id.id].get();
    }

    void AssetManager::UnRegisterAsset(const AssetID &id)
    {
        if (!id.IsValid())
        {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        AssetCache *cache = FindCache(id.type);
        if (!cache || id.id >= cache->assets.size())
        {
            return;
        }
        // Stale id must not unregister the new occupant of a recycled slot.
        if (!cache->handles.IsHandleValid(AssetHandle(id.id, id.generation)))
        {
            return;
        }

        Asset *asset = cache->assets[id.id].get();
        if (!asset || !CanDelete(asset))
        {
            return;
        }

        RemoveReferences(asset->GetID(), asset->GetDependencies());

        cache->path_index.erase(Key(asset->GetPath()));

        KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG,
               "Unregister asset[%s, %llu] successfully",
               asset->GetName().c_str(), id.Pack());

        cache->assets[id.id].reset();
        cache->handles.Destroy(AssetHandle(id.id, id.generation));
    }

    bool AssetManager::CanDelete(const Asset *asset)
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
            for (const auto &ref_id : asset->GetRefs())
            {
                KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG,
                       "  Referenced by AssetID: %llu", ref_id.Pack());
            }

            return false;
        }

        return true;
    }

    void AssetManager::AddReferences(const AssetID &from, const std::vector<AssetID> &to_list)
    {
        std::lock_guard<std::recursive_mutex> lock(state_mutex_);

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
        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        for (const auto &to_id : to_list)
        {
            Asset *target_asset = GetAsset(to_id);
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
            return model_loader_->Load(path, ModelGeometryType::KPMG_Mesh, info);
        }
        else if (type == AssetType::KPAT_Texture)
        {
            assert(image_loader_);
            return image_loader_->Load(path, info);
        }
        else if (type == AssetType::KPAT_ShaderProgram)
        {
            assert(shader_program_loader_);
            return shader_program_loader_->Load(path, info);
        }
        else if (type == AssetType::KPAT_Audio)
        {
            assert(audio_loader_);
            return audio_loader_->LoadFromFile(path, info);
        }

        std::string name = std::string(magic_enum::enum_name(type));
        KP_LOG("AssetManagerLog", LOG_LEVEL_WARNING, "Failed to found suitable loader for Assettype: %s", name.c_str());
        return false;
    }

}