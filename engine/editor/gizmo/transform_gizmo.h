#ifndef KPENGINE_EDITOR_GIZMO_TRANSFORM_GIZMO_H
#define KPENGINE_EDITOR_GIZMO_TRANSFORM_GIZMO_H

#include <array>
#include <cstdint>
#include <optional>

#include <imgui.h>

#include "base/handle.h"
#include "gameplay/actor/actor_types.h"
#include "graphics/backend/common/render_target.h"
#include "math/math_header.h"
#include "reflection/reflection_types.h"

namespace kpengine::render
{
    class RenderSystem;
}

namespace kpengine::editor
{
    class ActorEditorModel;

    // Editor-owned translate tool. It consumes immutable actor snapshots and
    // submits value-only edits; it never holds Gameplay component pointers.
    class EditorTransformGizmo final
    {
    public:
        enum class Handle : uint8_t
        {
            None,
            X,
            Y,
            Z,
        };

        EditorTransformGizmo(ActorEditorModel &model, render::RenderSystem &render_system);

        // Draws over the scene image and returns true when it consumed the
        // current left-mouse interaction, preventing scene picking underneath.
        bool Render(const ImVec2 &image_min, const ImVec2 &image_size,
                    const graphics::RenderTargetView &view);

    private:
        struct Channel
        {
            reflection::ReflectionPropertyId property;
            float value = 0.0f;
        };

        struct Target
        {
            gameplay::ActorHandle actor;
            gameplay::ComponentInstanceId component;
            reflection::ReflectionTypeId type;
            Vector3f location{};
            std::array<Channel, 3> channels{};
        };

        struct ProjectedAxis
        {
            ImVec2 end{};
            ImVec2 direction{};
            float length_pixels = 0.0f;
        };

        std::optional<Target> BuildTarget() const;
        std::optional<ImVec2> Project(const Vector3f &point,
                                      const ImVec2 &image_min,
                                      const ImVec2 &image_size,
                                      float aspect) const;
        Handle HitTest(const ImVec2 &mouse, const ImVec2 &pivot,
                       const std::array<ProjectedAxis, 3> &axes) const;
        void DrawAxis(ImDrawList &draw_list, const ImVec2 &pivot,
                      const ProjectedAxis &axis, ImU32 color, bool highlighted) const;
        bool Commit(const Target &target, float value);
        void ResetInteraction() noexcept;

        ActorEditorModel &model_;
        render::RenderSystem &render_system_;
        Handle active_handle_ = Handle::None;
        gameplay::ActorHandle active_actor_{};
        std::optional<Target> drag_target_;
        ImVec2 drag_start_mouse_{};
        ImVec2 drag_axis_direction_{};
        float drag_axis_pixels_ = 1.0f;
        float drag_start_value_ = 0.0f;
        float drag_delta_ = 0.0f;
        float drag_axis_length_ = 1.0f;
        float last_submitted_value_ = 0.0f;
        bool has_submitted_value_ = false;
        bool release_requested_ = false;
    };
}

#endif
