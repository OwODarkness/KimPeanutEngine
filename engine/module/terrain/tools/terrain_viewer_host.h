#ifndef KPENGINE_TERRAIN_TOOLS_TERRAIN_VIEWER_HOST_H
#define KPENGINE_TERRAIN_TOOLS_TERRAIN_VIEWER_HOST_H

#include <memory>
#include <vector>

#include "host/application_host.h"
#include "gameplay/actor/actor_types.h"

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
        std::vector<asset::AssetID> GetRenderAssetRoots() const override { return render_roots_; }
        void ShutdownRenderThread() noexcept override;
        void Shutdown() noexcept override;

    private:
        std::vector<asset::AssetID> render_roots_;
        gameplay::ActorHandle terrain_actor_;
        gameplay::ActorHandle camera_actor_;
        gameplay::ActorHandle light_actor_;
        asset::AssetID generated_mesh_;
        asset::AssetID preview_material_;
        std::shared_ptr<const ScalarField2D> preview_heightfield_;
        std::unique_ptr<TerrainEditor> terrain_editor_;
        bool initialized_ = false;
    };
}

#endif
