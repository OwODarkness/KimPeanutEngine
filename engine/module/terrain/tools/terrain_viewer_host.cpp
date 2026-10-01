#include "tools/terrain_viewer_host.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <utility>

#include "asset/asset.h"
#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/texture.h"
#include "config/path.h"
#include "data/texture_mipmap.h"
#include "gameplay/factory/camera_actor_factory.h"
#include "gameplay/factory/directional_light_actor_factory.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/world/gameplay_world.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"
#include "editor/terrain_editor.h"
#include "evaluation/terrain_generation.h"
#include "import/terrain_baker.h"
#include "import/terrain_material_settings.h"

namespace kpengine::terrain
{
    bool TerrainViewerHost::PreviewPublication::Begin(
        std::unique_ptr<PreviewAssets> &&preview, const std::uint64_t update_serial)
    {
        if (!preview || update_serial == 0 || IsPending()) return false;
        assets = std::move(preview);
        serial = update_serial;
        diagnostic.clear();
        state = State::Pending;
        return true;
    }

    bool TerrainViewerHost::PreviewPublication::MarkApplied(
        const std::uint64_t update_serial)
    {
        if (!IsPending() || serial != update_serial) return false;
        state = State::Applied;
        return true;
    }

    bool TerrainViewerHost::PreviewPublication::MarkFailed(
        const std::uint64_t update_serial, std::string reason)
    {
        if (!IsPending() || serial != update_serial) return false;
        diagnostic = std::move(reason);
        state = State::Failed;
        return true;
    }

    std::unique_ptr<TerrainViewerHost::PreviewAssets>
    TerrainViewerHost::PreviewPublication::Retire()
    {
        state = State::Retired;
        serial = 0;
        return std::move(assets);
    }

    void TerrainViewerHost::PreviewPublication::FinishApplied()
    {
        if (state == State::Applied)
        {
            state = State::Empty;
            serial = 0;
            diagnostic.clear();
            assets.reset();
        }
    }

    namespace
    {
        std::vector<std::uint8_t> BuildPreviewRiverMask(const ScalarField2D &heightfield)
        {
            const GridDomain2D &domain = heightfield.Domain();
            const std::size_t sample_count = heightfield.Samples().size();
            const DrainageNetwork drainage = RouteDrainage(
                heightfield, DrainageOutletPolicy::Perimeter);
            const double river_threshold = std::max(128.0,
                static_cast<double>(sample_count) * 0.01);
            std::vector<std::uint8_t> centerline(sample_count, 0);
            for (std::size_t index = 0; index < sample_count; ++index)
                if (heightfield.Samples()[index] > 0.5f &&
                    drainage.accumulation_cells[index] >= river_threshold)
                    centerline[index] = 1;

            std::vector<std::uint8_t> river_mask = centerline;
            for (std::uint32_t y = 0; y < domain.height; ++y)
                for (std::uint32_t x = 0; x < domain.width; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * domain.width + x;
                    if (!centerline[index]) continue;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (dx != 0 && dy != 0) continue;
                            const int nx = static_cast<int>(x) + dx;
                            const int ny = static_cast<int>(y) + dy;
                            if (nx < 0 || ny < 0 || nx >= static_cast<int>(domain.width) ||
                                ny >= static_cast<int>(domain.height))
                                continue;
                            const std::size_t neighbor = static_cast<std::size_t>(ny) * domain.width +
                                static_cast<std::uint32_t>(nx);
                            if (heightfield.Samples()[neighbor] > 0.5f)
                                river_mask[neighbor] = 1;
                        }
                }
            return river_mask;
        }

