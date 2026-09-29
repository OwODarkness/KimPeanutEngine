#include "tools/terrain_viewer_host.h"

#include <array>
#include <fstream>
#include <limits>
#include <utility>

#include "asset/asset.h"
#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "config/path.h"
#include "gameplay/factory/camera_actor_factory.h"
#include "gameplay/factory/directional_light_actor_factory.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/world/gameplay_world.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"
#include "editor/terrain_editor.h"
#include "evaluation/terrain_generation.h"

namespace kpengine::terrain
{
    namespace
    {
        bool BuildPreview(std::string &diagnostic, data::MeshData &mesh,
                          spatial::AABB &bounds,
                          std::shared_ptr<const ScalarField2D> &heightfield)
        {
#ifndef KPENGINE_TERRAIN_FIXTURE_DIR
#define KPENGINE_TERRAIN_FIXTURE_DIR ""
#endif
            const std::string path = std::string(KPENGINE_TERRAIN_FIXTURE_DIR) +
                                     "/mountain_basin.terrainrecipe.json";
            std::ifstream input(path);
            if (!input)
            {
                diagnostic = "could not open terrain preview fixture: " + path;
                return false;
            }

            nlohmann::json source;
            try
            {
                input >> source;
            }
            catch (const std::exception &error)
            {
                diagnostic = std::string("could not parse terrain preview fixture: ") +
                             error.what();
                return false;
            }
            TerrainRecipe recipe = TerrainRecipe::FromJson(source);
            constexpr double kFixtureScale = 16.0;
            recipe.domain.width = 129;
            recipe.domain.height = 129;
            recipe.domain.origin_x_m = -64.0;
            recipe.domain.origin_z_m = -64.0;
            recipe.domain.spacing_x_m = 1.0;
            recipe.domain.spacing_z_m = 1.0;
            // Upsample the compact fixture into a viewable finite preview domain.
            for (RecipeNode &node : recipe.nodes)
            {
                if (node.parameters.contains("width_m"))
                {
                    node.parameters["width_m"] =
                        node.parameters["width_m"].get<double>() * kFixtureScale;
                }
                if (node.parameters.contains("points_xz_m"))
                {
                    for (auto &point : node.parameters["points_xz_m"])
                    {
                        point[0] = point[0].get<double>() * kFixtureScale;
                        point[1] = point[1].get<double>() * kFixtureScale;
                    }
                }
            }

            auto registry = std::make_shared<OperatorRegistry>();
            if (!registry->RegisterBuiltins(diagnostic))
            {
                return false;
            }
            TerrainEvaluator evaluator(registry);
            const EvaluationResult result = evaluator.Evaluate(recipe);
            if (!result.succeeded)
            {
                diagnostic = result.diagnostic;
                return false;
            }
            const auto node = result.nodes.find("mountain_ring");
            if (node == result.nodes.end() || node->second.outputs.empty())
            {
                diagnostic = "terrain fixture did not produce a heightfield";
                return false;
            }
            heightfield = node->second.outputs.begin()->second;
            mesh = BuildHeightfieldMesh(*heightfield);
            if (mesh.vertices.empty() || mesh.indices.empty())
            {
                diagnostic = "terrain fixture produced an empty preview mesh";
                return false;
            }
            const float maximum = std::numeric_limits<float>::max();
            bounds = {{maximum, maximum, maximum}, {-maximum, -maximum, -maximum}};
            for (const data::Vertex &vertex : mesh.vertices)
            {
                bounds.ExpandToInclude(vertex.position);
            }
            return bounds.IsValid();
        }
    }

    TerrainViewerHost::TerrainViewerHost() = default;

    TerrainViewerHost::~TerrainViewerHost()
    {
        Shutdown();
    }

