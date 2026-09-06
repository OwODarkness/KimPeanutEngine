#include "editor/gizmo/transform_gizmo.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "editor/actor/actor_editor_model.h"
#include "gameplay/editor_bridge/gameplay_editor_bridge_types.h"
#include "reflection/i_reflection_catalog.h"
#include "runtime/render/render_system.h"

namespace
{
    constexpr float kAxisPixels = 76.0f;
    constexpr float kAxisHitPixels = 11.0f;
    constexpr float kMinWorldAxisLength = 0.05f;
    constexpr float kMaxWorldAxisLength = 100.0f;
    constexpr float kEpsilon = 0.0001f;

    constexpr ImU32 kAxisColors[3] = {
        IM_COL32(235, 72, 72, 255),
        IM_COL32(80, 220, 120, 255),
        IM_COL32(80, 145, 255, 255),
    };

    float Dot(const ImVec2 &lhs, const ImVec2 &rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y;
    }

    float Length(const ImVec2 &value)
    {
        return std::sqrt(Dot(value, value));
    }

    ImVec2 Normalize(const ImVec2 &value)
    {
        const float length = Length(value);
        return length > kEpsilon ? ImVec2(value.x / length, value.y / length) : ImVec2{};
    }

    float DistanceToSegment(const ImVec2 &point, const ImVec2 &start, const ImVec2 &end)
    {
        const ImVec2 segment{end.x - start.x, end.y - start.y};
        const float squared_length = Dot(segment, segment);
        if (squared_length <= kEpsilon)
        {
            return Length(ImVec2{point.x - start.x, point.y - start.y});
        }
        const ImVec2 offset{point.x - start.x, point.y - start.y};
        const float t = std::clamp(Dot(offset, segment) / squared_length, 0.0f, 1.0f);
        const ImVec2 closest{start.x + segment.x * t, start.y + segment.y * t};
        return Length(ImVec2{point.x - closest.x, point.y - closest.y});
    }

    int HandleIndex(kpengine::editor::EditorTransformGizmo::Handle handle)
    {
        switch (handle)
        {
        case kpengine::editor::EditorTransformGizmo::Handle::X:
            return 0;
        case kpengine::editor::EditorTransformGizmo::Handle::Y:
            return 1;
        case kpengine::editor::EditorTransformGizmo::Handle::Z:
            return 2;
        case kpengine::editor::EditorTransformGizmo::Handle::None:
        default:
            return -1;
        }
    }
}

namespace kpengine::editor
{
    EditorTransformGizmo::EditorTransformGizmo(ActorEditorModel &model,
                                               render::RenderSystem &render_system)
        : model_(model), render_system_(render_system)
    {
    }

    std::optional<EditorTransformGizmo::Target> EditorTransformGizmo::BuildTarget() const
    {
        const std::optional<gameplay::ActorHandle> selection = model_.GetSelection();
        if (!selection.has_value())
        {
            return std::nullopt;
        }

        const gameplay::ActorEditorSnapshot *const actor = model_.FindActor(*selection);
        const reflection::IReflectionCatalog *const catalog = model_.GetCatalog();
        if (actor == nullptr || catalog == nullptr)
        {
            return std::nullopt;
        }

        const std::optional<gameplay::ComponentInstanceId> root_id = actor->root_component;
        const gameplay::ComponentEditorSnapshot *component = nullptr;
        for (const gameplay::ComponentEditorSnapshot &candidate : actor->components)
        {
            if ((root_id.has_value() && candidate.component == *root_id) ||
                (!root_id.has_value() && candidate.is_root))
            {
                component = &candidate;
                break;
            }
        }
        if (component == nullptr)
        {
            return std::nullopt;
        }

        const reflection::ReflectionTypeDescriptor *const type =
            catalog->FindType(component->type);
        if (type == nullptr)
        {
            return std::nullopt;
        }

        constexpr std::array<const char *, 3> kLocationNames = {
            "transform.location.x", "transform.location.y", "transform.location.z"};
        Target target{};
        target.actor = actor->actor;
        target.component = component->component;
        target.type = component->type;
        for (std::size_t axis = 0; axis < kLocationNames.size(); ++axis)
        {
            const auto property = std::find_if(
                type->properties.begin(), type->properties.end(),
                [name = kLocationNames[axis]](const reflection::ReflectionPropertyDescriptor &candidate)
                { return candidate.name == name; });
            if (property == type->properties.end())
            {
                return std::nullopt;
            }
            const auto value = std::find_if(
                component->properties.begin(), component->properties.end(),
                [id = property->id](const gameplay::PropertyValueSnapshot &candidate)
                { return candidate.property == id && candidate.status == reflection::ReflectionResultStatus::Success; });
            if (value == component->properties.end())
            {
                return std::nullopt;
            }
            const std::optional<float> location =
                reflection::ConvertReflectionValue<float>(value->value);
            if (!location.has_value() || !std::isfinite(*location))
            {
                return std::nullopt;
            }
            target.channels[axis] = {property->id, *location};
            target.location[axis] = *location;
        }
        return target;
    }