        std::uint8_t LinearToSrgbByte(const float linear)
        {
            const float srgb = linear <= 0.0031308f
                ? 12.92f * linear
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::lround(
                std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
        }

        bool RegisterTerrainSkyDome(const asset::AssetID environment_texture,
            const std::string &environment_path, asset::AssetID &mesh_id,
            asset::AssetID &material_id, spatial::AABB &bounds,
            std::string &diagnostic)
        {
            using namespace asset;
            constexpr std::uint32_t slices = 96;
            constexpr std::uint32_t stacks = 48;
            constexpr float radius = 1500.0f;
            const std::string shader_path =
                (std::filesystem::path{GetAssetDirectory()} /
                 "shader/pbr_gbuffer.shader").generic_string();
            AssetManager &assets = AssetManager::GetInstance();
            const AssetID shader = assets.LoadSync(shader_path);
            if (!environment_texture.IsValid() || !shader.IsValid())
            {
                diagnostic = "terrain sky dome requires its HDR texture and PBR shader";
                return false;
            }

            auto mesh = std::make_shared<MeshResource>();
            mesh->data = std::make_shared<data::MeshData>();
            mesh->data->vertices.reserve(static_cast<std::size_t>(slices + 1) *
                                         (stacks + 1));
            mesh->data->indices.reserve(static_cast<std::size_t>(slices) * stacks * 6);
            const float two_pi = 2.0f * std::numbers::pi_v<float>;
            for (std::uint32_t y = 0; y <= stacks; ++y)
            {
                const float v = static_cast<float>(y) / stacks;
                const float theta = v * std::numbers::pi_v<float>;
                const float ring = std::sin(theta);
                for (std::uint32_t x = 0; x <= slices; ++x)
                {
                    const float u = static_cast<float>(x) / slices;
                    const float phi = u * two_pi;
                    const Vector3f direction{ring * std::cos(phi), std::cos(theta),
                                             ring * std::sin(phi)};
                    data::Vertex vertex{};
                    vertex.position = direction * radius;
                    vertex.normal = direction * -1.0f;
                    vertex.tex_coord = {u, v};
                    mesh->data->vertices.push_back(vertex);
                }
            }
            for (std::uint32_t y = 0; y < stacks; ++y)
                for (std::uint32_t x = 0; x < slices; ++x)
                {
                    const std::uint32_t upper_left = y * (slices + 1) + x;
                    const std::uint32_t lower_left = upper_left + slices + 1;
                    const std::uint32_t upper_right = upper_left + 1;
                    const std::uint32_t lower_right = lower_left + 1;
                    mesh->data->indices.insert(mesh->data->indices.end(),
                        {upper_left, lower_left, upper_right,
                         upper_right, lower_left, lower_right});
                }
            const float limit = radius;
            bounds = {{-limit, -limit, -limit}, {limit, limit, limit}};
            mesh->data->sections.push_back({0,
                static_cast<std::uint32_t>(mesh->data->indices.size()), 0, bounds});
            mesh->local_bounds = bounds;
            mesh->vertex_count = static_cast<std::uint32_t>(mesh->data->vertices.size());
            mesh->face_count = static_cast<std::uint32_t>(mesh->data->indices.size() / 3);

            AssetRegisterInfo mesh_info{};
            mesh_info.resource = mesh;
            mesh_info.path = "generated://terrain/sky-dome-mesh";
            mesh_info.name = "Terrain Viewer HDR Sky Dome";
            mesh_info.type = AssetType::KPAT_Mesh;
            mesh_id = assets.RegisterAsset(mesh_info);
            if (!mesh_id.IsValid())
            {
                diagnostic = "AssetManager rejected the terrain HDR sky-dome mesh";
                return false;
            }

            auto material = std::make_shared<MaterialResource>();
            material->shader_path = shader_path;
            material->shader_dependency_index = 0;
            material->surface.shading_model = MaterialShadingModel::StandardPbr;
            material->surface.cull_mode = MaterialCullMode::None;
            material->surface.double_sided = true;
            material->parameters = {
                {"base_color", MaterialParameterSourceType::Vector4,
                 std::array<float, 4>{0.7f, 0.7f, 0.7f, 1.0f}},
                {"metallic", MaterialParameterSourceType::Scalar, 0.0f},
                {"roughness", MaterialParameterSourceType::Scalar, 1.0f},
                {"occlusion", MaterialParameterSourceType::Scalar, 1.0f},
                {"normal_scale", MaterialParameterSourceType::Scalar, 1.0f}};
            MaterialParameterSource panorama{};
            panorama.name = "base_color_texture";
            panorama.type = MaterialParameterSourceType::Texture;
            panorama.value = environment_path;
            panorama.texture_color_space = MaterialTextureColorSpace::Linear;
            panorama.dependency_index = 1;
            material->parameters.push_back(std::move(panorama));

            AssetRegisterInfo material_info{};
            material_info.resource = std::move(material);
            material_info.path = "generated://terrain/sky-dome-material";
            material_info.name = "Terrain Viewer HDR Sky Dome";
            material_info.dependencies = {shader, environment_texture};
            material_info.type = AssetType::KPAT_Material;
            material_id = assets.RegisterAsset(material_info);
            if (!material_id.IsValid())
            {
                assets.UnRegisterAsset(mesh_id);
                mesh_id = {};
                diagnostic = "AssetManager rejected the terrain HDR sky-dome material";
                return false;
            }
            diagnostic.clear();
            return true;
        }

        bool RegisterTerrainPreviewMaterial(const data::MeshData &mesh,
                                            const ScalarField2D &heightfield,
                                            std::vector<asset::AssetID> &material_ids,
                                            std::vector<asset::AssetID> &texture_ids,
                                            std::string &diagnostic)
        {
            using namespace asset;
            const GridDomain2D &domain = heightfield.Domain();
            const std::uint32_t width = domain.width;
            const std::uint32_t height = domain.height;
            static std::atomic<std::uint64_t> revision{0};
            AssetManager &assets = AssetManager::GetInstance();
            if (width < 2 || height < 2 ||
                static_cast<std::uint64_t>(width) * height > mesh.vertices.size())
            {
                diagnostic = "terrain preview needs a complete top-surface vertex grid";
                return false;
            }
            const std::string shader_path =
                (std::filesystem::path{GetAssetDirectory()} / "shader/pbr_gbuffer.shader").generic_string();
            const AssetID shader = assets.LoadSync(shader_path);
            if (!shader.IsValid())
            {
                diagnostic = "could not load the standard PBR shader for the height-only preview";
                return false;
            }

            constexpr std::array<float, 3> dirt_color{0.24f, 0.12f, 0.075f};
            constexpr std::array<float, 3> grass_color{0.11f, 0.22f, 0.04f};
            constexpr std::array<float, 3> river_color{0.0284f, 0.2159f, 0.5271f};
            constexpr std::array<float, 3> snow_color{0.8070f, 0.8550f, 0.8880f};
            constexpr float snow_start = 0.94f;
            constexpr float snow_full = 0.99f;
            const std::vector<std::uint8_t> river_mask = BuildPreviewRiverMask(heightfield);
            const auto [minimum_height, maximum_height] = std::minmax_element(
                heightfield.Samples().begin(), heightfield.Samples().end());
            const float height_range = *maximum_height - *minimum_height;
            auto texture = std::make_shared<TextureResource>();
            texture->channel_count = 4;
            texture->data->width = width;
            texture->data->height = height;
            texture->data->format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
            texture->data->semantic = data::TextureSemantic::Color;
            texture->data->pixels.resize(static_cast<std::size_t>(width) * height * 4);
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const std::size_t vertex_index = static_cast<std::size_t>(y) * width + x;
                    const float normal_y = std::clamp(mesh.vertices[vertex_index].normal.y_, 0.0f, 1.0f);
                    const float grass_ratio = std::clamp((normal_y - 0.70f) / 0.28f, 0.0f, 1.0f);
                    const float normalized_height = height_range > std::numeric_limits<float>::epsilon()
                        ? (heightfield.Samples()[vertex_index] - *minimum_height) / height_range
                        : 0.0f;
                    const float snow_t = std::clamp(
                        (normalized_height - snow_start) / (snow_full - snow_start), 0.0f, 1.0f);
                    const float snow_ratio = snow_t * snow_t * (3.0f - 2.0f * snow_t);
                    const float water_ratio = river_mask[vertex_index] && snow_ratio < 0.5f ? 1.0f : 0.0f;
                    const std::size_t pixel_index = vertex_index * 4;
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const float ground_color = dirt_color[channel] * (1.0f - grass_ratio) +
                            grass_color[channel] * grass_ratio;
                        const float snowy_color = ground_color * (1.0f - snow_ratio) +
                            snow_color[channel] * snow_ratio;
                        const float color = snowy_color * (1.0f - water_ratio) +
                            river_color[channel] * water_ratio;
                        texture->data->pixels[pixel_index + channel] = LinearToSrgbByte(color);
                    }
                    texture->data->pixels[pixel_index + 3] = 255;
                }
            if (!data::GenerateTextureMipChain(*texture->data, texture->data->semantic))
            {
                diagnostic = "could not generate mipmaps for the terrain slope-color texture";
                return false;
            }

            const std::string suffix = std::to_string(revision.fetch_add(1, std::memory_order_relaxed));
            const std::string texture_path = "generated://terrain/slope-color-" + suffix;
            AssetRegisterInfo texture_info{};
            texture_info.resource = texture;
            texture_info.path = texture_path;
            texture_info.name = "Terrain Slope Color";
            texture_info.type = AssetType::KPAT_Texture;
            const AssetID texture_id = assets.RegisterAsset(texture_info);
            if (!texture_id.IsValid())
            {
                diagnostic = "AssetManager rejected the generated terrain slope-color texture";
                return false;
            }

