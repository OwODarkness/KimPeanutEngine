#include "editor_ui/ui_recording_backend.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace kpengine::editor::ui
{
    UiRecordingBackend::UiRecordingBackend(std::vector<UiScriptedInput> input)
        : input_(std::move(input))
    {
    }

    bool UiRecordingBackend::Initialize(const UiViewportInfo &)
    {
        initialized_ = true;
        return true;
    }

    void UiRecordingBackend::BeginFrame(const UiFrameInfo &frame)
    {
        if (!initialized_ || in_frame_)
            throw std::logic_error("recording backend frame state is invalid");
        session_ = frame.session;
        frame_ = frame.frame;
        for (UiScriptedInput &input : input_)
        {
            if (input.session == 0 && input.frame == 0)
            {
                input.session = session_;
                input.frame = frame_;
            }
            if (input.session != session_ || input.frame < frame_)
                throw std::logic_error("scripted input expired before it was consumed");
        }
        operations_.clear();
        pending_observations_.clear();
        window_open_ = false;
        parent_ = 0;
        in_frame_ = true;
    }

    bool UiRecordingBackend::BeginWindow(const UiControl &window)
    {
        if (!in_frame_ || window_open_)
            throw std::logic_error("recording backend supports one root window scope");
        window_open_ = true;
        parent_ = window.id;
        operations_.push_back({UiOperation::Kind::BeginWindow, window, {}});
        pending_observations_.push_back({session_, frame_, window.id, 0, window.role, window.label,
                                 window.description, window.value, window.enabled,
                                 window.disabled_reason, false, false, false, {},
                                 UiGeometryProvenance::Unavailable});
        return true;
    }

    void UiRecordingBackend::EndWindow()
    {
        if (!window_open_)
            throw std::logic_error("recording window scope is not open");
        operations_.push_back({UiOperation::Kind::EndWindow, {}, {}});
        parent_ = 0;
        window_open_ = false;
    }

    void UiRecordingBackend::SetHostContainer(const UiControl &container)
    {
        if (!in_frame_ || container.id == 0)
            throw std::logic_error("host container requires an active frame and stable ID");
        parent_ = container.id;
        pending_observations_.push_back({session_, frame_, container.id, 0, container.role,
                                 container.label, container.description, container.value,
                                 container.enabled, container.disabled_reason});
    }

    UiEditResult UiRecordingBackend::Control(const UiControl &control)
    {
        if (!in_frame_ || parent_ == 0)
            throw std::logic_error("recorded controls must be inside a host container");
        operations_.push_back({UiOperation::Kind::Control, control, {}});
        UiEditResult result{control.value, false, false, false};
        for (auto it = input_.begin(); it != input_.end(); ++it)
        {
            if (it->session != session_ || it->frame != frame_ || it->id != control.id)
                continue;
            if (!control.enabled)
                throw std::logic_error("scripted input targeted a disabled control");
            if (it->value.index() != control.value.index())
                throw std::logic_error("scripted input type does not match control value");
            if (!control.actions.empty() &&
                std::find(control.actions.begin(), control.actions.end(), it->action) ==
                    control.actions.end())
                throw std::logic_error("scripted action is not supported by this control");
            result.value = std::move(it->value);
            result.action = it->action;
            if (it->action == UiActionKind::Activate)
            {
                if (control.role != UiRole::Button)
                    throw std::logic_error("activate input requires a button control");
                result.activated = true;
            }
            else if (it->action == UiActionKind::Toggle)
            {
                if (control.role != UiRole::CheckBox)
                    throw std::logic_error("toggle input requires a checkbox control");
                result.changed = result.value != control.value;
                result.committed = result.changed;
            }
            else if (it->action == UiActionKind::Cancel)
            {
                result.value = control.value;
                result.cancelled = true;
            }
            else if (it->action == UiActionKind::Commit)
            {
                result.changed = result.value != control.value;
                result.committed = true;
            }
            else if (it->action == UiActionKind::Select)
            {
                if (control.role == UiRole::Selectable)
                {
                    result.value = true;
                    result.changed = !std::get<bool>(control.value);
                    result.committed = result.activated = true;
                }
                else if (control.role == UiRole::Combo)
                {
                    result.changed = result.value != control.value;
                    result.committed = result.activated = true;
                }
                else
                {
                    throw std::logic_error("select input requires selectable or combo control");
                }
            }
            else
            {
                result.changed = result.value != control.value;
            }
            input_.erase(it);
            break;
        }
        UiNodeObservation observation{session_, frame_, control.id, parent_, control.role,
                                      control.label, control.description, result.value,
                                      control.enabled, control.disabled_reason};
        observation.actions = control.actions;
        observation.last_action = result.action;
        if (const auto bounds = fixture_bounds_.find(control.id); bounds != fixture_bounds_.end())
        {
            observation.bounds = bounds->second;
            observation.geometry = UiGeometryProvenance::TestFixture;
        }
        pending_observations_.push_back(std::move(observation));
        return result;
    }

    void UiRecordingBackend::Text(const std::string_view text)
    {
        if (!in_frame_)
            throw std::logic_error("recorded text requires active frame");
        operations_.push_back({UiOperation::Kind::Text, {}, std::string{text}});
    }

    UiFrameResult UiRecordingBackend::EndFrame(const std::uint64_t frame)
    {
        if (!in_frame_ || window_open_)
            throw std::logic_error("recording frame contains unclosed scopes");
        if (std::any_of(input_.begin(), input_.end(), [this](const UiScriptedInput &input)
            { return input.session == session_ && input.frame == frame_; }))
            throw std::logic_error("scripted input did not match a control in this frame");
        in_frame_ = false;
        observations_.swap(pending_observations_);
        return {frame, true, false, false, observations_};
    }

    void UiRecordingBackend::AbortFrame() noexcept
    {
        in_frame_ = false;
        window_open_ = false;
        parent_ = 0;
        pending_observations_.clear();
    }

    void UiRecordingBackend::Shutdown() noexcept
    {
        in_frame_ = false;
        window_open_ = false;
        initialized_ = false;
    }

    std::vector<UiNodeObservation> UiRecordingBackend::Observations() const
    {
        return observations_;
    }

    void UiRecordingBackend::SetFixtureBounds(const UiNodeId id, const UiRect bounds)
    {
        fixture_bounds_.insert_or_assign(id, bounds);
    }

    void UiRecordingBackend::QueueInput(const UiSessionId session,
                                        const std::uint64_t frame,
                                        const UiNodeId id, UiValue value,
                                        const UiActionKind action)
    {
        if (session == 0 || frame == 0 || id == 0)
            throw std::invalid_argument("scripted input requires session, frame and node IDs");
        input_.emplace_back(session, frame, id, std::move(value), action);
    }
}