    std::optional<ImVec2> EditorTransformGizmo::Project(
        const Vector3f &point, const ImVec2 &image_min, const ImVec2 &image_size,
        float aspect) const
    {
        const std::optional<Vector3f> ndc = render_system_.ProjectScenePoint(point, aspect);
        if (!ndc.has_value() || ndc->z_ < -1.0f || ndc->z_ > 1.0f)
        {
            return std::nullopt;
        }
        return ImVec2{image_min.x + (ndc->x_ + 1.0f) * 0.5f * image_size.x,
                      image_min.y + (1.0f - ndc->y_) * 0.5f * image_size.y};
    }

    EditorTransformGizmo::Handle EditorTransformGizmo::HitTest(
        const ImVec2 &mouse, const ImVec2 &pivot,
        const std::array<ProjectedAxis, 3> &axes) const
    {
        float closest = kAxisHitPixels;
        Handle result = Handle::None;
        for (std::size_t axis = 0; axis < axes.size(); ++axis)
        {
            const float distance = DistanceToSegment(mouse, pivot, axes[axis].end);
            if (distance < closest)
            {
                closest = distance;
                result = static_cast<Handle>(static_cast<uint8_t>(Handle::X) + axis);
            }
        }
        return result;
    }

    void EditorTransformGizmo::DrawAxis(ImDrawList &draw_list, const ImVec2 &pivot,
                                        const ProjectedAxis &axis, ImU32 color,
                                        bool highlighted) const
    {
        const float shaft_length = std::max(0.0f, axis.length_pixels - 11.0f);
        const ImVec2 shaft_end{pivot.x + axis.direction.x * shaft_length,
                               pivot.y + axis.direction.y * shaft_length};
        const ImVec2 perpendicular{-axis.direction.y, axis.direction.x};
        const ImVec2 base{axis.end.x - axis.direction.x * 10.0f,
                          axis.end.y - axis.direction.y * 10.0f};
        const ImVec2 left{base.x + perpendicular.x * 5.0f, base.y + perpendicular.y * 5.0f};
        const ImVec2 right{base.x - perpendicular.x * 5.0f, base.y - perpendicular.y * 5.0f};
        if (highlighted)
        {
            draw_list.AddLine(pivot, shaft_end, IM_COL32(255, 245, 190, 255), 7.0f);
        }
        draw_list.AddLine(pivot, shaft_end, color, highlighted ? 4.5f : 3.5f);
        draw_list.AddTriangleFilled(axis.end, left, right, color);
    }

    bool EditorTransformGizmo::Commit(const Target &target, float value)
    {
        const int axis = HandleIndex(active_handle_);
        if (axis < 0 || std::fabs(drag_delta_) <= kEpsilon)
        {
            return false;
        }

        const ActorEditorPropertyKey key{target.actor, target.component,
                                         target.channels[axis].property};
        if (model_.IsPropertyPending(key))
        {
            return false;
        }

        const gameplay::PropertyEditSubmission submission = model_.SubmitPropertyEdit(
            target.actor, target.component, target.type, target.channels[axis].property,
            reflection::ReflectionValue{static_cast<double>(value)});
        if (!submission.IsQueued())
        {
            return false;
        }

        last_submitted_value_ = value;
        has_submitted_value_ = true;
        return true;
    }

    void EditorTransformGizmo::ResetInteraction() noexcept
    {
        active_handle_ = Handle::None;
        active_actor_ = {};
        drag_target_.reset();
        drag_start_mouse_ = {};
        drag_axis_direction_ = {};
        drag_axis_pixels_ = 1.0f;
        drag_start_value_ = 0.0f;
        drag_delta_ = 0.0f;
        drag_axis_length_ = 1.0f;
        last_submitted_value_ = 0.0f;
        has_submitted_value_ = false;
        release_requested_ = false;
    }

