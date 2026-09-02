#include "render_asset_preparer.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "config/path.h"
#include "log/logger.h"
#include "resource/resource_pipeline.h"

namespace kpengine::runtime
{
    namespace
    {
        bool IsTextureDataReady(const data::TextureData &data)
        {
            return data.width != 0 && data.height != 0 && !data.pixels.empty();
        }

        class PreparationTransaction final
        {
        public:
            PreparationTransaction(RenderAssetPreparationHooks hooks, GraphicsAPIType api_type)
                : hooks_(std::move(hooks)), api_type_(api_type)
            {
            }

            RenderAssetPreparationResult Run(asset::AssetID level_asset)
            {
                if (api_type_ != GraphicsAPIType::GRAPHICS_API_VULKAN &&
                    api_type_ != GraphicsAPIType::GRAPHICS_API_OPENGL)
                {
                    return Failure("render preparation received an unsupported graphics API");
                }
                if (!level_asset.IsValid() || level_asset.type != asset::AssetType::KPAT_Level)
                {
                    return Failure("startup level AssetID is invalid for render preparation");
                }
                if (!hooks_.load_sync || !hooks_.get_asset || !hooks_.process_shaders ||
                    !hooks_.process_environment_ibl || !hooks_.processed_shader_count)
                {
                    return Failure("render preparation services are incomplete");
                }
                if (!Visit(level_asset))
                {
                    return Failure(diagnostic_);
                }

                render::PreparedRenderAssetCatalogBuild build;
                build.graphics_api = api_type_;
                const auto &requirements = render::GetBuiltInRenderAssetRequirements();
                for (size_t index = 0; index < requirements.size(); ++index)
                {
                    const auto &requirement = requirements[index];
                    const asset::AssetID id = hooks_.load_sync(
                        GetAssetDirectory() + requirement.relative_path);
                    if (!id.IsValid() || id.type != requirement.expected_type || !Visit(id))
                    {
                        return Failure("failed to prepare Render built-in '" +
                                       std::string(requirement.relative_path) + "': " + diagnostic_);
                    }
                    build.built_ins[index] = id;
                }

                if (!ProcessShaders())
                {
                    return Failure(diagnostic_);
                }
                build.records = std::move(records_);
                build.prepared_shader_count = hooks_.processed_shader_count();

                if (!PrepareEnvironment(level_asset, build))
                {
                    return Failure(diagnostic_);
                }

                std::string catalog_diagnostic;
                auto catalog = render::PreparedRenderAssetCatalog::Create(
                    std::move(build), catalog_diagnostic);
                if (!catalog)
                {
                    return Failure(catalog_diagnostic);
                }
                return {std::make_shared<const render::PreparedRenderAssetCatalog>(
                            std::move(*catalog)), {}};
            }

        private:
            RenderAssetPreparationResult Failure(std::string diagnostic)
            {
                return {nullptr, std::move(diagnostic)};
            }

