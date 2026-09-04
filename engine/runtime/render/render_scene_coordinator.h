#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SCENE_COORDINATOR_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SCENE_COORDINATOR_H

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "camera_source_registry.h"
#include "environment_source_registry.h"
#include "light/light_source_registry.h"
#include "light/light_world.h"
#include "material/material_asset_resolver.h"
#include "render_capture_service.h"
#include "render_camera.h"
#include "render_source_registry.h"
#include "render_world/render_world.h"

namespace kpengine::render
{
    class MaterialSystem;
    class RenderResourceResolver;

    struct RenderSceneFrameInput
    {
        const RenderWorld &render_world;
        std::vector<Light> lights;
        RenderCamera camera;
        std::optional<EnvironmentSourceDesc> environment;
        std::optional<EnvironmentSourceHandle> environment_handle;
        std::function<bool(ShadowHandle)> is_shadow_handle_valid;
        std::optional<CaptureView> pending_capture;
    };

    // Stable Render-owned coordinator for Gameplay source inboxes and the
    // resolved scene state consumed by renderer policy.
    class RenderSceneCoordinator final
    {
    public:
        RenderSceneCoordinator() = default;
        ~RenderSceneCoordinator() = default;
        RenderSceneCoordinator(const RenderSceneCoordinator &) = delete;
        RenderSceneCoordinator &operator=(const RenderSceneCoordinator &) = delete;

        void Bind(MaterialSystem &material_system, RenderResourceResolver &resource_resolver,
                  std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets);
        RenderSceneFrameInput PrepareFrame(std::optional<CaptureView> pending_capture);
        void Clear();

        IRenderableSourceSink *GetRenderableSourceSink() { return &renderable_sources_; }
        ILightSourceSink *GetLightSourceSink() { return &light_sources_; }
        ICameraSourceSink *GetCameraSourceSink() { return &camera_sources_; }
        IEnvironmentSourceSink *GetEnvironmentSourceSink() { return &environment_sources_; }

    private:
        RenderableSourceResolution ResolveRenderableSource(
            const PrimitiveRenderableSourceDesc &source);
        MaterialResolution ResolveMaterialAsset(asset::AssetID material_asset,
                                                 MaterialInstanceHandle &out_instance);
        void ApplyDefaultCamera();

        MaterialSystem *material_system_ = nullptr;
        RenderResourceResolver *resource_resolver_ = nullptr;
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets_;
        std::unique_ptr<MaterialAssetResolver> material_asset_resolver_;

        RenderableSourceRegistry renderable_sources_;
        RenderWorld render_world_;
        LightSourceRegistry light_sources_;
        LightWorld light_world_;
        CameraSourceRegistry camera_sources_;
        EnvironmentSourceRegistry environment_sources_;
        RenderCamera scene_camera_;
    };
}

#endif