    bool TerrainViewerHost::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != runtime::ApplicationMode::TerrainViewer)
        {
            diagnostic = "TerrainViewerHost can only initialize in terrain-viewer mode";
            return false;
        }
        runtime::global_runtime_context.InitializeSceneServices();
        gameplay::GameplayWorld *const world =
            runtime::global_runtime_context.gameplay_world_.get();
        if (world == nullptr)
        {
            diagnostic = "Runtime did not create the preview GameplayWorld";
            return false;
        }
        const auto fail = [this, &diagnostic](std::string message)
        {
            diagnostic = std::move(message);
            Shutdown();
            return false;
        };

        data::MeshData mesh_data;
        spatial::AABB bounds;
        if (!BuildPreview(diagnostic, mesh_data, bounds, preview_heightfield_))
        {
            return false;
        }
        auto mesh = std::make_shared<asset::MeshResource>();
        mesh->data = std::make_shared<data::MeshData>(std::move(mesh_data));
        mesh->local_bounds = bounds;
        mesh->vertex_count = static_cast<uint32_t>(mesh->data->vertices.size());
        mesh->face_count = static_cast<uint32_t>(mesh->data->indices.size() / 3u);
        asset::AssetRegisterInfo mesh_info{};
        mesh_info.resource = mesh;
        mesh_info.path = "generated://terrain/mountain-basin-preview";
        mesh_info.name = "Mountain Basin Preview";
        mesh_info.type = asset::AssetType::KPAT_Mesh;
        generated_mesh_ = asset::AssetManager::GetInstance().RegisterAsset(mesh_info);
        if (!generated_mesh_.IsValid())
        {
            return fail("AssetManager rejected the generated terrain MeshData");
        }

        asset::AssetManager &assets = asset::AssetManager::GetInstance();
        const asset::AssetID error_material = assets.LoadSync(
            GetAssetDirectory() + asset::kEngineErrorMaterialAssetPath);
        asset::Asset *const error_wrapper = assets.GetAsset(error_material);
        const auto source_material = error_wrapper != nullptr
                                         ? error_wrapper->GetResource<asset::MaterialResource>()
                                         : nullptr;
        if (!source_material)
        {
            return fail("could not load the engine's built-in preview material");
        }
        auto preview_material = std::make_shared<asset::MaterialResource>(*source_material);
        for (asset::MaterialParameterSource &parameter : preview_material->parameters)
        {
            if (parameter.name == "base_color" &&
                parameter.type == asset::MaterialParameterSourceType::Vector4)
            {
                parameter.value = std::array<float, 4>{0.24f, 0.46f, 0.18f, 1.0f};
            }
        }
        asset::AssetRegisterInfo material_info{};
        material_info.resource = preview_material;
        material_info.path = "generated://terrain/mountain-basin-preview.material";
        material_info.name = "Terrain Preview Material";
        material_info.dependencies = error_wrapper->GetDependencies();
        material_info.type = asset::AssetType::KPAT_Material;
        preview_material_ = assets.RegisterAsset(material_info);
        if (!preview_material_.IsValid())
        {
            return fail("AssetManager rejected the generated terrain preview material");
        }
        render_roots_ = {generated_mesh_, preview_material_};

        gameplay::StaticMeshActorDesc terrain_desc{};
        terrain_desc.mesh_asset = generated_mesh_;
        terrain_desc.material_asset = preview_material_;
        terrain_desc.local_bounds = bounds;
        terrain_actor_ = gameplay::CreateStaticMeshActor(*world, terrain_desc);
        if (!terrain_actor_.IsValid())
        {
            return fail("could not create the terrain preview Actor and MeshComponent");
        }

        gameplay::CameraActorDesc camera_desc{};
        camera_desc.transform.position_ = {0.0f, 260.0f, 350.0f};
        camera_desc.transform.rotator_ = {-27.0f, -90.0f, 0.0f};
        camera_desc.far_plane = 2000.0f;
        camera_actor_ = gameplay::CreateCameraActor(*world, camera_desc);
        gameplay::DirectionalLightActorDesc light_desc{};
        light_desc.direction = {-0.4f, -1.0f, -0.3f};
        light_desc.intensity = 3.0f;
        light_actor_ = gameplay::CreateDirectionalLightActor(*world, light_desc);
        if (!camera_actor_.IsValid() || !light_actor_.IsValid())
        {
            return fail("could not create preview camera or directional light Actor");
        }
        initialized_ = true;
        return true;
    }

    bool TerrainViewerHost::InitializePresentation(runtime::Engine &engine,
                                                   std::string &diagnostic)
    {
        if (!initialized_ || !preview_heightfield_)
        {
            diagnostic = "Terrain preview data is unavailable for the editor";
            return false;
        }
        terrain_editor_ = std::make_unique<TerrainEditor>();
        if (!terrain_editor_->Initialize(engine, preview_heightfield_, diagnostic))
        {
            terrain_editor_.reset();
            return false;
        }
        return true;
    }

    bool TerrainViewerHost::Tick(float, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!initialized_)
        {
            diagnostic = "TerrainViewerHost is not initialized";
            return false;
        }
        return true;
    }

    bool TerrainViewerHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        if (!initialized_)
        {
            diagnostic = "TerrainViewerHost is not initialized";
            return false;
        }
        return true;
    }

    bool TerrainViewerHost::RenderPresentation(std::string &diagnostic)
    {
        if (terrain_editor_ == nullptr)
        {
            diagnostic.clear();
            return true;
        }
        return terrain_editor_->Render(diagnostic);
    }

    void TerrainViewerHost::ShutdownRenderThread() noexcept
    {
        if (terrain_editor_ != nullptr)
        {
            terrain_editor_->Shutdown();
            terrain_editor_.reset();
        }
    }

    void TerrainViewerHost::Shutdown() noexcept
    {
        initialized_ = false;
        ShutdownRenderThread();
        render_roots_.clear();
        if (runtime::global_runtime_context.gameplay_world_ != nullptr)
        {
            gameplay::GameplayWorld &world =
                *runtime::global_runtime_context.gameplay_world_;
            if (terrain_actor_.IsValid())
            {
                (void)world.DestroyActor(terrain_actor_);
            }
            if (camera_actor_.IsValid())
            {
                (void)world.DestroyActor(camera_actor_);
            }
            if (light_actor_.IsValid())
            {
                (void)world.DestroyActor(light_actor_);
            }
            world.ReclaimDestroyedActors();
        }
        terrain_actor_ = {};
        camera_actor_ = {};
        light_actor_ = {};
        if (preview_material_.IsValid())
        {
            asset::AssetManager::GetInstance().UnRegisterAsset(preview_material_);
            preview_material_ = {};
        }
        if (generated_mesh_.IsValid())
        {
            asset::AssetManager::GetInstance().UnRegisterAsset(generated_mesh_);
            generated_mesh_ = {};
        }
        preview_heightfield_.reset();
    }
}
