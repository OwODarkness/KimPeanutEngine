#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_WORLD_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_WORLD_H

#include <cstdint>
#include <array>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "render/light/light_source.h"

namespace kpengine::render
{
    struct LightTag
    {
    };

    // Render-private identity for a resolved light record. Gameplay retains a
    // separate LightSourceHandle and never observes this handle.
    using LightHandle = Handle<LightTag>;

    struct ShadowTag
    {
    };

    // Render-private identity for a scheduled shadow job. It never becomes an
    // Asset texture or a Gameplay component property.
    using ShadowHandle = Handle<ShadowTag>;

    enum class LightType : uint8_t
    {
        Directional,
        Point,
        Spot,
    };

    enum class ShadowKind : uint8_t
    {
        Directional2D,
        Spot2D,
        PointCube,
    };

    struct DirectionalLightData
    {
        Vector3f direction{0.0f, -1.0f, 0.0f};
    };

    struct PointLightData
    {
        Vector3f position{};
        float range = 1.0f;
    };

    struct SpotLightData
    {
        Vector3f position{};
        Vector3f direction{0.0f, -1.0f, 0.0f};
        float range = 1.0f;
        float inner_cone_radians = 0.0f;
        float outer_cone_radians = 0.785398163f;
    };

    struct PointShadowFaceDesc
    {
        Vector3f direction;
        Vector3f up;
        uint32_t tile_x = 0;
        uint32_t tile_y = 0;
    };

    // Canonical six-face order used by both the atlas producer and shader
    // consumer: +X, -X, +Y, -Y, +Z, -Z.
    const std::array<PointShadowFaceDesc, 6> &GetPointShadowFaceTable();
    uint32_t SelectPointShadowFace(const Vector3f &light_to_receiver);

    using LightTypeData = std::variant<DirectionalLightData, PointLightData, SpotLightData>;

    struct LightDesc
    {
        LightType type = LightType::Directional;
        Vector3f color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        bool enabled = true;
        uint32_t layer_mask = ~uint32_t{0};
        std::optional<ShadowHandle> shadow;
        LightTypeData type_data{DirectionalLightData{}};
    };

    // A job is a Render scheduling description, not a depth target. D4 creates
    // target allocation and pass scheduling from these typed values.
    struct ShadowJobDesc
    {
        LightHandle source_light;
        ShadowKind kind = ShadowKind::Directional2D;
        uint32_t resolution = 0;
        uint32_t binding_slot = 0;
    };

    struct Light
    {
        LightHandle handle;
        LightDesc desc;
    };

    bool IsLightDescValid(const LightDesc &desc);
    bool IsShadowKindCompatible(LightType light_type, ShadowKind shadow_kind);
    bool IsShadowJobDescValid(const ShadowJobDesc &desc);

    // Render owns resolved light records and mutates them only at the render
    // frame boundary. Snapshot returns values so passes cannot observe mutable
    // Gameplay component state.
    class LightWorld
    {
    public:
        LightHandle EnqueueCreate(const LightDesc &desc);
        bool EnqueueUpdate(LightHandle handle, const LightDesc &desc);
        bool EnqueueDestroy(LightHandle handle);

        void ApplyPendingCommands();
        std::vector<Light> Snapshot() const;
        std::optional<Light> Find(LightHandle handle) const;
        bool IsRegistered(LightHandle handle) const;
        void Clear();

    private:
        bool IsHandleRegistered(LightHandle handle) const;

        mutable std::mutex mutex_;
        HandleSystem<LightHandle> handles_;
        std::unordered_map<uint32_t, Light> lights_;
        struct CreateLightCommand
        {
            LightHandle handle;
            LightDesc desc;
        };
        struct UpdateLightCommand
        {
            LightHandle handle;
            LightDesc desc;
        };
        struct DestroyLightCommand
        {
            LightHandle handle;
        };
        using LightCommand = std::variant<CreateLightCommand, UpdateLightCommand, DestroyLightCommand>;
        std::vector<LightCommand> pending_commands_;
    };
}

#endif
