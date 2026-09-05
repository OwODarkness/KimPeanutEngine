#include "render_scene_coordinator.h"

#include <stdexcept>
#include <utility>

#include "asset/mesh.h"
#include "render/material/material_system.h"
#include "render_resource_resolver.h"

namespace kpengine::render
{
    void RenderSceneCoordinator::Bind(
        MaterialSystem &material_system, RenderResourceResolver &resource_resolver,
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets)
    {
        material_system_ = &material_system;
        resource_resolver_ = &resource_resolver;
        prepared_assets_ = std::move(prepared_assets);
        material_asset_resolver_ = std::make_unique<MaterialAssetResolver>(
            material_system, prepared_assets_);
        ApplyDefaultCamera();
    }

    RenderSceneFrameInput RenderSceneCoordinator::PrepareFrame(
        std::optional<CaptureView> pending_capture,
        std::optional<CaptureView> debug_view)
    {
        if (!material_system_ || !resource_resolver_ || !prepared_assets_ ||
            !material_asset_resolver_)
        {
            throw std::runtime_error("Render scene coordinator is not bound");
        }

        material_system_->RefreshResources();
        renderable_sources_.Drain(
            render_world_,
            [this](const PrimitiveRenderableSourceDesc &source)
            { return ResolveRenderableSource(source); });
        render_world_.ApplyPendingCommands();
        light_sources_.Drain(light_world_);
        camera_sources_.Drain();
        environment_sources_.Drain();

        const std::optional<CameraSourceDesc> active_camera =
            camera_sources_.GetActiveSource();
        if (!active_camera.has_value())
        {
            ApplyDefaultCamera();
        }
        else
        {
            const CameraSourceDesc &source = *active_camera;
            scene_camera_.SetPosition(source.world_transform.position_);
            scene_camera_.SetRotation(source.world_transform.rotator_);
            scene_camera_.SetProjectionMode(source.projection_mode);
            scene_camera_.SetFOV(source.field_of_view_degrees);
            scene_camera_.SetNearPlane(source.near_plane);
            scene_camera_.SetFarPlane(source.far_plane);
            scene_camera_.SetOrthographicHeight(source.orthographic_height);
        }

        return {
            render_world_,
            light_world_.Snapshot(),
            scene_camera_,
            environment_sources_.GetActiveSource(),
            environment_sources_.GetActiveHandle(),
            [this](ShadowHandle handle) { return light_sources_.IsShadowHandleValid(handle); },
            std::move(pending_capture),
            std::move(debug_view),
        };
    }

    void RenderSceneCoordinator::Clear()
    {
        camera_sources_.Clear();
        environment_sources_.Clear();
        renderable_sources_.Clear(render_world_);
        render_world_.ApplyPendingCommands();
        render_world_.Clear();
        light_sources_.Clear(light_world_);
        light_world_.Clear();
        if (material_asset_resolver_)
        {
            material_asset_resolver_->Clear();
            material_asset_resolver_.reset();
        }
        material_system_ = nullptr;
        resource_resolver_ = nullptr;
        prepared_assets_.reset();
    }

    RenderableSourceResolution RenderSceneCoordinator::ResolveRenderableSource(
        const PrimitiveRenderableSourceDesc &source)
    {
        const auto *const static_mesh = std::get_if<StaticMeshRenderableSourceDesc>(&source);
        if (static_mesh == nullptr)
        {
            return {RenderableSourceState::Failed, "unsupported renderable source variant", std::nullopt};
        }
        if (!static_mesh->mesh_asset.IsValid() ||
            static_mesh->mesh_asset.type != asset::AssetType::KPAT_Mesh)
        {
            return {RenderableSourceState::Failed, "static mesh source has an invalid mesh asset", std::nullopt};
        }

        MaterialInstanceHandle material_instance;
        const MaterialResolution material_resolution =
            ResolveMaterialAsset(static_mesh->material_asset, material_instance);
        if (material_resolution.state == MaterialResourceState::Failed)
        {
            return {RenderableSourceState::Failed, material_resolution.diagnostic, std::nullopt};
        }
        if (material_resolution.state != MaterialResourceState::Ready)
        {
            return {RenderableSourceState::Pending, material_resolution.diagnostic, std::nullopt};
        }

        const auto mesh = prepared_assets_->Get<asset::MeshResource>(static_mesh->mesh_asset);
        if (!mesh || !mesh->data)
        {
            return {RenderableSourceState::Pending, "mesh asset is not loaded", std::nullopt};
        }
        const graphics::MeshHandle mesh_handle =
            resource_resolver_->GetOrCreateMesh(static_mesh->mesh_asset, *mesh->data);
        if (!mesh_handle.IsValid())
        {
            return {RenderableSourceState::Failed, "mesh resource creation failed", std::nullopt};
        }

        MeshProxyDesc proxy_desc{};
        proxy_desc.mesh = mesh_handle;
        proxy_desc.material = material_instance;
        proxy_desc.world_transform = static_mesh->world_transform;
        proxy_desc.world_bounds = static_mesh->world_bounds;
        proxy_desc.flags = static_mesh->flags;
        proxy_desc.lod_bias = static_mesh->lod_bias;
        return {RenderableSourceState::Ready, {}, proxy_desc};
    }

    MaterialResolution RenderSceneCoordinator::ResolveMaterialAsset(
        asset::AssetID material_asset, MaterialInstanceHandle &out_instance)
    {
        if (!material_asset_resolver_)
        {
            return {MaterialResourceState::Pending, "material system is not initialized"};
        }
        return material_asset_resolver_->Resolve(material_asset, out_instance);
    }

    void RenderSceneCoordinator::ApplyDefaultCamera()
    {
        scene_camera_.SetPosition({0.f, 0.f, 300.f});
        scene_camera_.SetRotation({0.f, -90.f, 0.f});
        scene_camera_.SetProjectionMode(CameraProjectionMode::Perspective);
        scene_camera_.SetFOV(45.f);
        scene_camera_.SetNearPlane(1.f);
        scene_camera_.SetFarPlane(2000.f);
        scene_camera_.SetOrthographicHeight(10.f);
    }
}
