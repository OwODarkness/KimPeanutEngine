#include "native_model_loader.h"

#include <fstream>
#include <magic_enum/magic_enum.hpp>
#include <stdexcept>
#include <utility>

#include "log/logger.h"
#include "mesh.h"
#include "model.h"
#include "native_model.h"
#include "asset_product.h"
#include "utility.h"

namespace kpengine::asset
{
    NativeModelLoader::NativeModelLoader(std::filesystem::path product_root)
        : product_root_(std::move(product_root))
    {
    }

    namespace
    {
        std::vector<std::byte> ReadProduct(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                throw NativeModelError(NativeModelErrorCode::IoError,
                                       "failed to open native model product");
            }
            const std::streampos end = file.tellg();
            if (end < 0 || static_cast<std::uintmax_t>(end) > kNativeModelMaxBytes)
            {
                throw NativeModelError(NativeModelErrorCode::Overflow,
                                       "native model product size is invalid");
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(end));
            file.seekg(0, std::ios::beg);
            if (!bytes.empty())
            {
                file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!file)
                {
                    throw NativeModelError(NativeModelErrorCode::IoError,
                                           "failed to read native model product");
                }
            }
            return bytes;
        }
    }

    bool NativeModelLoader::Load(const std::string &path, ModelGeometryType type,
                                 AssetRegisterInfo &info)
    {
        if (type != ModelGeometryType::KPMG_Mesh)
        {
            return false;
        }

        NativeModelProduct product;
        try
        {
            const std::filesystem::path product_path{path};
            const std::vector<std::byte> bytes = ReadProduct(product_path);
            std::string diagnostic;
            if (!VerifyArchiveProduct(product_path, ArchiveProductType::Model, bytes, diagnostic,
                                      product_root_))
            {
                throw NativeModelError(NativeModelErrorCode::IntegrityMismatch,
                                       "invalid native model archive product: " + diagnostic);
            }
            product = DeserializeNativeModel(bytes);
        }
        catch (const NativeModelError &error)
        {
            KP_LOG("NativeModelLoadLog", LOG_LEVEL_ERROR, "%s: %s", path.c_str(), error.what());
            return false;
        }

        auto mesh_asset = std::make_shared<MeshResource>();
        mesh_asset->data->vertices = std::move(product.data.vertices);
        mesh_asset->data->indices = std::move(product.data.indices);
        mesh_asset->data->sections = std::move(product.data.sections);
        mesh_asset->local_bounds = product.data.local_bounds;
        mesh_asset->face_count = static_cast<std::uint32_t>(mesh_asset->data->indices.size() / 3);
        mesh_asset->vertex_count = static_cast<std::uint32_t>(mesh_asset->data->vertices.size());

        auto model = std::make_shared<ModelResource>();
        info.owned_children.push_back({
            mesh_asset,
            path + "#mesh",
            std::string{magic_enum::enum_name(AssetType::KPAT_Mesh)} + "_" +
                ExtractNameFromPath(path),
            {},
            AssetType::KPAT_Mesh});
        const std::shared_ptr<ModelResource> model_resource = model;
        info.bind_owned_children = [model_resource](const std::vector<AssetID> &children)
        {
            if (children.size() != 1 || !children.front().IsValid() ||
                children.front().type != AssetType::KPAT_Mesh)
            {
                throw std::runtime_error("native model owned Mesh registration failed");
            }
            model_resource->BindData(ModelGeometryType::KPMG_Mesh, children.front());
        };
        std::vector<std::uint32_t> material_dependency_indices;
        material_dependency_indices.reserve(product.data.material_references.size());

        const std::filesystem::path product_root = std::filesystem::path{path}.parent_path().parent_path();
        for (const NativeModelMaterialReference &reference : product.data.material_references)
        {
            if (reference.asset_type != AssetType::KPAT_Material)
            {
                return false;
            }
            const std::filesystem::path material_path =
                product_root / ProductRelativePath(ArchiveProductType::Material,
                                                   reference.content_hash);
            material_dependency_indices.push_back(
                static_cast<std::uint32_t>(info.owned_children.size() + info.dependencies.size() +
                                           info.dependency_requests.size()));
            info.dependency_requests.push_back({material_path.string(), AssetType::KPAT_Material});
        }
        model->BindMaterialDependencyIndices(std::move(material_dependency_indices));

        info.resource = std::move(model);
        info.type = AssetType::KPAT_Model;
        info.path = path;
        info.name = std::string{magic_enum::enum_name(info.type)} + "_" +
                    ExtractNameFromPath(path);
        return true;
    }
}
