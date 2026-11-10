#include "editor_ui/ui_backend.h"
#include "ui_control_validation.h"

#include <stdexcept>
#include <utility>

namespace kpengine::editor::ui
{
    namespace
    {
        UiControl MakeControl(const UiControlDescriptor &descriptor, const UiRole role,
                              UiValue value, std::vector<UiActionKind> actions)
        {
            UiControl control{};
            control.role = role;
            control.label = descriptor.label;
            control.description = descriptor.description;
            control.enabled = descriptor.enabled;
            control.disabled_reason = descriptor.disabled_reason;
            control.value = std::move(value);
            control.actions = std::move(actions);
            return control;
        }

    }

    UiContext::UiContext(const UiSessionId session, IUiBackend &backend) noexcept
        : session_(session), backend_(backend)
    {
    }

    void UiContext::VerifyThread() const
    {
        if (std::this_thread::get_id() != owner_thread_)
            throw std::logic_error("UI context used from a different thread");
    }

    void UiContext::BeginFrame(const std::uint64_t frame, const UiFrameInfo &info)
    {
        VerifyThread();
        if (in_frame_)
            throw std::logic_error("UI frame already active");
        frame_ = frame;
        window_depth_ = 0;
        frame_ids_.clear();
        named_ids_.clear();
        UiFrameInfo frame_info = info;
        frame_info.session = session_;
        frame_info.frame = frame_;
        in_frame_ = true;
        try
        {
            backend_.BeginFrame(frame_info);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
    }

    bool UiContext::BeginWindow(const UiControl &window)
    {
        VerifyThread();
        if (!in_frame_ || window_depth_ != 0 || window.id == 0 ||
            frame_ids_.contains(window.id))
            throw std::logic_error("invalid UI window scope");
        bool visible = false;
        try
        {
            visible = backend_.BeginWindow(window);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        frame_ids_.insert(window.id);
        ++window_depth_;
        return visible;
    }

    UiContext::WindowScope UiContext::Window(const UiControl &window)
    {
        return WindowScope(*this, window);
    }

    void UiContext::EndWindow()
    {
        VerifyThread();
        if (!in_frame_ || window_depth_ != 1)
            throw std::logic_error("unbalanced UI window scope");
        try
        {
            backend_.EndWindow();
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        --window_depth_;
    }

    void UiContext::SetHostContainer(const UiControl &container)
    {
        VerifyThread();
        if (!in_frame_ || container.id == 0 || frame_ids_.contains(container.id))
            throw std::logic_error("invalid or duplicate host container ID");
        try
        {
            backend_.SetHostContainer(container);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        frame_ids_.insert(container.id);
    }

    UiNodeId UiContext::HashNamedId(const std::string_view name)
    {
        VerifyThread();
        if (!in_frame_ || name.empty())
            throw std::logic_error("named UI controls require an active frame and stable ID");
        return StableUiNodeId(name);
    }

    UiEditResult UiContext::Control(const UiControl &control)
    {
        VerifyThread();
        if (!in_frame_ || control.id == 0)
            throw std::logic_error("UI controls require an active frame and stable ID");
        return DispatchControl(control, std::nullopt);
    }

    UiEditResult UiContext::DispatchControl(
        const UiControl &control, const std::optional<std::string_view> named_id)
    {
        detail::ValidateControlInput(control);
        if (named_id)
        {
            const auto existing = named_ids_.find(control.id);
            if (existing != named_ids_.end() && existing->second != *named_id)
                throw std::logic_error("stable UI ID hash collision in frame");
        }
        if (frame_ids_.contains(control.id))
            throw std::logic_error("duplicate UI node ID in frame");
        if (named_id)
        {
            named_ids_.insert_or_assign(control.id, std::string{*named_id});
        }
        frame_ids_.insert(control.id);
        try
        {
            UiEditResult result = backend_.Control(control);
            detail::ValidateControlResult(control, result);
            return result;
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
    }

    UiEditResult UiContext::Button(const UiControlDescriptor &descriptor)
    {
        UiControl control = MakeControl(descriptor, UiRole::Button, false,
                                        {UiActionKind::Activate});
        control.id = HashNamedId(descriptor.id);
        return DispatchControl(control, descriptor.id);
    }

    UiTypedEditResult<bool> UiContext::Checkbox(const UiControlDescriptor &descriptor,
                                                 const bool checked)
    {
        UiControl control = MakeControl(descriptor, UiRole::CheckBox, checked,
                                        {UiActionKind::Toggle});
        control.id = HashNamedId(descriptor.id);
        const UiEditResult result = DispatchControl(control, descriptor.id);
        return MakeTypedEditResult<bool>(result);
    }

    UiTypedEditResult<bool> UiContext::Selectable(const UiControlDescriptor &descriptor,
                                                   const bool selected)
    {
        UiControl control = MakeControl(descriptor, UiRole::Selectable, selected,
                                        {UiActionKind::Select});
        control.id = HashNamedId(descriptor.id);
        const UiEditResult result = DispatchControl(control, descriptor.id);
        return MakeTypedEditResult<bool>(result);
    }

    UiEditResult UiContext::ComboValue(const UiControlDescriptor &descriptor, UiValue selected,
                                       std::vector<UiChoice> choices)
    {
        UiControl control = MakeControl(descriptor, UiRole::Combo, std::move(selected),
                                        {UiActionKind::Select});
        control.id = HashNamedId(descriptor.id);
        control.choices = std::move(choices);
        return DispatchControl(control, descriptor.id);
    }

    UiEditResult UiContext::NumericInputValue(const UiControlDescriptor &descriptor,
                                              UiValue value,
                                              std::optional<UiValue> minimum,
                                              std::optional<UiValue> maximum,
                                              std::optional<UiValue> step)
    {
        UiControl control = MakeControl(descriptor, UiRole::NumericInput, std::move(value),
                                        {UiActionKind::Edit, UiActionKind::Commit});
        control.id = HashNamedId(descriptor.id);
        control.minimum = std::move(minimum);
        control.maximum = std::move(maximum);
        control.step = std::move(step);
        return DispatchControl(control, descriptor.id);
    }

    UiTypedEditResult<std::string> UiContext::TextInput(const UiControlDescriptor &descriptor,
                                                        const std::string_view value)
    {
        UiControl control = MakeControl(descriptor, UiRole::TextInput, std::string{value},
                                        {UiActionKind::Edit, UiActionKind::Commit,
                                         UiActionKind::Cancel});
        control.id = HashNamedId(descriptor.id);
        return MakeTypedEditResult<std::string>(DispatchControl(control, descriptor.id));
    }

    void UiContext::Text(const std::string_view text)
    {
        VerifyThread();
        if (!in_frame_)
            throw std::logic_error("UI text requires an active frame");
        try
        {
            backend_.Text(text);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
    }

    UiFrameResult UiContext::EndFrame()
    {
        VerifyThread();
        if (!in_frame_ || window_depth_ != 0)
            throw std::logic_error("cannot finish an incomplete UI frame");
        try
        {
            UiFrameResult result = backend_.EndFrame(frame_);
            in_frame_ = false;
            return result;
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
    }

    void UiContext::AbortFrame() noexcept
    {
        if (!in_frame_)
            return;
        backend_.AbortFrame();
        in_frame_ = false;
        window_depth_ = 0;
        frame_ids_.clear();
        named_ids_.clear();
    }

    UiContext::WindowScope::WindowScope(UiContext &context, const UiControl &window)
        : context_(&context), visible_(context.BeginWindow(window))
    {
    }

    UiContext::WindowScope::~WindowScope() noexcept
    {
        if (context_ != nullptr)
        {
            try
            {
                context_->EndWindow();
            }
            catch (...)
            {
                context_->AbortFrame();
            }
        }
    }

    UiContext::WindowScope::WindowScope(WindowScope &&other) noexcept
        : context_(std::exchange(other.context_, nullptr)), visible_(other.visible_)
    {
    }

}
