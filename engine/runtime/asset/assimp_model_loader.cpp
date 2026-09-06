#include "assimp_model_loader.h"

#include <magic_enum/magic_enum.hpp>
#include <utility>

#include "asset_manager.h"
#include "log/logger.h"
#include "mesh.h"
#include "model.h"
#include "utility.h"

namespace kpengine::asset
{
    Assimp_ModelLoader::Assimp_ModelLoader() = default;

    Assimp_ModelLoader::~Assimp_ModelLoader() = default;

    bool Assimp_ModelLoader::Load(const std::string &path, ModelGeometryType type,
                                  AssetRegisterInfo &info)
    {
        if (type != ModelGeometryType::KPMG_Mesh)
        {
            return false;
        }

        const AssetID id = LoadMesh(path);
        if (!id.IsValid())
        {
            return false;
        }

        auto model_ptr = std::get_if<ModelPtr>(&info.resource);
        std::shared_ptr<ModelResource> resource = model_ptr != nullptr ? *model_ptr : nullptr;
        if (!resource)
        {
            resource = std::make_shared<ModelResource>();
            info.resource = resource;
        }
        info.type = AssetType::KPAT_Model;
        info.path = path;
        info.name = std::string{magic_enum::enum_name(info.type)} + "_" +
                    ExtractNameFromPath(path);
        info.dependencies.push_back(id);
        resource->BindData(type, id);
        return true;
    }

    AssetID Assimp_ModelLoader::LoadMesh(const std::string &path)
    {
        ImportedModelDocument document;
        try
        {
            document = decoder_.Decode(std::filesystem::path{path});
        }
        catch (const ImportedModelDecodeError &error)
        {
            KP_LOG("ModelLoadLog", LOG_LEVEL_ERROR, "%s", error.what());
            return {};
        }

        auto mesh_asset = std::make_shared<MeshResource>();
        mesh_asset->data->vertices = std::move(document.mesh.vertices);
        mesh_asset->data->indices = std::move(document.mesh.indices);
        mesh_asset->data->sections = std::move(document.mesh.sections);
        mesh_asset->data->materials.reserve(document.materials.size());
        for (const ImportedMaterialSource &source : document.materials)
        {
            MeshMaterial destination;
            destination.name = source.name;
            destination.base_color = source.base_color;
            destination.metallic = source.metallic;
            destination.roughness = source.roughness;
            destination.emissive = source.emissive;
            destination.base_color_texture = source.base_color_texture;
            destination.normal_texture = source.normal_texture;
            destination.metallic_roughness_texture = source.metallic_roughness_texture;
            destination.occlusion_texture = source.occlusion_texture;
            destination.emissive_texture = source.emissive_texture;
            destination.double_sided = source.double_sided;
            destination.alpha_blended = source.alpha_mode == ImportedAlphaMode::Blend;
            mesh_asset->data->materials.push_back(std::move(destination));
        }
        mesh_asset->local_bounds = document.mesh.local_bounds;
        mesh_asset->face_count = document.mesh.face_count;
        mesh_asset->vertex_count = document.mesh.vertex_count;

        AssetRegisterInfo info{};
        info.resource = mesh_asset;
        info.path = path;
        info.type = AssetType::KPAT_Mesh;
        info.name = std::string{magic_enum::enum_name(info.type)} + "_" +
                    ExtractNameFromPath(path);
        return AssetManager::GetInstance().RegisterAsset(info);
    }
}