            auto material = std::make_shared<MaterialResource>();
            material->shader_path = shader_path;
            material->shader_dependency_index = 0;
            material->surface.shading_model = MaterialShadingModel::StandardPbr;
            material->parameters = {
                {"base_color", MaterialParameterSourceType::Vector4,
                 std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}},
                {"metallic", MaterialParameterSourceType::Scalar, 0.0f},
                {"roughness", MaterialParameterSourceType::Scalar, 0.95f},
                {"occlusion", MaterialParameterSourceType::Scalar, 1.0f},
                {"normal_scale", MaterialParameterSourceType::Scalar, 1.0f}};
            MaterialParameterSource albedo_texture{};
            albedo_texture.name = "base_color_texture";
            albedo_texture.type = MaterialParameterSourceType::Texture;
            albedo_texture.value = texture_path;
            albedo_texture.texture_color_space = MaterialTextureColorSpace::Srgb;
            albedo_texture.dependency_index = 1;
            material->parameters.push_back(std::move(albedo_texture));

            AssetRegisterInfo info{};
            info.resource = std::move(material);
            info.path = "generated://terrain/height-preview-" + suffix;
            info.name = "Terrain Slope Color Preview";
            info.dependencies = {shader, texture_id};
            info.type = AssetType::KPAT_Material;
            const AssetID id = assets.RegisterAsset(info);
            if (!id.IsValid())
            {
                assets.UnRegisterAsset(texture_id);
                diagnostic = "AssetManager rejected the neutral terrain preview material";
                return false;
            }
            material_ids = {id};
            texture_ids = {texture_id};
            diagnostic.clear();
            return true;
        }

        bool BuildPreview(std::string &diagnostic, data::MeshData &mesh,
                          spatial::AABB &bounds,
                          std::shared_ptr<const ScalarField2D> &heightfield,
                          TerrainRecipe &recipe, EvaluationResult &initial_evaluation)
        {
#ifndef KPENGINE_TERRAIN_FIXTURE_DIR
#define KPENGINE_TERRAIN_FIXTURE_DIR ""
#endif
            const std::string path = std::string(KPENGINE_TERRAIN_FIXTURE_DIR) +
                                     "/island_macro_256.terrainrecipe.json";
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
            recipe = TerrainRecipe::FromJson(source);
            auto registry = std::make_shared<OperatorRegistry>();
            if (!registry->RegisterBuiltins(diagnostic))
            {
                return false;
            }
            TerrainRecipe base_recipe = recipe;
            const auto thermal = std::find_if(base_recipe.nodes.begin(), base_recipe.nodes.end(),
                [](const RecipeNode &node) {
                    return node.operator_id == "terrain.erosion.thermal_flux";
                });
            if (thermal != base_recipe.nodes.end())
                base_recipe.nodes.erase(thermal, base_recipe.nodes.end());
            TerrainEvaluator evaluator(registry);
            initial_evaluation = evaluator.Evaluate(base_recipe);
            if (!initial_evaluation.succeeded)
            {
                diagnostic = initial_evaluation.diagnostic;
                return false;
            }
            const auto node = base_recipe.nodes.empty() ? initial_evaluation.nodes.end() :
                initial_evaluation.nodes.find(base_recipe.nodes.back().id);
            if (node == initial_evaluation.nodes.end() || node->second.outputs.empty())
            {
                diagnostic = "terrain fixture did not produce a heightfield";
                return false;
            }
            const auto height_value = node->second.outputs.at("height");
            const auto *const scalar_height = height_value->AsScalarField();
            if (scalar_height == nullptr)
            {
                diagnostic = "terrain fixture height output is not a scalar heightfield";
                return false;
            }
            heightfield = std::shared_ptr<const ScalarField2D>(height_value, scalar_height);
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
        engine.SetRayTracingEnabled(false);
        engine.SetPathTracingEnabled(false);
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

        const std::string environment_path =
            (std::filesystem::path{GetAssetDirectory()} /
             "texture/hdr/qwantani_dusk_2_puresky_4k.hdr").string();
        environment_texture_ = asset::AssetManager::GetInstance().LoadSync(environment_path);
        if (!environment_texture_.IsValid())
            return fail("could not load the terrain viewer HDR sky environment");
        render_environment_source_ = {environment_texture_, 0.25f};
        render_environment_handle_ = runtime::global_runtime_context.render_system_
            ->GetEnvironmentSourceSink()->EnqueueCreate(render_environment_source_);
        if (!render_environment_handle_.IsValid())
            return fail("RenderSystem rejected the terrain viewer environment source");
        if (!RegisterTerrainSkyDome(environment_texture_, environment_path,
                sky_dome_mesh_, sky_dome_material_, sky_dome_bounds_, diagnostic))
            return fail("could not prepare the terrain HDR sky dome: " + diagnostic);

        data::MeshData mesh_data;
        spatial::AABB bounds;
        recipe_ = std::make_unique<TerrainRecipe>();
        last_evaluation_ = std::make_unique<EvaluationResult>();
        if (!BuildPreview(diagnostic, mesh_data, bounds, preview_heightfield_, *recipe_,
                          *last_evaluation_))
        {
            return false;
        }
        AssignTerrainMaterialSections(mesh_data);
        if (!RegisterTerrainPreviewMaterial(mesh_data, *preview_heightfield_,
                terrain_material_assets_, terrain_texture_assets_, diagnostic))
            return fail("could not prepare terrain slope-color material: " + diagnostic);
        operator_registry_ = std::make_shared<OperatorRegistry>();
        if (!operator_registry_->RegisterBuiltins(diagnostic)) return false;
        generation_executor_ = std::make_unique<GenerationExecutor>(
            operator_registry_, 1, 1, 1);
        execution_control_ = std::make_shared<EvaluationExecutionControl>();
        committed_recipe_ = std::make_unique<TerrainRecipe>(*recipe_);
        const bool has_thermal_erosion = std::any_of(recipe_->nodes.begin(), recipe_->nodes.end(),
            [](const RecipeNode &node) {
                return node.operator_id == "terrain.erosion.thermal_flux";
            });
        if (has_thermal_erosion)
        {
            revision_ = 1;
            {
                std::lock_guard lock(progress_mutex_);
                progress_revision_ = revision_;
                progress_nodes_.clear();
            }
            if (!generation_executor_->Submit(revision_, *recipe_, execution_control_,
                    [this, revision = revision_](std::string_view node_id,
                                                 const NodeResult &node_result) {
                        std::lock_guard lock(progress_mutex_);
                        if (progress_revision_ == revision)
                            progress_nodes_[std::string(node_id)] = node_result;
                    }))
                return fail("could not queue initial thermal erosion evaluation");
            generation_status_ = "Generating thermal erosion preview";
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

        preview_material_ = terrain_material_assets_.front();
        render_roots_ = {generated_mesh_, sky_dome_mesh_, sky_dome_material_};
        render_roots_.insert(render_roots_.end(), terrain_material_assets_.begin(),
                             terrain_material_assets_.end());
        render_roots_.insert(render_roots_.end(), terrain_texture_assets_.begin(),
                             terrain_texture_assets_.end());
        render_roots_.push_back(environment_texture_);

        gameplay::StaticMeshActorDesc terrain_desc{};
        terrain_desc.mesh_asset = generated_mesh_;
        terrain_desc.material_asset = preview_material_;
        terrain_desc.material_assets = terrain_material_assets_;
        terrain_desc.local_bounds = bounds;
        terrain_actor_ = gameplay::CreateStaticMeshActor(*world, terrain_desc);
        if (!terrain_actor_.IsValid())
        {
            return fail("could not create the terrain preview Actor and MeshComponent");
        }

        gameplay::CameraActorDesc camera_desc{};
        const float initial_yaw = camera_yaw_degrees_ * 0.01745329251994329577f;
        const float initial_pitch = camera_pitch_degrees_ * 0.01745329251994329577f;
        const float initial_horizontal = std::cos(initial_pitch) * camera_distance_;
        camera_desc.transform.position_ = {
            -std::cos(initial_yaw) * initial_horizontal,
            camera_target_y_ - std::sin(initial_pitch) * camera_distance_,
            -std::sin(initial_yaw) * initial_horizontal};
        camera_desc.transform.rotator_ = {
            camera_pitch_degrees_, camera_yaw_degrees_, 0.0f};
        camera_desc.far_plane = 3000.0f;
        camera_actor_ = gameplay::CreateCameraActor(*world, camera_desc);
        gameplay::StaticMeshActorDesc sky_dome_desc{};
        sky_dome_desc.mesh_asset = sky_dome_mesh_;
        sky_dome_desc.material_asset = sky_dome_material_;
        sky_dome_desc.material_assets = {sky_dome_material_};
        sky_dome_desc.transform.position_ = camera_desc.transform.position_;
        sky_dome_desc.local_bounds = sky_dome_bounds_;
        sky_dome_desc.casts_shadow = false;
        sky_dome_actor_ = gameplay::CreateStaticMeshActor(*world, sky_dome_desc);
        gameplay::DirectionalLightActorDesc light_desc{};
        light_desc.direction = {-0.4f, -1.0f, -0.3f};
        light_desc.intensity = 3.0f;
        light_actor_ = gameplay::CreateDirectionalLightActor(*world, light_desc);
        if (!camera_actor_.IsValid() || !sky_dome_actor_.IsValid() ||
            !light_actor_.IsValid())
        {
            return fail("could not create preview camera, sky dome, or directional light Actor");
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
        if (!terrain_editor_->Initialize(engine, preview_heightfield_, diagnostic,
                [this](std::uint64_t seed, std::uint32_t lattice_size,
                       std::uint32_t octaves, float persistence, float lacunarity,
                       float talus_angle_degrees, float thermal_rate,
                       std::uint32_t thermal_iterations) {
                    RequestRegenerate(seed, lattice_size, octaves, persistence, lacunarity,
                        talus_angle_degrees, thermal_rate, thermal_iterations);
                },
                [this] { RequestCancel(); },
                [this](int command) { RequestExecutionControl(command); },
                [this](float yaw, float pitch, float distance) {
                    QueueCamera(yaw, pitch, distance);
                },
                [this] { RequestBake(); }))
        {
            terrain_editor_.reset();
            return false;
        }
        if (last_evaluation_ && recipe_)
            terrain_editor_->SetEvaluationSnapshot(preview_heightfield_, *last_evaluation_,
                                                    *recipe_, "Ready");
        return true;
    }

    bool TerrainViewerHost::RegisterHostCommands(
        runtime::command::CommandRegistry &registry, std::string &diagnostic)
    {
        using namespace runtime::command;
        CommandDesc regenerate{
            "terrain.regenerate", "TerrainViewer",
            "Generate and publish a seeded 256x256 terrain preview",
            CommandCategory::Gameplay,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState,
            {{CommandArgumentDesc{"seed", CommandValueType::UnsignedInteger, true, {}, {}},
              CommandArgumentDesc{"lattice_size", CommandValueType::UnsignedInteger, true, {}, {}},
              CommandArgumentDesc{"octaves", CommandValueType::UnsignedInteger, true, {}, {}},
              CommandArgumentDesc{"persistence", CommandValueType::Float, true, {}, {}},
              CommandArgumentDesc{"lacunarity", CommandValueType::Float, true, {}, {}}}},
            [this](const CommandCall &call, const CommandContext &context)
            {
                const auto seed = std::get<std::uint64_t>(call.arguments.at("seed"));
                const auto lattice_size = std::get<std::uint64_t>(call.arguments.at("lattice_size"));
                const auto octaves = std::get<std::uint64_t>(call.arguments.at("octaves"));
                const auto persistence = std::get<double>(call.arguments.at("persistence"));
                const auto lacunarity = std::get<double>(call.arguments.at("lacunarity"));
                RequestRegenerate(seed, static_cast<std::uint32_t>(lattice_size),
                    static_cast<std::uint32_t>(octaves), static_cast<float>(persistence),
                    static_cast<float>(lacunarity));
                return CommandResult{CommandStatus::Success,
                    "Terrain regeneration queued", context.request_id,
                    {{"seed", seed}, {"lattice_size", lattice_size},
                     {"octaves", octaves}, {"persistence", persistence},
                     {"lacunarity", lacunarity}}};
            }, CommandThread::Game};
        auto regenerate_registration = registry.Register(std::move(regenerate));
        if (!regenerate_registration.IsSuccess())
        {
            diagnostic = regenerate_registration.diagnostic;
            return false;
        }
        command_registrations_.push_back(std::move(regenerate_registration.registration));

        CommandDesc cancel{
            "terrain.cancel", "TerrainViewer",
            "Cancel in-flight Terrain generation while retaining the last committed preview",
            CommandCategory::Gameplay,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState,
            {},
            [this](const CommandCall &, const CommandContext &context)
            {
                RequestCancel();
                return CommandResult{CommandStatus::Success,
                    "Terrain cancellation queued", context.request_id, {}};
            }, CommandThread::Game};
        auto cancel_registration = registry.Register(std::move(cancel));
        if (!cancel_registration.IsSuccess())
        {
            diagnostic = cancel_registration.diagnostic;
            command_registrations_.clear();
            return false;
        }
        command_registrations_.push_back(std::move(cancel_registration.registration));

        CommandDesc execution_control{
            "terrain.execution_control", "TerrainViewer",
            "Pause, step, or resume Terrain DAG evaluation at node boundaries",
            CommandCategory::Gameplay,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState,
            {{CommandArgumentDesc{"action", CommandValueType::Enum, true, {},
                                  {"pause", "step", "resume"}}}},
            [this](const CommandCall &call, const CommandContext &context)
            {
                const std::string &action = std::get<std::string>(call.arguments.at("action"));
                const int control = action == "pause" ? 0 : action == "step" ? 1 : 2;
                RequestExecutionControl(control);
                return CommandResult{CommandStatus::Success,
                    "Terrain execution control queued: " + action, context.request_id,
                    {{"action", action}}};
            }, CommandThread::Game};
        auto execution_control_registration = registry.Register(std::move(execution_control));
        if (!execution_control_registration.IsSuccess())
        {
            diagnostic = execution_control_registration.diagnostic;
            command_registrations_.clear();
            return false;
        }
        command_registrations_.push_back(
            std::move(execution_control_registration.registration));

        CommandDesc hydraulic_comparison{
            "terrain.run_hydraulic_comparison", "TerrainViewer",
            "Run the CPU hydraulic erosion node on the current terrain preview",
            CommandCategory::Gameplay,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState,
            {},
            [this](const CommandCall &, const CommandContext &context)
            {
                RequestHydraulicComparison();
                return CommandResult{CommandStatus::Success,
                    "Hydraulic terrain comparison queued", context.request_id, {}};
            }, CommandThread::Game};
        auto hydraulic_comparison_registration =
            registry.Register(std::move(hydraulic_comparison));
        if (!hydraulic_comparison_registration.IsSuccess())
        {
            diagnostic = hydraulic_comparison_registration.diagnostic;
            command_registrations_.clear();
            return false;
        }
        command_registrations_.push_back(
            std::move(hydraulic_comparison_registration.registration));

        CommandDesc preview_status{
            "terrain.preview_status", "TerrainViewer",
            "Read the current terrain generation and preview status",
            CommandCategory::Gameplay,
            CommandFlags::AgentAllowed,
            {},
            [this](const CommandCall &, const CommandContext &context)
            {
                return CommandResult{CommandStatus::Success, generation_status_,
                    context.request_id, {{"status", generation_status_},
                                         {"revision", revision_}}};
            }, CommandThread::Game};
        auto preview_status_registration = registry.Register(std::move(preview_status));
        if (!preview_status_registration.IsSuccess())
        {
            diagnostic = preview_status_registration.diagnostic;
            command_registrations_.clear();
            return false;
        }
        command_registrations_.push_back(
            std::move(preview_status_registration.registration));

        CommandDesc bake{
            "terrain.bake", "TerrainViewer",
            "Bake the last committed Terrain preview to native model and material assets",
            CommandCategory::Gameplay,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState,
            {},
            [this](const CommandCall &, const CommandContext &context)
            {
                if (!context.complete)
                    return CommandResult{CommandStatus::Failed,
                        "Terrain bake requires a deferred Runtime command request",
                        context.request_id, {}};
                if (bake_in_progress_ || !committed_recipe_ || !preview_heightfield_)
                    return CommandResult{CommandStatus::Busy,
                        "Terrain bake is unavailable while generation or baking is busy",
                        context.request_id, {}};
                bake_command_completion_ = context.complete;
                bake_command_request_id_ = context.request_id;
                ApplyBake();
                return CommandResult{CommandStatus::Pending,
                    "Terrain native bake started", context.request_id,
                    {{"status", std::string{"pending"}}}};
            }, CommandThread::Game};
        auto bake_registration = registry.Register(std::move(bake));
        if (!bake_registration.IsSuccess())
        {
            diagnostic = bake_registration.diagnostic;
            command_registrations_.clear();
            return false;
        }
        command_registrations_.push_back(std::move(bake_registration.registration));
        diagnostic.clear();
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
        if (refresh_sky_dome_on_first_tick_)
        {
            if (!RecreateSkyDomeActor(diagnostic)) return false;
            refresh_sky_dome_on_first_tick_ = false;
        }
        if (publish_initial_preview_on_first_tick_)
        {
            publish_initial_preview_on_first_tick_ = false;
            std::string publish_diagnostic;
            if (!last_evaluation_ || !preview_heightfield_ ||
                !PublishPreview(*last_evaluation_, preview_heightfield_,
                                publish_diagnostic))
            {
                generation_status_ = "Failed to publish the initial terrain preview: " +
                                     publish_diagnostic;
                diagnostic = generation_status_;
                return false;
            }
        }
        ProcessAuthoringCommands();
        std::map<std::string, NodeResult, std::less<>> progress_snapshot;
        {
            std::lock_guard lock(progress_mutex_);
            if (progress_revision_ == revision_) progress_snapshot = progress_nodes_;
        }
        if (!progress_snapshot.empty())
        {
            if (!last_evaluation_) last_evaluation_ = std::make_unique<EvaluationResult>();
            last_evaluation_->nodes = std::move(progress_snapshot);
            if (execution_control_ && execution_control_->IsPaused())
                generation_status_ = "Paused at node boundary";
            if (terrain_editor_)
                terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                    *last_evaluation_, *recipe_, generation_status_);
        }
        if (bake_in_progress_ && bake_future_.valid() &&
            bake_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            const TerrainBakeResult baked = bake_future_.get();
            bake_in_progress_ = false;
            generation_status_ = baked.succeeded
                ? "Baked " + baked.logical_model_path
                : "Bake failed: " + baked.diagnostic;
            if (terrain_editor_ && last_evaluation_)
                terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                    *last_evaluation_, *recipe_, generation_status_);
            if (bake_command_completion_)
            {
                runtime::command::CommandResult command_result{
                    baked.succeeded ? runtime::command::CommandStatus::Success
                                    : runtime::command::CommandStatus::Failed,
                    baked.succeeded ? "Terrain bake completed" : baked.diagnostic,
                    bake_command_request_id_,
                    {{"logical_model_path", baked.logical_model_path},
                     {"provenance_path", baked.provenance_path},
                     {"bake_time_ms", baked.bake_time_ms},
                     {"model_bytes", static_cast<std::uint64_t>(baked.model_bytes)},
                     {"material_bytes", static_cast<std::uint64_t>(baked.material_bytes)}}};
                bake_command_completion_(std::move(command_result));
                bake_command_completion_ = {};
                bake_command_request_id_ = 0;
            }
        }
        if (preview_publication_.IsPending() &&
            runtime::global_runtime_context.render_system_)
        {
            const std::uint64_t serial = preview_publication_.serial;
            const auto update = runtime::global_runtime_context.render_system_->
                GetPreparedAssetsUpdateResult(serial);
            if (update.status == render::PreparedAssetsUpdateStatus::Failed ||
                update.status == render::PreparedAssetsUpdateStatus::Superseded ||
                update.status == render::PreparedAssetsUpdateStatus::Unknown)
            {
                const std::string failure = update.diagnostic.empty()
                    ? "catalog update did not reach a terminal applied state"
                    : update.diagnostic;
                (void)preview_publication_.MarkFailed(serial, failure);
                DiscardPendingPreview();
                generation_status_ = "Preview catalog promotion failed: " + failure;
                if (terrain_editor_ && last_evaluation_ && recipe_)
                    terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                        *last_evaluation_, *recipe_, generation_status_);
            }
            if (update.status == render::PreparedAssetsUpdateStatus::Applied &&
                preview_publication_.MarkApplied(serial))
            {
            PreviewAssets &pending = *preview_publication_.assets;
            gameplay::GameplayWorld *const world =
                runtime::global_runtime_context.gameplay_world_.get();
            if (world == nullptr)
            {
                diagnostic = "GameplayWorld disappeared during terrain preview replacement";
                (void)preview_publication_.MarkFailed(serial, diagnostic);
                DiscardPendingPreview();
                return false;
            }
            if (terrain_actor_.IsValid()) (void)world->DestroyActor(terrain_actor_);
            if (sky_dome_actor_.IsValid()) (void)world->DestroyActor(sky_dome_actor_);
            if (camera_actor_.IsValid()) (void)world->DestroyActor(camera_actor_);
            if (light_actor_.IsValid()) (void)world->DestroyActor(light_actor_);
            world->ReclaimDestroyedActors();
            for (const asset::AssetID id : terrain_material_assets_)
                asset::AssetManager::GetInstance().UnRegisterAsset(id);
            for (const asset::AssetID id : terrain_texture_assets_)
                asset::AssetManager::GetInstance().UnRegisterAsset(id);
            terrain_material_assets_ = pending.materials;
            terrain_texture_assets_ = pending.textures;
            if (generated_mesh_.IsValid())
                asset::AssetManager::GetInstance().UnRegisterAsset(generated_mesh_);
            terrain_actor_ = {};
            sky_dome_actor_ = {};
            generated_mesh_ = pending.mesh;
            preview_material_ = pending.material;
            preview_heightfield_ = pending.heightfield;
            committed_recipe_ = std::make_unique<TerrainRecipe>(*recipe_);
            gameplay::StaticMeshActorDesc desc{};
            desc.mesh_asset = generated_mesh_;
            desc.material_asset = preview_material_;
            desc.material_assets = terrain_material_assets_;
            desc.local_bounds = pending.bounds;
            terrain_actor_ = gameplay::CreateStaticMeshActor(*world, desc);
            if (!terrain_actor_.IsValid())
            {
                diagnostic = "could not create replacement terrain Actor";
                (void)preview_publication_.MarkFailed(serial, diagnostic);
                DiscardPendingPreview();
                generation_status_ = "Failed to create preview Actor";
                return true;
            }
            gameplay::CameraActorDesc camera_desc{};
            camera_desc.transform.rotator_ = {
                camera_pitch_degrees_, camera_yaw_degrees_, 0.0f};
            const float yaw = camera_yaw_degrees_ * 0.01745329251994329577f;
            const float pitch = camera_pitch_degrees_ * 0.01745329251994329577f;
            const float horizontal = std::cos(pitch) * camera_distance_;
            camera_desc.transform.position_ = {
                -std::cos(yaw) * horizontal,
                camera_target_y_ - std::sin(pitch) * camera_distance_,
                -std::sin(yaw) * horizontal};
            camera_desc.far_plane = 3000.0f;
            camera_actor_ = gameplay::CreateCameraActor(*world, camera_desc);
            gameplay::StaticMeshActorDesc sky_dome_desc{};
            sky_dome_desc.mesh_asset = sky_dome_mesh_;
            sky_dome_desc.material_asset = sky_dome_material_;
            sky_dome_desc.material_assets = {sky_dome_material_};
            sky_dome_desc.transform.position_ = camera_desc.transform.position_;
            sky_dome_desc.local_bounds = sky_dome_bounds_;
            sky_dome_desc.casts_shadow = false;
            sky_dome_actor_ = gameplay::CreateStaticMeshActor(*world, sky_dome_desc);
            gameplay::DirectionalLightActorDesc light_desc{};
            light_desc.direction = {-0.4f, -1.0f, -0.3f};
            light_desc.intensity = 3.0f;
            light_actor_ = gameplay::CreateDirectionalLightActor(*world, light_desc);
            if (!camera_actor_.IsValid() || !sky_dome_actor_.IsValid() ||
                !light_actor_.IsValid())
            {
                diagnostic = "could not restore preview camera, sky dome, or light after catalog replacement";
                (void)preview_publication_.MarkFailed(serial, diagnostic);
                DiscardPendingPreview();
                generation_status_ = "Failed to restore preview camera, sky dome, or light";
                return true;
            }
            render_roots_ = {generated_mesh_, sky_dome_mesh_, sky_dome_material_};
            render_roots_.insert(render_roots_.end(), terrain_material_assets_.begin(),
                                 terrain_material_assets_.end());
            render_roots_.insert(render_roots_.end(), terrain_texture_assets_.begin(),
                                 terrain_texture_assets_.end());
            render_roots_.push_back(environment_texture_);
            last_evaluation_ = pending.evaluation
                ? std::make_unique<EvaluationResult>(*pending.evaluation)
                : nullptr;
            if (terrain_editor_ && last_evaluation_)
                terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                    *last_evaluation_, *recipe_, "Ready");
            preview_publication_.FinishApplied();
            generation_status_ = "Ready";
            }
        }

        GenerationJobResult completed;
        while (generation_executor_ && generation_executor_->TryPop(completed))
        {
            if (completed.revision != revision_) continue;
            {
                std::lock_guard lock(progress_mutex_);
                if (progress_revision_ == completed.revision) progress_nodes_.clear();
            }
            last_evaluation_ = std::make_unique<EvaluationResult>(completed.evaluation);
            if (!completed.evaluation.succeeded)
            {
                generation_status_ = completed.evaluation.cancelled ? "Cancelled" : "Failed";
                if (terrain_editor_)
                    terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                        completed.evaluation, *recipe_, generation_status_);
                continue;
            }
            const auto node = recipe_->nodes.empty() ? completed.evaluation.nodes.end() :
                completed.evaluation.nodes.find(recipe_->nodes.back().id);
            if (node == completed.evaluation.nodes.end() ||
                !node->second.outputs.contains("height"))
            {
                generation_status_ = "Failed: final terrain height output missing";
                continue;
            }
            std::string publish_diagnostic;
            const auto height_value = node->second.outputs.at("height");
            const auto *const scalar_height = height_value->AsScalarField();
            if (scalar_height == nullptr)
            {
                generation_status_ = "Failed: final terrain height output is not scalar";
                continue;
            }
            auto field = std::shared_ptr<const ScalarField2D>(height_value, scalar_height);
            if (!PublishPreview(completed.evaluation, std::move(field), publish_diagnostic))
            {
                generation_status_ = "Failed: " + publish_diagnostic;
                if (terrain_editor_)
                    terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                        completed.evaluation, *recipe_, generation_status_);
            }
        }
        return true;
    }

    void TerrainViewerHost::RequestRegenerate(std::uint64_t seed,
        std::uint32_t lattice_size, std::uint32_t octaves,
        float persistence, float lacunarity, float talus_angle_degrees,
        float thermal_rate, std::uint32_t thermal_iterations)
    {
        std::lock_guard lock(authoring_command_mutex_);
        AuthoringCommand command;
        command.kind = AuthoringCommand::Kind::Regenerate;
        command.seed = seed;
        command.lattice_size = lattice_size;
        command.octaves = octaves;
        command.third = persistence;
        command.fourth = lacunarity;
        command.talus_angle_degrees = talus_angle_degrees;
        command.thermal_rate = thermal_rate;
        command.thermal_iterations = thermal_iterations;
        authoring_commands_.push_back(command);
    }

    void TerrainViewerHost::RequestCancel()
    {
        std::lock_guard lock(authoring_command_mutex_);
        AuthoringCommand command;
        command.kind = AuthoringCommand::Kind::Cancel;
        authoring_commands_.push_back(command);
    }

    void TerrainViewerHost::RequestExecutionControl(int control)
    {
        std::lock_guard lock(authoring_command_mutex_);
        AuthoringCommand command;
        command.kind = AuthoringCommand::Kind::Control;
        command.control = control;
        authoring_commands_.push_back(command);
    }

    void TerrainViewerHost::RequestHydraulicComparison()
    {
        std::lock_guard lock(authoring_command_mutex_);
        authoring_commands_.push_back({AuthoringCommand::Kind::HydraulicComparison});
    }

    void TerrainViewerHost::QueueCamera(float yaw_degrees, float pitch_degrees,
                                        float distance)
    {
        std::lock_guard lock(authoring_command_mutex_);
        authoring_commands_.push_back({AuthoringCommand::Kind::Camera, 0,
            yaw_degrees, pitch_degrees, distance});
    }

    void TerrainViewerHost::RequestBake()
    {
        std::lock_guard lock(authoring_command_mutex_);
        AuthoringCommand command;
        command.kind = AuthoringCommand::Kind::Bake;
        authoring_commands_.push_back(command);
    }

    void TerrainViewerHost::ProcessAuthoringCommands()
    {
        std::deque<AuthoringCommand> commands;
        {
            std::lock_guard lock(authoring_command_mutex_);
            commands.swap(authoring_commands_);
        }
        for (const AuthoringCommand &command : commands)
        {
            switch (command.kind)
            {
            case AuthoringCommand::Kind::Regenerate:
                ApplyRegenerate(command.seed, command.lattice_size, command.octaves,
                    command.third, command.fourth, command.talus_angle_degrees,
                    command.thermal_rate, command.thermal_iterations);
                break;
            case AuthoringCommand::Kind::Cancel:
                ApplyCancel();
                break;
            case AuthoringCommand::Kind::Control:
                ApplyExecutionControl(command.control);
                break;
            case AuthoringCommand::Kind::HydraulicComparison:
                ApplyHydraulicComparison();
                break;
            case AuthoringCommand::Kind::Camera:
                UpdateCamera(command.first, command.second, command.third);
                break;
            case AuthoringCommand::Kind::Bake:
                ApplyBake();
                break;
            }
        }
    }

    void TerrainViewerHost::ApplyRegenerate(std::uint64_t seed,
        std::uint32_t lattice_size, std::uint32_t octaves,
        float persistence, float lacunarity, float talus_angle_degrees,
        float thermal_rate, std::uint32_t thermal_iterations)
    {
        if (preview_publication_.IsPending())
        {
            generation_status_ = "Busy: waiting for the preview catalog swap";
            return;
        }
        if (!generation_executor_ || !recipe_ || !execution_control_ ||
            lattice_size == 0 || lattice_size > 64 || octaves == 0 || octaves > 16 ||
            !std::isfinite(persistence) || persistence < 0.0f || persistence > 1.0f ||
            !std::isfinite(lacunarity) || lacunarity < 1.0f || lacunarity > 8.0f ||
            !std::isfinite(talus_angle_degrees) || talus_angle_degrees < 0.0f ||
            talus_angle_degrees >= 89.0f || !std::isfinite(thermal_rate) ||
            thermal_rate <= 0.0f || thermal_rate > 1.0f ||
            thermal_iterations == 0 || thermal_iterations > 512)
        {
            generation_status_ = "Invalid generation controls";
            return;
        }
        recipe_->seed = seed;
        const auto node = std::find_if(recipe_->nodes.begin(), recipe_->nodes.end(),
            [](const RecipeNode &candidate) {
                return candidate.operator_id == "terrain.heightfield.perlin_fbm";
            });
        if (node == recipe_->nodes.end())
        {
            generation_status_ = "Recipe is missing its Perlin fBm node";
            return;
        }
        node->parameters["lattice_size"] = lattice_size;
        node->parameters["octaves"] = octaves;
        node->parameters["persistence"] = persistence;
        node->parameters["lacunarity"] = lacunarity;
        const auto thermal_node = std::find_if(recipe_->nodes.begin(), recipe_->nodes.end(),
            [](const RecipeNode &candidate) {
                return candidate.operator_id == "terrain.erosion.thermal_flux";
            });
        if (thermal_node != recipe_->nodes.end())
        {
            thermal_node->parameters["talus_angle_degrees"] = talus_angle_degrees;
            thermal_node->parameters["thermal_rate"] = thermal_rate;
            thermal_node->parameters["iterations"] = thermal_iterations;
        }
        execution_control_->Resume();
        ++revision_;
        {
            std::lock_guard lock(progress_mutex_);
            progress_revision_ = revision_;
            progress_nodes_.clear();
        }
        last_evaluation_ = std::make_unique<EvaluationResult>();
        if (!generation_executor_->Submit(revision_, *recipe_, execution_control_,
                [this, revision = revision_](std::string_view node_id, const NodeResult &node_result) {
                    std::lock_guard lock(progress_mutex_);
                    if (progress_revision_ == revision)
                        progress_nodes_[std::string(node_id)] = node_result;
                }))
        {
            generation_status_ = "Busy: generation queue is full";
            return;
        }
        generation_status_ = "Generating revision " + std::to_string(revision_);
        if (terrain_editor_ && last_evaluation_)
            terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                *last_evaluation_, *recipe_, generation_status_);
    }

    void TerrainViewerHost::ApplyHydraulicComparison()
    {
        if (preview_publication_.IsPending() || !generation_executor_ || !execution_control_ || !recipe_ ||
            generation_status_.starts_with("Generating") ||
            generation_status_.starts_with("Waiting for render boundary"))
        {
            generation_status_ = "Busy: wait for the current terrain update";
            return;
        }
        if (std::any_of(recipe_->nodes.begin(), recipe_->nodes.end(),
            [](const RecipeNode &node) {
                return node.operator_id == "terrain.erosion.hydraulic_pipe";
            }))
        {
            generation_status_ = "Hydraulic comparison already applied";
            return;
        }
        if (recipe_->nodes.empty())
        {
            generation_status_ = "Failed: terrain recipe is empty";
            return;
        }

        TerrainRecipe comparison_recipe = *recipe_;
        const std::string bedrock_node = comparison_recipe.nodes.back().id;
        const auto add_scalar = [&comparison_recipe](const char *id, float value) {
            comparison_recipe.nodes.push_back({id, "terrain.scalar.constant", 1,
                {{"value", value}}, {}});
        };
        add_scalar("comparison_soil", 0.3f);
        add_scalar("comparison_sand", 0.0f);
        add_scalar("comparison_water", 0.0f);
        add_scalar("comparison_sediment", 0.0f);
        add_scalar("comparison_rain", 0.02f);
        add_scalar("comparison_erodibility", 0.8f);
        add_scalar("comparison_hardness", 0.25f);
        add_scalar("comparison_obstacle", 0.0f);
        comparison_recipe.nodes.push_back({"comparison_state",
            "terrain.state.layered_heightfield", 1, nlohmann::json::object(),
            {{"bedrock_elevation_m", {bedrock_node, "height"}},
             {"soil_thickness_m", {"comparison_soil", "value"}},
             {"sand_thickness_m", {"comparison_sand", "value"}},
             {"water_depth_m", {"comparison_water", "value"}},
             {"suspended_sediment_kg_per_m2", {"comparison_sediment", "value"}}}});
        comparison_recipe.nodes.push_back({"hydraulic_comparison",
            "terrain.erosion.hydraulic_pipe", 1,
            {{"duration_s", 12.0}, {"maximum_timestep_s", 0.05},
             {"capacity_kg_s_per_m3", 8.0}, {"dissolution_rate_per_s", 0.8},
             {"deposition_rate_per_s", 1.0}, {"boundary", "open"}},
            {{"state", {"comparison_state", "state"}},
             {"rain_rate_m_per_s", {"comparison_rain", "value"}},
             {"erodibility_0_1", {"comparison_erodibility", "value"}},
             {"hardness_0_1", {"comparison_hardness", "value"}},
             {"obstacle_0_1", {"comparison_obstacle", "value"}}}});

        execution_control_->Resume();
        ++revision_;
        {
            std::lock_guard lock(progress_mutex_);
            progress_revision_ = revision_;
            progress_nodes_.clear();
        }
        last_evaluation_ = std::make_unique<EvaluationResult>();
        if (!generation_executor_->Submit(revision_, comparison_recipe, execution_control_,
                [this, revision = revision_](std::string_view node_id,
                                             const NodeResult &node_result) {
                    std::lock_guard lock(progress_mutex_);
                    if (progress_revision_ == revision)
                        progress_nodes_[std::string(node_id)] = node_result;
                }))
        {
            generation_status_ = "Busy: terrain evaluation queue is full";
            return;
        }
        recipe_ = std::make_unique<TerrainRecipe>(std::move(comparison_recipe));
        generation_status_ = "Generating hydraulic erosion comparison";
        if (terrain_editor_ && last_evaluation_)
            terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                *last_evaluation_, *recipe_, generation_status_);
    }

    void TerrainViewerHost::ApplyCancel()
    {
        if (!generation_executor_) return;
        ++revision_;
        generation_executor_->CancelBefore(revision_);
        if (execution_control_) execution_control_->Resume();
        {
            std::lock_guard lock(progress_mutex_);
            progress_nodes_.clear();
        }
        generation_status_ = "Cancelled; previous terrain retained";
        if (terrain_editor_ && last_evaluation_)
            terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                *last_evaluation_, *recipe_, generation_status_);
    }

    void TerrainViewerHost::ApplyExecutionControl(int command)
    {
        if (!execution_control_) return;
        if (command == 0)
        {
            execution_control_->Pause();
            generation_status_ = "Paused at node boundary";
        }
        else if (command == 1)
        {
            execution_control_->Step();
            generation_status_ = "Stepping one node";
        }
        else if (command == 2)
        {
            execution_control_->Resume();
            generation_status_ = "Generating revision " + std::to_string(revision_);
        }
        if (terrain_editor_ && last_evaluation_ && committed_recipe_)
            terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                *last_evaluation_, *recipe_, generation_status_);
    }

    void TerrainViewerHost::ApplyBake()
    {
        if (bake_in_progress_ || !committed_recipe_ || !preview_heightfield_)
        {
            generation_status_ = "Bake unavailable: generation or bake is busy";
            return;
        }
        TerrainRecipe recipe_snapshot = *committed_recipe_;
        std::shared_ptr<const ScalarField2D> field_snapshot = preview_heightfield_;
        bake_in_progress_ = true;
        generation_status_ = "Baking native model and material";
        bake_future_ = std::async(std::launch::async,
            [recipe = std::move(recipe_snapshot), field = std::move(field_snapshot)] {
                return TerrainBaker::Bake(recipe, *field);
            });
    }

    void TerrainViewerHost::UpdateCamera(float yaw_degrees, float pitch_degrees,
                                         float distance)
    {
        camera_yaw_degrees_ = yaw_degrees;
        camera_pitch_degrees_ = pitch_degrees;
        camera_distance_ = distance;
        gameplay::GameplayWorld *const world =
            runtime::global_runtime_context.gameplay_world_.get();
        if (!world || !camera_actor_.IsValid()) return;
        const float yaw = yaw_degrees * 0.01745329251994329577f;
        const float pitch = pitch_degrees * 0.01745329251994329577f;
        const float horizontal = std::cos(pitch) * distance;
        const Vector3f position{
            -std::cos(yaw) * horizontal,
            camera_target_y_ - std::sin(pitch) * distance,
            -std::sin(yaw) * horizontal};
        (void)world->SetActorRootTransform(camera_actor_, position,
            Rotatorf{pitch_degrees, yaw_degrees, 0.0f});
        if (sky_dome_actor_.IsValid())
            (void)world->SetActorRootTransform(sky_dome_actor_, position, Rotatorf{});
    }

    bool TerrainViewerHost::RecreateSkyDomeActor(std::string &diagnostic)
    {
        gameplay::GameplayWorld *const world =
            runtime::global_runtime_context.gameplay_world_.get();
        if (world == nullptr || !sky_dome_mesh_.IsValid() ||
            !sky_dome_material_.IsValid())
        {
            diagnostic = "sky dome dependencies or GameplayWorld are unavailable";
            return false;
        }
        if (sky_dome_actor_.IsValid())
        {
            (void)world->DestroyActor(sky_dome_actor_);
            world->ReclaimDestroyedActors();
            sky_dome_actor_ = {};
        }
        const float yaw = camera_yaw_degrees_ * 0.01745329251994329577f;
        const float pitch = camera_pitch_degrees_ * 0.01745329251994329577f;
        const float horizontal = std::cos(pitch) * camera_distance_;
        gameplay::StaticMeshActorDesc desc{};
        desc.mesh_asset = sky_dome_mesh_;
        desc.material_asset = sky_dome_material_;
        desc.material_assets = {sky_dome_material_};
        desc.transform.position_ = {
            -std::cos(yaw) * horizontal,
            camera_target_y_ - std::sin(pitch) * camera_distance_,
            -std::sin(yaw) * horizontal};
        desc.local_bounds = sky_dome_bounds_;
        desc.casts_shadow = false;
        sky_dome_actor_ = gameplay::CreateStaticMeshActor(*world, desc);
        if (!sky_dome_actor_.IsValid())
        {
            diagnostic = "could not refresh the terrain HDR sky-dome renderable";
            return false;
        }
        diagnostic.clear();
        return true;
    }

    bool TerrainViewerHost::PublishPreview(const EvaluationResult &result,
        std::shared_ptr<const ScalarField2D> heightfield, std::string &diagnostic)
    {
        if (preview_publication_.IsPending())
        {
            diagnostic = "a terrain preview catalog update is already pending";
            return false;
        }
        if (!heightfield || !runtime::global_runtime_context.render_system_)
        {
            diagnostic = "preview data or RenderSystem is unavailable";
            return false;
        }
        data::MeshData mesh_data = BuildHeightfieldMesh(*heightfield);
        try
        {
            AssignTerrainMaterialSections(mesh_data);
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            return false;
        }
        if (mesh_data.vertices.empty() || mesh_data.indices.empty())
        {
            diagnostic = "generated heightfield produced an empty mesh";
            return false;
        }
        const float maximum = std::numeric_limits<float>::max();
        spatial::AABB bounds{{maximum, maximum, maximum},
                             {-maximum, -maximum, -maximum}};
        for (const data::Vertex &vertex : mesh_data.vertices)
            bounds.ExpandToInclude(vertex.position);
        auto mesh = std::make_shared<asset::MeshResource>();
        mesh->data = std::make_shared<data::MeshData>(std::move(mesh_data));
        mesh->local_bounds = bounds;
        mesh->vertex_count = static_cast<uint32_t>(mesh->data->vertices.size());
        mesh->face_count = static_cast<uint32_t>(mesh->data->indices.size() / 3u);
        asset::AssetManager &assets = asset::AssetManager::GetInstance();
        const std::string suffix = std::to_string(revision_);
        asset::AssetRegisterInfo mesh_info{};
        mesh_info.resource = mesh;
        mesh_info.path = "generated://terrain/preview-" + suffix;
        mesh_info.name = "Terrain Preview " + suffix;
        mesh_info.type = asset::AssetType::KPAT_Mesh;
        const asset::AssetID mesh_id = assets.RegisterAsset(mesh_info);
        if (!mesh_id.IsValid())
        {
            diagnostic = "AssetManager rejected generated MeshData";
            return false;
        }
        std::vector<asset::AssetID> material_ids;
        std::vector<asset::AssetID> texture_ids;
        if (!RegisterTerrainPreviewMaterial(*mesh->data, *heightfield,
                material_ids, texture_ids, diagnostic))
        {
            assets.UnRegisterAsset(mesh_id);
            return false;
        }
        std::uint64_t catalog_serial = 0;
        std::vector<asset::AssetID> replacement_roots{
            mesh_id, sky_dome_mesh_, sky_dome_material_};
        replacement_roots.insert(replacement_roots.end(), material_ids.begin(), material_ids.end());
        replacement_roots.insert(replacement_roots.end(), texture_ids.begin(), texture_ids.end());
        replacement_roots.push_back(environment_texture_);
        const runtime::RuntimeContext::StartupResult queued =
            runtime::global_runtime_context.QueueRenderAssetsReplacement(
                replacement_roots, catalog_serial, GetRenderEnvironmentSource());
        if (!queued)
        {
            for (const asset::AssetID id : material_ids) assets.UnRegisterAsset(id);
            for (const asset::AssetID id : texture_ids) assets.UnRegisterAsset(id);
            assets.UnRegisterAsset(mesh_id);
            diagnostic = queued.diagnostic;
            return false;
        }
        auto pending = std::make_unique<PreviewAssets>();
        pending->mesh = mesh_id;
        pending->material = material_ids.front();
        pending->materials = std::move(material_ids);
        pending->textures = std::move(texture_ids);
        pending->heightfield = std::move(heightfield);
        pending->evaluation = std::make_shared<EvaluationResult>(result);
        pending->catalog_serial = catalog_serial;
        pending->bounds = bounds;
        if (!preview_publication_.Begin(std::move(pending), catalog_serial))
        {
            for (const asset::AssetID id : pending->materials) assets.UnRegisterAsset(id);
            for (const asset::AssetID id : pending->textures) assets.UnRegisterAsset(id);
            if (pending->mesh.IsValid()) assets.UnRegisterAsset(pending->mesh);
            diagnostic = "could not begin terrain preview publication";
            return false;
        }
        generation_status_ = "Waiting for render boundary";
        return true;
    }

    void TerrainViewerHost::DiscardPendingPreview()
    {
        std::unique_ptr<PreviewAssets> pending = preview_publication_.Retire();
        if (!pending) return;
        asset::AssetManager &assets = asset::AssetManager::GetInstance();
        for (const asset::AssetID id : pending->materials)
            assets.UnRegisterAsset(id);
        for (const asset::AssetID id : pending->textures)
            assets.UnRegisterAsset(id);
        if (pending->mesh.IsValid())
            assets.UnRegisterAsset(pending->mesh);
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
        if (render_environment_handle_.IsValid() &&
            runtime::global_runtime_context.render_system_ != nullptr)
        {
            (void)runtime::global_runtime_context.render_system_
                ->GetEnvironmentSourceSink()->EnqueueDestroy(render_environment_handle_);
            render_environment_handle_ = {};
        }
        if (terrain_editor_ != nullptr)
        {
            terrain_editor_->Shutdown();
            terrain_editor_.reset();
        }
    }

    void TerrainViewerHost::Shutdown() noexcept
    {
        initialized_ = false;
        command_registrations_.clear();
        if (bake_command_completion_)
        {
            bake_command_completion_(runtime::command::CommandResult{
                runtime::command::CommandStatus::Cancelled,
                "Terrain viewer shut down during bake",
                bake_command_request_id_, {}});
            bake_command_completion_ = {};
            bake_command_request_id_ = 0;
        }
        generation_executor_.reset();
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
            if (sky_dome_actor_.IsValid())
            {
                (void)world.DestroyActor(sky_dome_actor_);
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
        sky_dome_actor_ = {};
        camera_actor_ = {};
        light_actor_ = {};
        if (generated_mesh_.IsValid())
        {
            asset::AssetManager::GetInstance().UnRegisterAsset(generated_mesh_);
            generated_mesh_ = {};
        }
        if (sky_dome_material_.IsValid())
        {
            asset::AssetManager::GetInstance().UnRegisterAsset(sky_dome_material_);
            sky_dome_material_ = {};
        }
        if (sky_dome_mesh_.IsValid())
        {
            asset::AssetManager::GetInstance().UnRegisterAsset(sky_dome_mesh_);
            sky_dome_mesh_ = {};
        }
        DiscardPendingPreview();
        for (const asset::AssetID id : terrain_material_assets_)
            asset::AssetManager::GetInstance().UnRegisterAsset(id);
        for (const asset::AssetID id : terrain_texture_assets_)
            asset::AssetManager::GetInstance().UnRegisterAsset(id);
        terrain_material_assets_.clear();
        terrain_texture_assets_.clear();
        preview_material_ = {};
        execution_control_.reset();
        operator_registry_.reset();
        recipe_.reset();
        committed_recipe_.reset();
        last_evaluation_.reset();
        preview_heightfield_.reset();
    }
}
