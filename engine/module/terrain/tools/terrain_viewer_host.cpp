#include "tools/terrain_viewer_host.h"

#include <algorithm>
#include <array>
#include <cmath>
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
#include "import/terrain_baker.h"

namespace kpengine::terrain
{
    namespace
    {
        bool BuildPreview(std::string &diagnostic, data::MeshData &mesh,
                          spatial::AABB &bounds,
                          std::shared_ptr<const ScalarField2D> &heightfield,
                          TerrainRecipe &recipe)
        {
#ifndef KPENGINE_TERRAIN_FIXTURE_DIR
#define KPENGINE_TERRAIN_FIXTURE_DIR ""
#endif
            const std::string path = std::string(KPENGINE_TERRAIN_FIXTURE_DIR) +
                                     "/random_128.terrainrecipe.json";
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
            TerrainEvaluator evaluator(registry);
            const EvaluationResult result = evaluator.Evaluate(recipe);
            if (!result.succeeded)
            {
                diagnostic = result.diagnostic;
                return false;
            }
            const auto node = recipe.nodes.empty() ? result.nodes.end() :
                result.nodes.find(recipe.nodes.back().id);
            if (node == result.nodes.end() || node->second.outputs.empty())
            {
                diagnostic = "terrain fixture did not produce a heightfield";
                return false;
            }
            heightfield = node->second.outputs.at("height");
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

        data::MeshData mesh_data;
        spatial::AABB bounds;
        recipe_ = std::make_unique<TerrainRecipe>();
        const std::filesystem::path material_settings_path =
            std::filesystem::path{GetAssetDirectory()} /
                "terrain/material/mountain_basin.terrainmaterial.json";
        if (!LoadTerrainMaterialSettings(material_settings_path, GetAssetDirectory(),
                                         terrain_material_settings_, diagnostic))
            return fail("could not load terrain PBR settings: " + diagnostic);
        if (!BuildPreview(diagnostic, mesh_data, bounds, preview_heightfield_, *recipe_))
        {
            return false;
        }
        AssignTerrainMaterialSections(mesh_data);
        if (!RegisterTerrainPreviewMaterials(terrain_material_settings_, *preview_heightfield_,
                GetAssetDirectory(),
                GetContentArchiveDirectory(), terrain_material_assets_, terrain_texture_assets_, diagnostic))
            return fail("could not prepare terrain PBR materials: " + diagnostic);
        operator_registry_ = std::make_shared<OperatorRegistry>();
        if (!operator_registry_->RegisterBuiltins(diagnostic)) return false;
        generation_executor_ = std::make_unique<GenerationExecutor>(
            operator_registry_, 1, 1, 1);
        execution_control_ = std::make_shared<EvaluationExecutionControl>();
        TerrainEvaluator initial_evaluator(operator_registry_);
        last_evaluation_ = std::make_unique<EvaluationResult>(
            initial_evaluator.Evaluate(*recipe_));
        committed_recipe_ = std::make_unique<TerrainRecipe>(*recipe_);
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
        render_roots_ = {generated_mesh_};
        render_roots_.insert(render_roots_.end(), terrain_material_assets_.begin(),
                             terrain_material_assets_.end());

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
        camera_desc.transform.position_ = {0.0f, 100.0f, 182.0f};
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
        if (!terrain_editor_->Initialize(engine, preview_heightfield_, diagnostic,
                [this](std::uint64_t seed, std::uint32_t lattice_size,
                       std::uint32_t octaves, float persistence, float lacunarity) {
                    RequestRegenerate(seed, lattice_size, octaves, persistence, lacunarity);
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
        return true;
    }

    bool TerrainViewerHost::RegisterHostCommands(
        runtime::command::CommandRegistry &registry, std::string &diagnostic)
    {
        using namespace runtime::command;
        CommandDesc regenerate{
            "terrain.regenerate", "TerrainViewer",
            "Generate and publish a seeded 128x128 terrain preview",
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
        if (pending_preview_ && pending_catalog_serial_ != 0 &&
            runtime::global_runtime_context.render_system_ &&
            runtime::global_runtime_context.render_system_->
                GetAppliedPreparedAssetsUpdate() >= pending_catalog_serial_)
        {
            gameplay::GameplayWorld *const world =
                runtime::global_runtime_context.gameplay_world_.get();
            if (world == nullptr)
            {
                diagnostic = "GameplayWorld disappeared during terrain preview replacement";
                return false;
            }
            if (terrain_actor_.IsValid()) (void)world->DestroyActor(terrain_actor_);
            if (camera_actor_.IsValid()) (void)world->DestroyActor(camera_actor_);
            if (light_actor_.IsValid()) (void)world->DestroyActor(light_actor_);
            world->ReclaimDestroyedActors();
            for (const asset::AssetID id : terrain_material_assets_)
                asset::AssetManager::GetInstance().UnRegisterAsset(id);
            for (const asset::AssetID id : terrain_texture_assets_)
                asset::AssetManager::GetInstance().UnRegisterAsset(id);
            terrain_material_assets_ = pending_preview_->materials;
            terrain_texture_assets_ = pending_preview_->textures;
            if (generated_mesh_.IsValid())
                asset::AssetManager::GetInstance().UnRegisterAsset(generated_mesh_);
            terrain_actor_ = {};
            generated_mesh_ = pending_preview_->mesh;
            preview_material_ = pending_preview_->material;
            preview_heightfield_ = pending_preview_->heightfield;
            committed_recipe_ = std::make_unique<TerrainRecipe>(*recipe_);
            gameplay::StaticMeshActorDesc desc{};
            desc.mesh_asset = generated_mesh_;
            desc.material_asset = preview_material_;
            desc.material_assets = terrain_material_assets_;
            data::MeshData mesh = BuildHeightfieldMesh(*preview_heightfield_);
            AssignTerrainMaterialSections(mesh);
            spatial::AABB bounds{{std::numeric_limits<float>::max(),
                                   std::numeric_limits<float>::max(),
                                   std::numeric_limits<float>::max()},
                                  {-std::numeric_limits<float>::max(),
                                   -std::numeric_limits<float>::max(),
                                   -std::numeric_limits<float>::max()}};
            for (const data::Vertex &vertex : mesh.vertices) bounds.ExpandToInclude(vertex.position);
            desc.local_bounds = bounds;
            terrain_actor_ = gameplay::CreateStaticMeshActor(*world, desc);
            if (!terrain_actor_.IsValid())
            {
                diagnostic = "could not create replacement terrain Actor";
                pending_preview_.reset();
                pending_catalog_serial_ = 0;
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
            camera_desc.far_plane = 2000.0f;
            camera_actor_ = gameplay::CreateCameraActor(*world, camera_desc);
            gameplay::DirectionalLightActorDesc light_desc{};
            light_desc.direction = {-0.4f, -1.0f, -0.3f};
            light_desc.intensity = 3.0f;
            light_actor_ = gameplay::CreateDirectionalLightActor(*world, light_desc);
            if (!camera_actor_.IsValid() || !light_actor_.IsValid())
            {
                diagnostic = "could not restore preview camera or light after catalog replacement";
                return false;
            }
            render_roots_ = {generated_mesh_};
            render_roots_.insert(render_roots_.end(), terrain_material_assets_.begin(),
                                 terrain_material_assets_.end());
            last_evaluation_ = pending_preview_->evaluation
                ? std::make_unique<EvaluationResult>(*pending_preview_->evaluation)
                : nullptr;
            if (terrain_editor_ && last_evaluation_)
                terrain_editor_->SetEvaluationSnapshot(preview_heightfield_,
                    *last_evaluation_, *recipe_, "Ready");
            pending_preview_.reset();
            pending_catalog_serial_ = 0;
            generation_status_ = "Ready";
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
            auto field = node->second.outputs.at("height");
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
        float persistence, float lacunarity)
    {
        std::lock_guard lock(authoring_command_mutex_);
        AuthoringCommand command;
        command.kind = AuthoringCommand::Kind::Regenerate;
        command.seed = seed;
        command.lattice_size = lattice_size;
        command.octaves = octaves;
        command.third = persistence;
        command.fourth = lacunarity;
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
                    command.third, command.fourth);
                break;
            case AuthoringCommand::Kind::Cancel:
                ApplyCancel();
                break;
            case AuthoringCommand::Kind::Control:
                ApplyExecutionControl(command.control);
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
        float persistence, float lacunarity)
    {
        if (pending_preview_)
        {
            generation_status_ = "Busy: waiting for the preview catalog swap";
            return;
        }
        if (!generation_executor_ || !recipe_ || !execution_control_ ||
            lattice_size == 0 || lattice_size > 64 || octaves == 0 || octaves > 8 ||
            !std::isfinite(persistence) || persistence < 0.0f || persistence > 1.0f ||
            !std::isfinite(lacunarity) || lacunarity < 1.0f || lacunarity > 8.0f)
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
    }

    bool TerrainViewerHost::PublishPreview(const EvaluationResult &result,
        std::shared_ptr<const ScalarField2D> heightfield, std::string &diagnostic)
    {
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
        if (!RegisterTerrainPreviewMaterials(terrain_material_settings_, *heightfield,
                GetAssetDirectory(), GetContentArchiveDirectory(), material_ids, texture_ids, diagnostic))
        {
            assets.UnRegisterAsset(mesh_id);
            return false;
        }
        std::uint64_t catalog_serial = 0;
        std::vector<asset::AssetID> replacement_roots{mesh_id};
        replacement_roots.insert(replacement_roots.end(), material_ids.begin(), material_ids.end());
        const runtime::RuntimeContext::StartupResult queued =
            runtime::global_runtime_context.QueueRenderAssetsReplacement(
                replacement_roots, catalog_serial);
        if (!queued)
        {
            for (const asset::AssetID id : material_ids) assets.UnRegisterAsset(id);
            for (const asset::AssetID id : texture_ids) assets.UnRegisterAsset(id);
            assets.UnRegisterAsset(mesh_id);
            diagnostic = queued.diagnostic;
            return false;
        }
        pending_preview_ = std::make_unique<PreviewAssets>();
        pending_preview_->mesh = mesh_id;
        pending_preview_->material = material_ids.front();
        pending_preview_->materials = std::move(material_ids);
        pending_preview_->textures = std::move(texture_ids);
        pending_preview_->heightfield = std::move(heightfield);
        pending_preview_->evaluation = std::make_shared<EvaluationResult>(result);
        pending_preview_->catalog_serial = catalog_serial;
        pending_catalog_serial_ = catalog_serial;
        generation_status_ = "Waiting for render boundary";
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
        if (generated_mesh_.IsValid())
        {
            asset::AssetManager::GetInstance().UnRegisterAsset(generated_mesh_);
            generated_mesh_ = {};
        }
        if (pending_preview_)
        {
            for (const asset::AssetID id : pending_preview_->materials)
                asset::AssetManager::GetInstance().UnRegisterAsset(id);
            for (const asset::AssetID id : pending_preview_->textures)
                asset::AssetManager::GetInstance().UnRegisterAsset(id);
            if (pending_preview_->mesh.IsValid())
                asset::AssetManager::GetInstance().UnRegisterAsset(pending_preview_->mesh);
            pending_preview_.reset();
        }
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