    bool EditorTransformGizmo::Render(const ImVec2 &image_min, const ImVec2 &image_size,
                                      const graphics::RenderTargetView &view)
    {
        if (image_size.x <= 0.0f || image_size.y <= 0.0f || !view.IsValid())
        {
            ResetInteraction();
            return false;
        }

        const std::optional<Target> target = BuildTarget();
        if (!target.has_value())
        {
            ResetInteraction();
            return false;
        }
        if (active_handle_ != Handle::None &&
            (!drag_target_.has_value() || !(active_actor_ == target->actor) ||
             drag_target_->component != target->component ||
             drag_target_->type != target->type))
        {
            ResetInteraction();
        }

        const bool was_active = active_handle_ != Handle::None;

        Vector3f display_location =
            drag_target_.has_value() && active_handle_ != Handle::None
                ? drag_target_->location
                : target->location;
        const int active_axis = HandleIndex(active_handle_);
        if (active_axis >= 0)
        {
            const Vector3f direction = active_axis == 0
                                           ? Vector3f{1.0f, 0.0f, 0.0f}
                                           : active_axis == 1 ? Vector3f{0.0f, 1.0f, 0.0f}
                                                              : Vector3f{0.0f, 0.0f, 1.0f};
            display_location += direction * drag_delta_;
        }

        const float aspect = image_size.x / image_size.y;
        const std::optional<ImVec2> pivot = Project(display_location, image_min, image_size, aspect);
        if (!pivot.has_value())
        {
            return active_handle_ != Handle::None;
        }

        const std::array<Vector3f, 3> directions = {
            Vector3f{1.0f, 0.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, Vector3f{0.0f, 0.0f, 1.0f}};
        float world_length = 1.0f;
        if (const std::optional<ImVec2> one_unit =
                Project(display_location + directions[0], image_min, image_size, aspect))
        {
            const float pixels_per_world = Length(ImVec2{one_unit->x - pivot->x,
                                                         one_unit->y - pivot->y});
            if (pixels_per_world > kEpsilon)
            {
                world_length = std::clamp(kAxisPixels / pixels_per_world,
                                          kMinWorldAxisLength, kMaxWorldAxisLength);
            }
        }
        if (active_axis >= 0 && drag_axis_length_ > kEpsilon)
        {
            world_length = drag_axis_length_;
        }

        std::array<ProjectedAxis, 3> axes{};
        for (std::size_t axis = 0; axis < directions.size(); ++axis)
        {
            const std::optional<ImVec2> endpoint =
                Project(display_location + directions[axis] * world_length,
                        image_min, image_size, aspect);
            if (!endpoint.has_value())
            {
                return active_handle_ != Handle::None;
            }
            const ImVec2 delta{endpoint->x - pivot->x, endpoint->y - pivot->y};
            axes[axis] = {*endpoint, Normalize(delta), Length(delta)};
        }

        ImGuiIO &io = ImGui::GetIO();
        const bool mouse_in_image = io.MousePos.x >= image_min.x &&
                                    io.MousePos.x <= image_min.x + image_size.x &&
                                    io.MousePos.y >= image_min.y &&
                                    io.MousePos.y <= image_min.y + image_size.y;
        const Handle hovered = mouse_in_image ? HitTest(io.MousePos, *pivot, axes) : Handle::None;

        if (active_handle_ == Handle::None && hovered != Handle::None &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            active_handle_ = hovered;
            active_actor_ = target->actor;
            drag_target_ = *target;
            drag_start_mouse_ = io.MousePos;
            const int axis = HandleIndex(active_handle_);
            drag_start_value_ = target->channels[axis].value;
            drag_delta_ = 0.0f;
            drag_axis_length_ = world_length;
            drag_axis_direction_ = axes[axis].direction;
            drag_axis_pixels_ = axes[axis].length_pixels;
            last_submitted_value_ = drag_start_value_;
            has_submitted_value_ = false;
            release_requested_ = false;
        }

        if (active_handle_ != Handle::None)
        {
            const int axis = HandleIndex(active_handle_);
            if (!release_requested_)
            {
                const ImVec2 mouse_delta{io.MousePos.x - drag_start_mouse_.x,
                                         io.MousePos.y - drag_start_mouse_.y};
                drag_delta_ = Dot(mouse_delta, drag_axis_direction_) /
                              std::max(drag_axis_pixels_, 1.0f) * drag_axis_length_;
            }

            // The editor bridge is intentionally asynchronous. Submit the latest value
            // whenever the previous value has been consumed by Runtime, so the rendered
            // mesh follows the drag without allowing an unbounded command queue.
            if (drag_target_.has_value() && std::fabs(drag_delta_) > kEpsilon)
            {
                const float desired_value = drag_start_value_ + drag_delta_;
                if (!has_submitted_value_ ||
                    std::fabs(desired_value - last_submitted_value_) > kEpsilon)
                {
                    (void)Commit(*drag_target_, desired_value);
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                release_requested_ = true;
            }

            if (release_requested_ && drag_target_.has_value())
            {
                const float final_value = drag_start_value_ + drag_delta_;
                const int final_axis = HandleIndex(active_handle_);
                const ActorEditorPropertyKey key{
                    drag_target_->actor, drag_target_->component,
                    drag_target_->channels[final_axis].property};
                if (std::fabs(drag_delta_) <= kEpsilon ||
                    (!model_.IsPropertyPending(key) &&
                     has_submitted_value_ &&
                     std::fabs(final_value - last_submitted_value_) <= kEpsilon))
                {
                    ResetInteraction();
                }
            }
        }

        ImDrawList *const draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(ImVec2{pivot->x - 6.0f, pivot->y - 6.0f},
                                 ImVec2{pivot->x + 6.0f, pivot->y + 6.0f},
                                 IM_COL32(35, 35, 42, 230), 2.0f);
        draw_list->AddRect(ImVec2{pivot->x - 6.0f, pivot->y - 6.0f},
                           ImVec2{pivot->x + 6.0f, pivot->y + 6.0f},
                           IM_COL32(245, 245, 245, 230), 2.0f);
        for (std::size_t axis = 0; axis < axes.size(); ++axis)
        {
            const Handle handle = static_cast<Handle>(static_cast<uint8_t>(Handle::X) + axis);
            DrawAxis(*draw_list, *pivot, axes[axis], kAxisColors[axis],
                     handle == hovered || handle == active_handle_);
        }
        return was_active || active_handle_ != Handle::None || hovered != Handle::None;
    }
}