            bool Visit(asset::AssetID id)
            {
                if (!id.IsValid())
                {
                    diagnostic_ = "dependency AssetID is invalid";
                    return false;
                }
                if (!visited_.insert(id.Pack()).second)
                {
                    return true;
                }

                asset::Asset *const wrapper = hooks_.get_asset(id);
                if (wrapper == nullptr || !(wrapper->GetID() == id) || !wrapper->IsValid())
                {
                    diagnostic_ = "AssetID " + std::to_string(id.Pack()) +
                                  " is not loaded or has no CPU payload";
                    return false;
                }
                const std::vector<asset::AssetID> dependencies = wrapper->GetDependencies();

                if (id.type == asset::AssetType::KPAT_Level)
                {
                    if (!wrapper->GetResource<asset::LevelResource>())
                    {
                        diagnostic_ = "level AssetID has no LevelResource";
                        return false;
                    }
                }
                else if (id.type == asset::AssetType::KPAT_Model)
                {
                    const auto model = wrapper->GetResource<asset::ModelResource>();
                    if (!model || !model->GetMesh())
                    {
                        diagnostic_ = "model AssetID has no mesh payload";
                        return false;
                    }
                }
                else if (id.type == asset::AssetType::KPAT_Mesh)
                {
                    const auto payload = wrapper->GetResource<asset::MeshResource>();
                    if (!payload || !payload->data)
                    {
                        diagnostic_ = "mesh AssetID has no MeshData payload";
                        return false;
                    }
                }
                else if (id.type == asset::AssetType::KPAT_Texture)
                {
                    const auto texture = wrapper->GetResource<asset::TextureResource>();
                    if (!texture || !texture->data || !IsTextureDataReady(*texture->data))
                    {
                        diagnostic_ = "texture AssetID has no ready TextureData payload";
                        return false;
                    }
                }
                else if (id.type != asset::AssetType::KPAT_Shader &&
                         id.type != asset::AssetType::KPAT_ShaderProgram &&
                         id.type != asset::AssetType::KPAT_Material)
                {
                    diagnostic_ = "unsupported render dependency AssetID " +
                                  std::to_string(id.Pack());
                    return false;
                }

                if (id.type != asset::AssetType::KPAT_Level && id.type != asset::AssetType::KPAT_Model)
                {
                    render::PreparedRenderAssetRecord record;
                    record.id = id;
                    record.dependencies = dependencies;
                    switch (id.type)
                    {
                    case asset::AssetType::KPAT_Mesh:
                        record.payload = wrapper->GetResource<asset::MeshResource>();
                        break;
                    case asset::AssetType::KPAT_Texture:
                        record.payload = wrapper->GetResource<asset::TextureResource>();
                        break;
                    case asset::AssetType::KPAT_Shader:
                        record.payload = wrapper->GetResource<asset::ShaderResource>();
                        break;
                    case asset::AssetType::KPAT_ShaderProgram:
                        record.payload = wrapper->GetResource<asset::ShaderProgramResource>();
                        break;
                    case asset::AssetType::KPAT_Material:
                        record.payload = wrapper->GetResource<asset::MaterialResource>();
                        break;
                    default:
                        break;
                    }
                    records_.push_back(std::move(record));
                }

                for (const asset::AssetID dependency : dependencies)
                {
                    if (!Visit(dependency))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool ProcessShaders()
            {
                std::vector<asset::ShaderPtr> shaders;
                std::unordered_set<uint64_t> seen;
                for (const render::PreparedRenderAssetRecord &record : records_)
                {
                    if (record.id.type != asset::AssetType::KPAT_Shader)
                    {
                        continue;
                    }
                    const auto shader = std::get<asset::ShaderPtr>(record.payload);
                    if (shader && seen.insert(record.id.Pack()).second)
                    {
                        shaders.push_back(shader);
                    }
                }
                hooks_.process_shaders(shaders);
                for (const render::PreparedRenderAssetRecord &record : records_)
                {
                    if (record.id.type != asset::AssetType::KPAT_Shader)
                    {
                        continue;
                    }
                    const auto shader = std::get<asset::ShaderPtr>(record.payload);
                    if (!shader || shader->status != asset::ShaderStatus::Ready || !shader->data ||
                        shader->data->api != api_type_ ||
                        (api_type_ == GraphicsAPIType::GRAPHICS_API_VULKAN
                             ? shader->data->byte_code.empty()
                             : shader->data->source.empty()))
                    {
                        diagnostic_ = "shader AssetID " + std::to_string(record.id.Pack()) +
                                      " was not prepared for the selected graphics API";
                        return false;
                    }
                }
                return true;
            }

            bool PrepareEnvironment(asset::AssetID level_asset,
                                    render::PreparedRenderAssetCatalogBuild &build)
            {
                const asset::LevelResource *const level =
                    hooks_.get_asset(level_asset) != nullptr
                        ? hooks_.get_asset(level_asset)->GetResource<asset::LevelResource>().get()
                        : nullptr;
                if (level == nullptr || !level->environment.has_value())
                {
                    return true;
                }
                const asset::AssetID source = ResolveDependency(
                    level_asset, level->environment->texture.dependency_index,
                    asset::AssetType::KPAT_Texture);
                const auto texture = source.IsValid()
                                          ? hooks_.get_asset(source)->GetResource<asset::TextureResource>()
                                          : nullptr;
                if (!source.IsValid() || !texture || !texture->data ||
                    texture->data->format != TextureFormat::TEXTURE_FORMAT_RGBA16F)
                {
                    diagnostic_ = "startup environment texture is not a valid RGBA16F payload";
                    return false;
                }
                const auto processed = hooks_.process_environment_ibl(*texture->data);
                if (!processed.has_value())
                {
                    diagnostic_ = "startup environment IBL preparation failed";
                    return false;
                }
                build.environment_ibl.push_back({source, *processed});
                return true;
            }

            asset::AssetID ResolveDependency(asset::AssetID owner, size_t index,
                                              asset::AssetType expected_type) const
            {
                const asset::Asset *const wrapper = hooks_.get_asset(owner);
                if (wrapper == nullptr || index >= wrapper->GetDependencies().size())
                {
                    return {};
                }
                const asset::AssetID dependency = wrapper->GetDependencies()[index];
                return dependency.type == expected_type && hooks_.get_asset(dependency) != nullptr
                           ? dependency
                           : asset::AssetID{};
            }

            RenderAssetPreparationHooks hooks_;
            GraphicsAPIType api_type_;
            std::vector<render::PreparedRenderAssetRecord> records_;
            std::unordered_set<uint64_t> visited_;
            std::string diagnostic_;
        };
    }

    RenderAssetPreparationResult RenderAssetPreparer::Prepare(asset::AssetID level_asset,
                                                               GraphicsAPIType api_type) const
    {
        try
        {
            RenderAssetPreparationHooks hooks = hooks_;
            resource::ResourcePipeline pipeline;
            if (!hooks.load_sync || !hooks.get_asset || !hooks.process_shaders ||
                !hooks.process_environment_ibl || !hooks.processed_shader_count)
            {
                resource::ResourcePipelineContext context{};
                context.graphics_type = api_type;
                pipeline.Initialize(context);
                asset::AssetManager &assets = asset::AssetManager::GetInstance();
                hooks.load_sync = [&assets](const std::string &path) { return assets.LoadSync(path); };
                hooks.get_asset = [&assets](asset::AssetID id) { return assets.GetAsset(id); };
                hooks.process_shaders = [&pipeline](const std::vector<asset::ShaderPtr> &shaders)
                { pipeline.ProcessShader(shaders); };
                hooks.process_environment_ibl = [&pipeline](const data::TextureData &source)
                { return pipeline.ProcessEnvironmentIbl(source); };
                hooks.processed_shader_count = [&pipeline]()
                { return pipeline.GetProcessedShaderCount(); };
            }
            PreparationTransaction transaction(std::move(hooks), api_type);
            return transaction.Run(level_asset);
        }
        catch (const std::exception &error)
        {
            return {nullptr, std::string("render asset preparation threw: ") + error.what()};
        }
        catch (...)
        {
            return {nullptr, "render asset preparation threw an unknown exception"};
        }
    }
}
