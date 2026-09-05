#include "gameplay/reflection/gameplay_reflection_internal.h"

#include "gameplay/component/mesh_component.h"

namespace kpengine::gameplay::reflection_detail
{
    namespace
    {
        template <typename T>
        bool GetVisible(const T &component) { return component.IsVisible(); }

        template <typename T>
        void SetVisible(T &component, bool value) { component.SetVisible(value); }

        template <typename T>
        void SetCastsShadow(T &component, bool value) { component.SetCastsShadow(value); }
    }

    ReflectionResult RegisterActorReflection(EnttReflectionRegistrar &registrar)
    {
        auto scene = registrar.Type<SceneComponent>("kpengine.gameplay.SceneComponent");
        if (!scene) return scene.GetResult();
        ReflectionResult result = RegisterTransformChannels(scene, true, true, true);
        if (!result) return result;

        auto mesh = registrar.Type<MeshComponent>("kpengine.gameplay.MeshComponent");
        if (!mesh) return mesh.GetResult();
        result = RegisterTransformChannels(mesh, true, true, true);
        if (!result) return result;

        #define KP_REFLECT_TRY(...) \
            do { const ReflectionResult property_result = (__VA_ARGS__); if (!property_result) return property_result; } while (false)
        KP_REFLECT_TRY(mesh.Property<&SetVisible<MeshComponent>, &GetVisible<MeshComponent>>(
            "render.visible", kEditable, Metadata("Visible", "Render")));
        KP_REFLECT_TRY(mesh.Property<&SetCastsShadow<MeshComponent>, &MeshComponent::CastsShadow>(
            "render.casts_shadow", kEditable, Metadata("Casts Shadow", "Render")));
        KP_REFLECT_TRY(mesh.Property<&MeshComponent::SetLodBias, &MeshComponent::GetLodBias>(
            "mesh.lod_bias", kEditable, Metadata("LOD Bias", "Mesh")));
        #undef KP_REFLECT_TRY
        return {};
    }
}
