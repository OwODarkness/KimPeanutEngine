#include "editor_ui/ui_backend.h"
#include "ui_control_validation.h"

#include <stdexcept>
#include <cmath>
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
            control.hint = descriptor.hint;
            control.width = descriptor.width;
            control.height = descriptor.height;
            control.text_role = descriptor.text_role;
            control.emphasized = descriptor.emphasized;
            control.span_row = descriptor.span_row;
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
        frame_info_ = info;
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

    bool UiContext::BeginWindow(const UiControl &window, bool &open,
                                const UiVec2 first_use_position,
                                const UiVec2 first_use_size)
    {
        VerifyThread();
        if (!in_frame_ || window_depth_ != 0 || window.id == 0 ||
            frame_ids_.contains(window.id))
            throw std::logic_error("invalid UI window scope");
        bool visible = false;
        try
        {
            visible = backend_.BeginWindow(window, open, first_use_position, first_use_size);
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

    bool UiContext::BeginWindow(const UiControl &window,
                                const UiVec2 first_use_position,
                                const UiVec2 first_use_size)
    {
        VerifyThread();
        if (!in_frame_ || window_depth_ != 0 || window.id == 0 ||
            frame_ids_.contains(window.id) || !std::isfinite(first_use_position.x) ||
            !std::isfinite(first_use_position.y) || !std::isfinite(first_use_size.x) ||
            !std::isfinite(first_use_size.y) || first_use_size.x < 0.0F ||
            first_use_size.y < 0.0F)
            throw std::logic_error("invalid UI window scope or geometry");
        bool visible = false;
        try
        {
            visible = backend_.BeginWindow(window, first_use_position, first_use_size);
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

    UiContext::WindowScope UiContext::Window(const UiControlDescriptor &descriptor,
                                              const UiVec2 first_use_position,
                                              const UiVec2 first_use_size)
    {
        const UiNodeId id = HashNamedId(descriptor.id);
        const auto existing = named_ids_.find(id);
        if (existing != named_ids_.end() && existing->second != descriptor.id)
            throw std::logic_error("stable UI ID hash collision in frame");
        named_ids_.insert_or_assign(id, std::string{descriptor.id});
        UiControl window{};
        window.id = id;
        window.role = UiRole::Window;
        window.label = descriptor.label;
        window.description = descriptor.description;
        window.enabled = descriptor.enabled;
        window.disabled_reason = descriptor.disabled_reason;
        return WindowScope(*this, window, first_use_position, first_use_size);
    }

    UiContext::WindowScope UiContext::Window(const UiControlDescriptor &descriptor,
                                              bool &open,
                                              const UiVec2 first_use_position,
                                              const UiVec2 first_use_size)
    {
        const UiNodeId id = HashNamedId(descriptor.id);
        const auto existing = named_ids_.find(id);
        if (existing != named_ids_.end() && existing->second != descriptor.id)
            throw std::logic_error("stable UI ID hash collision in frame");
        named_ids_.insert_or_assign(id, std::string{descriptor.id});
        UiControl window{};
        window.id = id;
        window.role = UiRole::Window;
        window.label = descriptor.label;
        window.description = descriptor.description;
        window.enabled = descriptor.enabled;
        window.disabled_reason = descriptor.disabled_reason;
        return WindowScope(*this, window, open, first_use_position, first_use_size);
    }

    UiContext::TabBarScope UiContext::TabBar(const UiControlDescriptor &descriptor)
    {
        const UiNodeId id = HashNamedId(descriptor.id);
        const auto existing = named_ids_.find(id);
        if (existing != named_ids_.end() && existing->second != descriptor.id)
            throw std::logic_error("stable UI ID hash collision in frame");
        named_ids_.insert_or_assign(id, std::string{descriptor.id});
        UiControl control{};
        control.id = id;
        control.role = UiRole::Group;
        control.label = descriptor.label;
        control.description = descriptor.description;
        return TabBarScope(*this, control);
    }

    UiContext::TabItemScope UiContext::TabItem(const UiControlDescriptor &descriptor)
    {
        const UiNodeId id = HashNamedId(descriptor.id);
        const auto existing = named_ids_.find(id);
        if (existing != named_ids_.end() && existing->second != descriptor.id)
            throw std::logic_error("stable UI ID hash collision in frame");
        named_ids_.insert_or_assign(id, std::string{descriptor.id});
        UiControl control{};
        control.id = id;
        control.role = UiRole::Group;
        control.label = descriptor.label;
        control.description = descriptor.description;
        return TabItemScope(*this, control);
    }

    bool UiContext::BeginTabBar(const UiControl &tab_bar)
    {
        VerifyThread();
        if (!in_frame_ || window_depth_ != 1 || tab_bar_depth_ != 0 ||
            tab_item_depth_ != 0 || tab_bar.id == 0 || frame_ids_.contains(tab_bar.id))
            throw std::logic_error("invalid UI tab bar scope");
        bool visible = false;
        try
        {
            visible = backend_.BeginTabBar(tab_bar);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        frame_ids_.insert(tab_bar.id);
        if (visible) ++tab_bar_depth_;
        return visible;
    }

    void UiContext::EndTabBar()
    {
        VerifyThread();
        if (!in_frame_ || tab_bar_depth_ != 1 || tab_item_depth_ != 0)
            throw std::logic_error("unbalanced UI tab bar scope");
        try
        {
            backend_.EndTabBar();
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        --tab_bar_depth_;
    }

    bool UiContext::BeginTabItem(const UiControl &tab_item)
    {
        VerifyThread();
        if (!in_frame_ || tab_bar_depth_ != 1 || tab_item_depth_ != 0 ||
            tab_item.id == 0 || frame_ids_.contains(tab_item.id))
            throw std::logic_error("invalid UI tab item scope");
        bool visible = false;
        try
        {
            visible = backend_.BeginTabItem(tab_item);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        frame_ids_.insert(tab_item.id);
        if (visible) ++tab_item_depth_;
        return visible;
    }

    void UiContext::EndTabItem()
    {
        VerifyThread();
        if (!in_frame_ || tab_item_depth_ != 1)
            throw std::logic_error("unbalanced UI tab item scope");
        try
        {
            backend_.EndTabItem();
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        --tab_item_depth_;
    }

    UiContext::ThemeScope UiContext::Theme(const UiTheme &theme)
    {
        return ThemeScope(*this, theme);
    }

    void UiContext::PushTheme(const UiTheme &theme)
    {
        VerifyThread();
        if (!in_frame_)
            throw std::logic_error("UI theme requires an active frame");
        try
        {
            backend_.PushTheme(theme);
            ++theme_depth_;
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
    }

    void UiContext::PopTheme() noexcept
    {
        if (!in_frame_ || theme_depth_ == 0)
            return;
        try
        {
            backend_.PopTheme();
            --theme_depth_;
        }
        catch (...)
        {
            AbortFrame();
        }
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

    UiCanvasResult UiContext::Canvas(const UiControlDescriptor &descriptor,
                                     const UiVec2 size,
                                     std::vector<UiCanvasPrimitive> primitives)
    {
        VerifyThread();
        if (!in_frame_ || !std::isfinite(size.x) || !std::isfinite(size.y) ||
            size.x < 0.0F || size.y < 0.0F)
            throw std::logic_error("UI canvas requires an active frame and non-negative extent");
        detail::ValidateCanvas(size, primitives);
        UiControl control{};
        control.id = HashNamedId(descriptor.id);
        control.role = UiRole::Canvas;
        control.label = descriptor.label;
        control.description = descriptor.description;
        control.enabled = descriptor.enabled;
        control.disabled_reason = descriptor.disabled_reason;
        detail::ValidateControlInput(control);
        const auto existing = named_ids_.find(control.id);
        if (existing != named_ids_.end() && existing->second != descriptor.id)
            throw std::logic_error("stable UI ID hash collision in frame");
        if (frame_ids_.contains(control.id))
            throw std::logic_error("duplicate UI node ID in frame");
        named_ids_.insert_or_assign(control.id, std::string{descriptor.id});
        frame_ids_.insert(control.id);
        try
        {
            return backend_.Canvas(control, size, primitives);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
    }

    UiEditResult UiContext::DispatchControl(
        const UiControl &control, const std::optional<std::string_view> named_id,
        const UiIconMask icon)
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
            UiEditResult result = icon.IsValid() ? backend_.IconControl(control, icon)
                                                 : backend_.Control(control);
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
        return DispatchControl(control, descriptor.id, descriptor.icon);
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
        const UiEditResult result = DispatchControl(control, descriptor.id, descriptor.icon);
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

    UiEditResult UiContext::SliderValue(const UiControlDescriptor &descriptor,
                                        UiValue value, UiValue minimum, UiValue maximum)
    {
        UiControl control = MakeControl(descriptor, UiRole::Slider, std::move(value),
                                        {UiActionKind::Edit, UiActionKind::Commit});
        control.id = HashNamedId(descriptor.id);
        control.minimum = std::move(minimum);
        control.maximum = std::move(maximum);
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

    void UiContext::Text(const std::string_view text, const UiTextRole role)
    {
        VerifyThread();
        if (!in_frame_)
            throw std::logic_error("UI text requires an active frame");
        backend_.StyledText(text, role);
    }

    void UiContext::SameLine()
    {
        VerifyThread();
        if (!in_frame_) throw std::logic_error("UI SameLine requires an active frame");
        backend_.SameLine();
    }

    UiVec2 UiContext::AvailableContent() const
    {
        VerifyThread();
        return in_frame_ ? backend_.AvailableContent() : UiVec2{};
    }

    void UiContext::Section(const std::string_view text)
    {
        VerifyThread();
        if (!in_frame_) throw std::logic_error("UI section requires an active frame");
        backend_.Section(text);
    }

    void UiContext::WrappedText(const std::string_view text)
    {
        VerifyThread();
        if (!in_frame_) throw std::logic_error("UI text requires an active frame");
        backend_.WrappedText(text);
    }

    void UiContext::Table(const std::string_view name, const std::span<const UiTableColumn> columns,
                          const float height, const bool headers, const std::function<void()> &draw)
    {
        const UiNodeId id = HashNamedId(name);
        if (in_table_ || columns.empty() || columns.size() > 32 ||
            !std::isfinite(height) || height < 0.0F)
            throw std::logic_error("invalid UI table scope");
        for (const auto &column : columns)
        {
            if (!std::isfinite(column.width) || column.width < 0.0F)
                throw std::logic_error("invalid UI table column width");
        }
        if (!frame_ids_.insert(id).second)
            throw std::logic_error("duplicate UI table ID in frame");
        if (!backend_.BeginTable(id, columns, height, headers)) return;
        in_table_ = true;
        try
        {
            draw();
        }
        catch (...)
        {
            backend_.EndTable();
            in_table_ = false;
            throw;
        }
        backend_.EndTable();
        in_table_ = false;
    }

    void UiContext::TableRow(const bool highlighted)
    {
        VerifyThread();
        if (!in_frame_ || !in_table_) throw std::logic_error("UI row requires an active table");
        backend_.TableRow(highlighted);
    }

    void UiContext::TableColumn()
    {
        VerifyThread();
        if (!in_frame_ || !in_table_) throw std::logic_error("UI column requires an active table");
        backend_.TableColumn();
    }

    UiFrameResult UiContext::EndFrame()
    {
        VerifyThread();
        if (!in_frame_ || window_depth_ != 0 || theme_depth_ != 0 ||
            tab_bar_depth_ != 0 || tab_item_depth_ != 0)
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
        theme_depth_ = 0;
        tab_bar_depth_ = 0;
        tab_item_depth_ = 0;
        frame_ids_.clear();
        named_ids_.clear();
    }

    UiContext::WindowScope::WindowScope(UiContext &context, const UiControl &window)
        : context_(&context), visible_(context.BeginWindow(window))
    {
    }

    UiContext::ThemeScope::ThemeScope(UiContext &context, const UiTheme &theme)
        : context_(&context)
    {
        context.PushTheme(theme);
    }

    UiContext::TabBarScope::TabBarScope(UiContext &context, const UiControl &control)
        : context_(&context), visible_(context.BeginTabBar(control))
    {
        if (!visible_) context_ = nullptr;
    }

    UiContext::TabBarScope::~TabBarScope() noexcept
    {
        if (context_ != nullptr)
        {
            try { context_->EndTabBar(); }
            catch (...) { context_->AbortFrame(); }
        }
    }

    UiContext::TabBarScope::TabBarScope(TabBarScope &&other) noexcept
        : context_(std::exchange(other.context_, nullptr)), visible_(other.visible_)
    {
    }

    UiContext::TabItemScope::TabItemScope(UiContext &context, const UiControl &control)
        : context_(&context), visible_(context.BeginTabItem(control))
    {
        if (!visible_) context_ = nullptr;
    }

    UiContext::TabItemScope::~TabItemScope() noexcept
    {
        if (context_ != nullptr)
        {
            try { context_->EndTabItem(); }
            catch (...) { context_->AbortFrame(); }
        }
    }

    UiContext::TabItemScope::TabItemScope(TabItemScope &&other) noexcept
        : context_(std::exchange(other.context_, nullptr)), visible_(other.visible_)
    {
    }

    UiContext::ThemeScope::~ThemeScope() noexcept
    {
        if (context_ != nullptr)
            context_->PopTheme();
    }

    UiContext::ThemeScope::ThemeScope(ThemeScope &&other) noexcept
        : context_(std::exchange(other.context_, nullptr))
    {
    }

    UiContext::WindowScope::WindowScope(UiContext &context, const UiControl &window,
                                        bool &open, const UiVec2 first_use_position,
                                        const UiVec2 first_use_size)
        : context_(&context),
          visible_(context.BeginWindow(window, open, first_use_position, first_use_size))
    {
    }

    UiContext::WindowScope::WindowScope(UiContext &context, const UiControl &window,
                                        const UiVec2 first_use_position,
                                        const UiVec2 first_use_size)
        : context_(&context),
          visible_(context.BeginWindow(window, first_use_position, first_use_size))
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
