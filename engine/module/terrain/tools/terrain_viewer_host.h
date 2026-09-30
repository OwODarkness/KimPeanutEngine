#ifndef KPENGINE_TERRAIN_TOOLS_TERRAIN_VIEWER_HOST_H
#define KPENGINE_TERRAIN_TOOLS_TERRAIN_VIEWER_HOST_H

#include <memory>
#include <mutex>
#include <string>
#include <deque>
#include <map>
#include <future>
#include <vector>

#include "host/application_host.h"
#include "command/command_registry.h"
#include "gameplay/actor/actor_types.h"
#include "evaluation/terrain_generation.h"
#include "import/terrain_baker.h"

namespace kpengine::terrain
{
    class ScalarField2D;
    class TerrainEditor;

    class TerrainViewerHost final : public runtime::IApplicationHost
    {
    public:
        TerrainViewerHost();
        ~TerrainViewerHost() override;

        const char *Name() const noexcept override { return "TerrainViewerHost"; }
        bool Initialize(runtime::Engine &engine, std::string &diagnostic) override;
        bool InitializePresentation(runtime::Engine &engine,
                                    std::string &diagnostic) override;
        bool Tick(float delta_time, std::string &diagnostic) override;
        bool RecordFrame(std::string &diagnostic) override;
        bool RenderPresentation(std::string &diagnostic) override;
        bool RegisterHostCommands(runtime::command::CommandRegistry &registry,
                                  std::string &diagnostic) override;
        std::vector<asset::AssetID> GetRenderAssetRoots() const override { return render_roots_; }
        void ShutdownRenderThread() noexcept override;
        void Shutdown() noexcept override;

    private:
        void RequestRegenerate(std::uint64_t seed, std::uint32_t lattice_size,
                               std::uint32_t octaves, float persistence, float lacunarity);
        void RequestCancel();
        void RequestExecutionControl(int command);
        void QueueCamera(float yaw_degrees, float pitch_degrees, float distance);
        void RequestBake();
        void ProcessAuthoringCommands();
        void ApplyRegenerate(std::uint64_t seed, std::uint32_t lattice_size,
                             std::uint32_t octaves, float persistence, float lacunarity);
        void ApplyCancel();
        void ApplyExecutionControl(int command);
        void ApplyBake();
        void UpdateCamera(float yaw_degrees, float pitch_degrees, float distance);
        bool PublishPreview(const EvaluationResult &result,
                            std::shared_ptr<const ScalarField2D> heightfield,
                            std::string &diagnostic);
        void RetirePreviousPreview();

        struct PreviewAssets
        {
            asset::AssetID mesh;
        asset::AssetID material;
        std::vector<asset::AssetID> materials;
        std::vector<asset::AssetID> textures;
            gameplay::ActorHandle actor;
            uint64_t catalog_serial = 0;
            std::shared_ptr<const ScalarField2D> heightfield;
            std::shared_ptr<EvaluationResult> evaluation;
        };
        struct AuthoringCommand
        {
            enum class Kind : std::uint8_t { Regenerate, Cancel, Control, Camera, Bake };
            Kind kind = Kind::Cancel;
            std::uint64_t seed = 0;
            float first = 0.0f;
            float second = 0.0f;
            float third = 0.0f;
            float fourth = 0.0f;
            std::uint32_t lattice_size = 4;
            std::uint32_t octaves = 4;
            int control = 0;
        };

        std::vector<asset::AssetID> render_roots_;
        std::vector<asset::AssetID> terrain_material_assets_;
        std::vector<asset::AssetID> terrain_texture_assets_;
        gameplay::ActorHandle terrain_actor_;
        gameplay::ActorHandle camera_actor_;
        gameplay::ActorHandle light_actor_;
        asset::AssetID generated_mesh_;
        asset::AssetID preview_material_;
        std::shared_ptr<const ScalarField2D> preview_heightfield_;
        std::shared_ptr<OperatorRegistry> operator_registry_;
        std::unique_ptr<GenerationExecutor> generation_executor_;
        std::shared_ptr<EvaluationExecutionControl> execution_control_;
        std::unique_ptr<TerrainRecipe> recipe_;
        std::unique_ptr<TerrainRecipe> committed_recipe_;
        std::unique_ptr<EvaluationResult> last_evaluation_;
        std::future<TerrainBakeResult> bake_future_;
        runtime::command::CommandCompletionSink bake_command_completion_;
        std::uint64_t bake_command_request_id_ = 0;
        std::vector<runtime::command::CommandRegistration> command_registrations_;
        bool bake_in_progress_ = false;
        std::uint64_t revision_ = 0;
        std::mutex progress_mutex_;
        std::map<std::string, NodeResult, std::less<>> progress_nodes_;
        std::uint64_t progress_revision_ = 0;
        std::mutex authoring_command_mutex_;
        std::deque<AuthoringCommand> authoring_commands_;
        std::uint64_t pending_catalog_serial_ = 0;
        std::unique_ptr<PreviewAssets> pending_preview_;
        std::vector<asset::AssetID> retired_asset_ids_;
        std::string generation_status_ = "Ready";
        float camera_target_y_ = 0.0f;
        float camera_yaw_degrees_ = -90.0f;
        float camera_pitch_degrees_ = -27.0f;
        float camera_distance_ = 440.0f;
        std::unique_ptr<TerrainEditor> terrain_editor_;
        bool initialized_ = false;
    };
}

#endif
