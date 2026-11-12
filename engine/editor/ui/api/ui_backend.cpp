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
            control.unit = descriptor.unit;
            control.domain_summary = descriptor.domain_summary;
            control.width = descriptor.width;
            control.height = descriptor.height;
            control.multiline = descriptor.multiline;
            control.text_role = descriptor.text_role;
            control.emphasized = descriptor.emphasized;
            control.span_row = descriptor.span_row;
            control.selection_decoration = descriptor.selection_decoration;
            control.value = std::move(value);
            if (descriptor.semantic_value) control.value = *descriptor.semantic_value;
            control.minimum = descriptor.semantic_minimum;
            control.maximum = descriptor.semantic_maximum;
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
        in_table_ = false;
        in_virtual_list_ = false;
        node_depth_ = 0;
        host_container_set_ = false;
        frame_ids_.clear();
        named_ids_.clear();
        frame_styles_.clear();
        theme_stack_.clear();
        virtual_list_range_.reset();
        semantic_parent_ = 0;
        semantic_scopes_.clear();
        observation_parents_.clear();
        semantic_observations_.clear();
        observed_table_columns_.clear();
        table_column_.reset();
        table_row_index_ = 0;
        table_id_ = table_parent_ = active_row_id_ = 0;
        text_index_ = 0;
        if (info.observe)
        {
            const UiNodeId root = StableUiNodeId("ui.session." + std::to_string(session_));
            semantic_parent_ = root;
            semantic_observations_.push_back({.session = session_, .frame = frame_, .id = root,
                .role = UiRole::Session, .label = "UI Session"});
        }
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
        if (frame_info_.observe)
        {
            observation_parents_.insert_or_assign(window.id, semantic_parent_);
            UiNodeObservation observation{.session = session_, .frame = frame_, .id = window.id,
                .parent = semantic_parent_, .role = UiRole::Window, .label = window.label,
                .description = window.description, .enabled = window.enabled,
                .disabled_reason = window.disabled_reason};
            observation.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
            if (const auto bounds = backend_.WindowBounds(window.id))
            {
                observation.bounds = *bounds;
                observation.geometry = UiGeometryProvenance::ActualBackend;
            }
            RecordSemanticNode(std::move(observation));
        }
        if (frame_info_.observe)
        {
            semantic_scopes_.push_back(semantic_parent_);
            semantic_parent_ = window.id;
        }
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
        if (frame_info_.observe)
        {
            observation_parents_.insert_or_assign(window.id, semantic_parent_);
            UiNodeObservation observation{.session = session_, .frame = frame_, .id = window.id,
                .parent = semantic_parent_, .role = UiRole::Window, .label = window.label,
                .description = window.description, .enabled = window.enabled,
                .disabled_reason = window.disabled_reason};
            observation.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
            if (const auto bounds = backend_.WindowBounds(window.id))
            {
                observation.bounds = *bounds;
                observation.geometry = UiGeometryProvenance::ActualBackend;
            }
            RecordSemanticNode(std::move(observation));
        }
        if (frame_info_.observe)
        {
            semantic_scopes_.push_back(semantic_parent_);
            semantic_parent_ = window.id;
        }
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
        if (frame_info_.observe)
        {
            observation_parents_.insert_or_assign(window.id, semantic_parent_);
            UiNodeObservation observation{.session = session_, .frame = frame_, .id = window.id,
                .parent = semantic_parent_, .role = UiRole::Window, .label = window.label,
                .description = window.description, .enabled = window.enabled,
                .disabled_reason = window.disabled_reason};
            observation.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
            if (const auto bounds = backend_.WindowBounds(window.id))
            {
                observation.bounds = *bounds;
                observation.geometry = UiGeometryProvenance::ActualBackend;
            }
            RecordSemanticNode(std::move(observation));
        }
        if (frame_info_.observe)
        {
            semantic_scopes_.push_back(semantic_parent_);
            semantic_parent_ = window.id;
        }
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
            throw std::logic_error("stable UI ID hash collision in frame: " +
                existing->second + " vs " + std::string{descriptor.id});
        named_ids_.insert_or_assign(id, std::string{descriptor.id});
        UiControl window{};
        window.id = id;
        window.role = UiRole::Window;
        window.label = descriptor.label;
        window.description = descriptor.description;
        window.enabled = descriptor.enabled;
        window.disabled_reason = descriptor.disabled_reason;
        window.fixed_geometry = descriptor.fixed_geometry;
        window.preserve_geometry = descriptor.preserve_geometry;
        window.window_no_title_bar = descriptor.window_no_title_bar;
        window.window_no_background = descriptor.window_no_background;
        window.window_no_border = descriptor.window_no_border;
        window.window_no_collapse = descriptor.window_no_collapse;
        window.window_no_saved_settings = descriptor.window_no_saved_settings;
        window.window_no_bring_to_front = descriptor.window_no_bring_to_front;
        window.window_no_inputs = descriptor.window_no_inputs;
        window.window_padding_zero = descriptor.window_padding_zero;
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
            throw std::logic_error("stable UI ID hash collision in frame: " +
                existing->second + " vs " + std::string{descriptor.id});
        named_ids_.insert_or_assign(id, std::string{descriptor.id});
        UiControl window{};
        window.id = id;
        window.role = UiRole::Window;
        window.label = descriptor.label;
        window.description = descriptor.description;
        window.enabled = descriptor.enabled;
        window.disabled_reason = descriptor.disabled_reason;
        window.fixed_geometry = descriptor.fixed_geometry;
        window.preserve_geometry = descriptor.preserve_geometry;
        window.window_no_title_bar = descriptor.window_no_title_bar;
        window.window_no_background = descriptor.window_no_background;
        window.window_no_border = descriptor.window_no_border;
        window.window_no_collapse = descriptor.window_no_collapse;
        window.window_no_saved_settings = descriptor.window_no_saved_settings;
        window.window_no_bring_to_front = descriptor.window_no_bring_to_front;
        window.window_no_inputs = descriptor.window_no_inputs;
        window.window_padding_zero = descriptor.window_padding_zero;
        return WindowScope(*this, window, open, first_use_position, first_use_size);
    }

    UiContext::TabBarScope UiContext::TabBar(const UiControlDescriptor &descriptor)
    {
        const UiNodeId id = HashNamedId(descriptor.id);
        const auto existing = named_ids_.find(id);
        if (existing != named_ids_.end() && existing->second != descriptor.id)
            throw std::logic_error("stable UI ID hash collision in frame: " +
                existing->second + " vs " + std::string{descriptor.id});
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
            throw std::logic_error("stable UI ID hash collision in frame: " +
                existing->second + " vs " + std::string{descriptor.id});
        named_ids_.insert_or_assign(id, std::string{descriptor.id});
        UiControl control{};
        control.id = id;
        control.role = UiRole::Group;
        control.label = descriptor.label;
        control.description = descriptor.description;
        return TabItemScope(*this, control);
    }

    UiContext::NodeScope UiContext::Node(const UiControlDescriptor &descriptor,
                                         const UiRole role, const bool selected)
    {
        if (role != UiRole::Group && role != UiRole::Panel)
            throw std::logic_error("semantic UI nodes must use a container role");
        UiControl control = MakeControl(descriptor, role, false, {});
        control.id = HashNamedId(descriptor.id);
        return NodeScope(*this, control, selected);
    }

    void UiContext::BeginNode(const UiControl &node, const bool selected)
    {
        VerifyThread();
        if (!in_frame_ || node.id == 0 || in_table_ || in_virtual_list_ ||
            !frame_ids_.insert(node.id).second)
            throw std::logic_error("invalid or duplicate UI semantic node scope");
        if (frame_info_.observe)
        {
            UiNodeObservation observation{};
            observation.session = session_;
            observation.frame = frame_;
            observation.id = node.id;
            observation.parent = semantic_parent_;
            observation.role = node.role;
            observation.label = node.label;
            observation.description = node.description;
            observation.enabled = node.enabled;
            observation.disabled_reason = node.disabled_reason;
            observation.domain_summary = node.domain_summary;
            observation.selected = node.role == UiRole::Panel
                ? std::optional<bool>{selected} : std::nullopt;
            observation.visible = true;
            semantic_observations_.push_back(std::move(observation));
            semantic_scopes_.push_back(semantic_parent_);
            semantic_parent_ = node.id;
        }
        ++node_depth_;
    }

    void UiContext::EndNode()
    {
        VerifyThread();
        if (!in_frame_ || node_depth_ == 0)
            throw std::logic_error("unbalanced UI semantic node scope");
        --node_depth_;
        if (frame_info_.observe && !semantic_scopes_.empty())
        {
            semantic_parent_ = semantic_scopes_.back();
            semantic_scopes_.pop_back();
        }
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
        if (visible)
        {
            if (frame_info_.observe)
            {
                UiNodeObservation observation{.session = session_, .frame = frame_, .id = tab_bar.id,
                    .parent = semantic_parent_, .role = UiRole::TabBar, .label = tab_bar.label,
                    .visible = visible};
                RecordSemanticNode(std::move(observation));
                semantic_scopes_.push_back(semantic_parent_);
                semantic_parent_ = tab_bar.id;
            }
            ++tab_bar_depth_;
        }
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
        if (frame_info_.observe)
        {
            semantic_parent_ = semantic_scopes_.back();
            semantic_scopes_.pop_back();
        }
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
        if (frame_info_.observe)
        {
            UiNodeObservation observation{.session = session_, .frame = frame_, .id = tab_item.id,
                .parent = semantic_parent_, .role = UiRole::Tab, .label = tab_item.label,
                .selected = visible, .visible = true};
            RecordSemanticNode(std::move(observation));
        }
        if (visible)
        {
            if (frame_info_.observe)
            {
                semantic_scopes_.push_back(semantic_parent_);
                semantic_parent_ = tab_item.id;
            }
            ++tab_item_depth_;
        }
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
        if (frame_info_.observe)
        {
            semantic_parent_ = semantic_scopes_.back();
            semantic_scopes_.pop_back();
        }
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
            if (frame_info_.observe)
            {
                const auto existing = std::find_if(frame_styles_.begin(), frame_styles_.end(),
                    [&theme](const UiThemeObservation &style) { return style.declared == theme; });
                const std::string id = existing != frame_styles_.end() ? existing->id
                    : "style." + std::to_string(frame_styles_.size() + 1);
                if (existing == frame_styles_.end()) frame_styles_.push_back({id, theme});
                theme_stack_.emplace_back(theme, id);
            }
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
            if (frame_info_.observe && !theme_stack_.empty()) theme_stack_.pop_back();
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
        if (frame_info_.observe && !semantic_scopes_.empty())
        {
            semantic_parent_ = semantic_scopes_.back();
            semantic_scopes_.pop_back();
        }
    }

    std::optional<UiRect> UiContext::WindowBounds(const std::string_view id) const
    {
        VerifyThread();
        if (!in_frame_ || id.empty())
            throw std::logic_error("window bounds require an active frame and stable ID");
        return backend_.WindowBounds(StableUiNodeId(id));
    }

    void UiContext::SetHostContainer(const UiControl &container)
    {
        VerifyThread();
        if (!in_frame_ || container.id == 0 || frame_ids_.contains(container.id))
            throw std::logic_error("invalid or duplicate host container ID");
        UiControl observed = container;
        observed.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
        try
        {
            backend_.SetHostContainer(observed);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        frame_ids_.insert(container.id);
        if (frame_info_.observe)
        {
            UiNodeObservation observation{.session = session_, .frame = frame_, .id = container.id,
                .parent = semantic_parent_, .role = UiRole::Group, .label = container.label,
                .description = container.description, .value = container.value,
                .enabled = container.enabled, .disabled_reason = container.disabled_reason};
            observation.style_id = observed.style_id;
            RecordSemanticNode(std::move(observation));
            semantic_parent_ = container.id;
        }
        semantic_parent_ = container.id;
        host_container_set_ = true;
    }

    UiNodeId UiContext::HashNamedId(const std::string_view name)
    {
        VerifyThread();
        if (!in_frame_ || name.empty())
            throw std::logic_error("named UI controls require an active frame and stable ID");
        return StableUiNodeId(name);
    }

    void UiContext::RecordSemanticNode(UiNodeObservation observation)
    {
        if (!frame_info_.observe) return;
        if (in_table_ && observation.parent == active_row_id_)
            observation.table_column = table_column_;
        semantic_observations_.push_back(std::move(observation));
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
                                     std::vector<UiCanvasPrimitive> primitives,
                                     std::optional<UiChartObservation> chart)
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
        control.unit = descriptor.unit;
        control.domain_summary = descriptor.domain_summary;
        if (descriptor.semantic_value) control.value = *descriptor.semantic_value;
        control.minimum = descriptor.semantic_minimum;
        control.maximum = descriptor.semantic_maximum;
        control.actions = descriptor.semantic_actions;
        detail::ValidateControlInput(control);
        const auto existing = named_ids_.find(control.id);
        if (existing != named_ids_.end() && existing->second != descriptor.id)
            throw std::logic_error("stable UI ID hash collision in frame: " +
                existing->second + " vs " + std::string{descriptor.id});
        if (frame_ids_.contains(control.id))
            throw std::logic_error("duplicate UI node ID in frame: " +
                (named_ids_.contains(control.id) ? named_ids_.at(control.id)
                    : std::string{"<unnamed>"}) + " (" + std::to_string(control.id) + ")");
        named_ids_.insert_or_assign(control.id, std::string{descriptor.id});
        frame_ids_.insert(control.id);
        try
        {
            if (frame_info_.observe)
            {
                control.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
                observation_parents_.insert_or_assign(control.id, semantic_parent_);
                UiNodeObservation observation{.session = session_, .frame = frame_, .id = control.id,
                    .parent = semantic_parent_, .role = descriptor.semantic_role.value_or(
                        chart ? UiRole::Chart : UiRole::Canvas),
                    .label = control.label, .description = control.description,
                    .selected = descriptor.semantic_role == UiRole::Selectable
                        ? std::optional<bool>{descriptor.semantic_selected} : std::nullopt,
                    .chart = std::move(chart)};
                if (descriptor.semantic_value) observation.value = *descriptor.semantic_value;
                RecordSemanticNode(std::move(observation));
            }
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
                throw std::logic_error("stable UI ID hash collision in frame: " +
                    existing->second + " vs " + std::string{*named_id});
        }
        if (frame_ids_.contains(control.id))
            throw std::logic_error("duplicate UI node ID in frame: " +
                (named_ids_.contains(control.id) ? named_ids_.at(control.id)
                    : std::string{"<unnamed>"}) + " (" + std::to_string(control.id) + ")");
        if (named_id)
        {
            named_ids_.insert_or_assign(control.id, std::string{*named_id});
        }
        frame_ids_.insert(control.id);
        UiControl observed = control;
        if (frame_info_.observe)
        {
            observed.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
            observation_parents_.insert_or_assign(control.id, semantic_parent_);
            if (in_table_ && semantic_parent_ == active_row_id_ && table_column_)
                observed_table_columns_.emplace(control.id, *table_column_);
        }
        try
        {
            UiEditResult result = icon.IsValid() ? backend_.IconControl(observed, icon)
                                                 : backend_.Control(observed);
            detail::ValidateControlResult(observed, result);
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
            if (frame_info_.observe)
            {
                const UiNodeId id = StableUiNodeId("ui.text." + std::to_string(semantic_parent_) + "." +
                                                    std::to_string(text_index_++));
                UiControl item{};
                item.id = id;
                item.role = UiRole::Text;
                item.label = std::string{text};
                item.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
                observation_parents_.insert_or_assign(id, semantic_parent_);
                backend_.ObserveItem(item);
                UiNodeObservation observation{.session = session_, .frame = frame_,
                    .id = id, .parent = semantic_parent_, .role = UiRole::Text,
                    .label = std::string{text}, .style_id = item.style_id};
                RecordSemanticNode(std::move(observation));
            }
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
        if (frame_info_.observe)
        {
            const UiNodeId id = StableUiNodeId("ui.text." + std::to_string(semantic_parent_) + "." +
                                                std::to_string(text_index_++));
            UiControl item{};
            item.id = id;
            item.role = UiRole::Text;
            item.label = std::string{text};
            item.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
            observation_parents_.insert_or_assign(id, semantic_parent_);
            backend_.ObserveItem(item);
            UiNodeObservation observation{.session = session_, .frame = frame_,
                .id = id, .parent = semantic_parent_, .role = UiRole::Text,
                .label = std::string{text}, .style_id = item.style_id};
            RecordSemanticNode(std::move(observation));
        }
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
        if (frame_info_.observe)
        {
            const UiNodeId id = StableUiNodeId("ui.heading." + std::to_string(semantic_parent_) + "." +
                                                std::to_string(text_index_++));
            UiControl item{};
            item.id = id;
            item.role = UiRole::Heading;
            item.label = std::string{text};
            item.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
            observation_parents_.insert_or_assign(id, semantic_parent_);
            backend_.ObserveItem(item);
            UiNodeObservation observation{.session = session_, .frame = frame_,
                .id = id, .parent = semantic_parent_, .role = UiRole::Heading,
                .label = std::string{text}, .style_id = item.style_id};
            RecordSemanticNode(std::move(observation));
        }
    }

    void UiContext::WrappedText(const std::string_view text)
    {
        VerifyThread();
        if (!in_frame_) throw std::logic_error("UI text requires an active frame");
        backend_.WrappedText(text);
        if (frame_info_.observe)
        {
            const UiNodeId id = StableUiNodeId("ui.wrapped_text." + std::to_string(semantic_parent_) + "." +
                                                std::to_string(text_index_++));
            UiControl item{};
            item.id = id;
            item.role = UiRole::Text;
            item.label = std::string{text};
            item.style_id = theme_stack_.empty() ? std::string{} : theme_stack_.back().second;
            observation_parents_.insert_or_assign(id, semantic_parent_);
            backend_.ObserveItem(item);
            UiNodeObservation observation{.session = session_, .frame = frame_,
                .id = id, .parent = semantic_parent_, .role = UiRole::Text,
                .label = std::string{text}, .style_id = item.style_id};
            RecordSemanticNode(std::move(observation));
        }
    }

    void UiContext::Table(const std::string_view name, const std::span<const UiTableColumn> columns,
                          const float height, const bool headers, const std::function<void()> &draw,
                          const std::string_view label)
    {
        const UiNodeId id = HashNamedId(name);
        if (in_table_)
            throw std::logic_error("invalid UI table scope: nested tables are not supported");
        if (in_virtual_list_)
            throw std::logic_error("invalid UI table scope: table cannot begin inside a virtual list");
        if (columns.empty() || columns.size() > 32 || !std::isfinite(height) || height < 0.0F)
            throw std::logic_error("invalid UI table scope: invalid columns or height");
        for (const auto &column : columns)
        {
            if (!std::isfinite(column.width) || column.width < 0.0F)
                throw std::logic_error("invalid UI table column width");
        }
        if (!frame_ids_.insert(id).second)
            throw std::logic_error("duplicate UI table ID in frame");
        const UiNodeId previous_parent = semantic_parent_;
        const bool visible = backend_.BeginTable(id, columns, height, headers);
        if (frame_info_.observe)
        {
            UiNodeObservation table{};
            table.session = session_;
            table.frame = frame_;
            table.id = id;
            table.parent = previous_parent;
            table.role = UiRole::Table;
            table.label = std::string{label.empty() ? name : label};
            for (const auto &column : columns) table.table_columns.emplace_back(column.label);
            table.semantic_key = std::string{name};
            table.visible = visible;
            semantic_observations_.push_back(std::move(table));
        }
        if (!visible) return;
        in_table_ = true;
        if (frame_info_.observe)
        {
            table_id_ = id;
            table_parent_ = previous_parent;
            table_row_index_ = 0;
            active_row_id_ = 0;
            table_column_.reset();
            semantic_scopes_.push_back(previous_parent);
            semantic_parent_ = id;
        }
        try
        {
            draw();
        }
        catch (...)
        {
            backend_.EndTable();
            in_table_ = false;
            if (frame_info_.observe)
            {
                semantic_parent_ = table_parent_;
                if (!semantic_scopes_.empty()) semantic_scopes_.pop_back();
                table_id_ = table_parent_ = active_row_id_ = 0;
            }
            throw;
        }
        backend_.EndTable();
        in_table_ = false;
        if (frame_info_.observe)
        {
            semantic_parent_ = table_parent_;
            if (!semantic_scopes_.empty()) semantic_scopes_.pop_back();
            table_id_ = table_parent_ = active_row_id_ = 0;
        }
    }

    void UiContext::TableRow(const bool highlighted)
    {
        if (!frame_info_.observe)
        {
            VerifyThread();
            if (!in_frame_ || !in_table_) throw std::logic_error("UI row requires an active table");
            backend_.TableRow(highlighted);
            return;
        }
        const std::string id = "ui.table." + std::to_string(table_id_) + ".row." +
                               std::to_string(table_row_index_++);
        TableRow(id, highlighted);
    }

    void UiContext::TableRow(const std::string_view name, const bool highlighted,
                             const std::string_view label)
    {
        VerifyThread();
        if (!in_frame_ || !in_table_) throw std::logic_error("UI row requires an active table");
        if (frame_info_.observe)
        {
            const UiNodeId id = HashNamedId(name);
            if (!frame_ids_.insert(id).second)
                throw std::logic_error("duplicate UI table row ID in frame");
            UiNodeObservation row{.session = session_, .frame = frame_, .id = id,
                .parent = table_id_, .role = UiRole::Row, .label = std::string{label},
                .selected = highlighted, .semantic_key = std::string{name}};
            RecordSemanticNode(std::move(row));
            active_row_id_ = id;
            table_column_.reset();
            semantic_parent_ = id;
        }
        backend_.TableRow(highlighted);
    }

    void UiContext::TableColumn()
    {
        VerifyThread();
        if (!in_frame_ || !in_table_) throw std::logic_error("UI column requires an active table");
        backend_.TableColumn();
        if (frame_info_.observe && active_row_id_ != 0)
            table_column_ = table_column_ ? *table_column_ + 1 : 0;
    }

    void UiContext::VirtualList(const std::string_view name, const std::size_t item_count,
                                const float row_height, const bool scroll_to_end,
                                const std::function<void(UiListRange)> &draw)
    {
        VerifyThread();
        const UiNodeId id = HashNamedId(name);
        if (!in_frame_ || (window_depth_ != 1 && !host_container_set_) ||
            in_virtual_list_ ||
            !std::isfinite(row_height) || row_height <= 0.0F || !draw)
            throw std::logic_error("invalid UI virtual list scope");
        if (!frame_ids_.insert(id).second)
            throw std::logic_error("duplicate UI virtual list ID in frame");

        UiListRange range{};
        try
        {
            range = backend_.BeginVirtualList(id, item_count, row_height, scroll_to_end);
        }
        catch (...)
        {
            AbortFrame();
            throw;
        }
        if (range.first > range.last || range.last > item_count)
        {
            backend_.EndVirtualList();
            throw std::logic_error("UI backend returned an invalid virtual list range");
        }

        if (frame_info_.observe)
        {
            UiNodeObservation list{.session = session_, .frame = frame_, .id = id,
                .parent = semantic_parent_, .role = UiRole::VirtualList,
                .label = std::string{name}};
            list.virtual_list_total = item_count;
            list.virtual_list_first = range.first;
            list.virtual_list_last = range.last;
            RecordSemanticNode(list);
            virtual_list_range_ = range;
            semantic_scopes_.push_back(semantic_parent_);
            semantic_parent_ = id;
        }

        in_virtual_list_ = true;
        try
        {
            draw(range);
        }
        catch (...)
        {
            backend_.EndVirtualList();
            in_virtual_list_ = false;
            if (frame_info_.observe)
            {
                semantic_parent_ = semantic_scopes_.back();
                semantic_scopes_.pop_back();
                virtual_list_range_.reset();
            }
            throw;
        }
        backend_.EndVirtualList();
        in_virtual_list_ = false;
        if (frame_info_.observe)
        {
            semantic_parent_ = semantic_scopes_.back();
            semantic_scopes_.pop_back();
            virtual_list_range_.reset();
        }
    }

    void UiContext::SetClipboardText(const std::string_view text)
    {
        VerifyThread();
        if (!in_frame_)
            throw std::logic_error("clipboard access requires an active UI frame");
        try
        {
            backend_.SetClipboardText(text);
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
        if (!in_frame_ || window_depth_ != 0 || theme_depth_ != 0 || in_table_ ||
            in_virtual_list_ ||
            tab_bar_depth_ != 0 || tab_item_depth_ != 0 || node_depth_ != 0)
            throw std::logic_error("cannot finish an incomplete UI frame");
        try
        {
            UiFrameResult result = backend_.EndFrame(frame_);
            result.session = session_;
            result.viewport = frame_info_.viewport;
            result.observed = frame_info_.observe;
            result.styles = frame_styles_;
            if (frame_info_.observe)
            {
                std::vector<UiNodeObservation> backend_observations =
                    std::move(result.observations);
                result.observations = std::move(semantic_observations_);
                std::unordered_map<UiNodeId, std::size_t> positions;
                positions.reserve(result.observations.size() + backend_observations.size());
                for (std::size_t index = 0; index < result.observations.size(); ++index)
                    positions.emplace(result.observations[index].id, index);
                for (UiNodeObservation &backend_node : backend_observations)
                {
                    if (const auto parent = observation_parents_.find(backend_node.id);
                        parent != observation_parents_.end())
                        backend_node.parent = parent->second;
                    const auto [found, inserted] = positions.emplace(
                        backend_node.id, result.observations.size());
                    if (inserted)
                    {
                        result.observations.push_back(std::move(backend_node));
                        continue;
                    }
                    UiNodeObservation &node = result.observations[found->second];
                    node.parent = backend_node.parent;
                    node.focused = backend_node.focused;
                    node.hovered = backend_node.hovered;
                    node.clipped = backend_node.clipped;
                    node.active = backend_node.active;
                    node.visible = backend_node.visible;
                    node.clipped_bounds = backend_node.clipped_bounds;
                    if (backend_node.selected) node.selected = backend_node.selected;
                    if (backend_node.geometry != UiGeometryProvenance::Unavailable)
                    {
                        node.bounds = backend_node.bounds;
                        node.geometry = backend_node.geometry;
                    }
                    if (!backend_node.style_id.empty()) node.style_id = std::move(backend_node.style_id);
                    if (!backend_node.actions.empty()) node.actions = std::move(backend_node.actions);
                    node.last_action = backend_node.last_action;
                    if (backend_node.value) node.value = std::move(backend_node.value);
                    node.enabled = backend_node.enabled;
                    node.disabled_reason = std::move(backend_node.disabled_reason);
                    node.unit = std::move(backend_node.unit);
                    node.domain_summary = std::move(backend_node.domain_summary);
                    node.minimum = std::move(backend_node.minimum);
                    node.maximum = std::move(backend_node.maximum);
                    node.step = std::move(backend_node.step);
                    node.choices = std::move(backend_node.choices);
                }
                for (UiNodeObservation &node : result.observations)
                {
                    if (const auto column = observed_table_columns_.find(node.id);
                        column != observed_table_columns_.end())
                        node.table_column = column->second;
                    if (const auto key = named_ids_.find(node.id); key != named_ids_.end())
                        node.semantic_key = key->second;
                    else if (node.role == UiRole::Session)
                        node.semantic_key = "ui.session";
                    if (node.role != UiRole::VirtualList) continue;
                    result.has_virtual_list = true;
                    result.virtual_list_total += node.virtual_list_total;
                    result.virtual_list_first += node.virtual_list_first;
                    result.virtual_list_last += node.virtual_list_last;
                }
            }
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
        node_depth_ = 0;
        in_table_ = false;
        in_virtual_list_ = false;
        host_container_set_ = false;
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

    UiContext::NodeScope::NodeScope(UiContext &context, const UiControl &control,
                                    const bool selected)
        : context_(&context)
    {
        context.BeginNode(control, selected);
    }

    UiContext::NodeScope::~NodeScope() noexcept
    {
        if (context_ != nullptr)
        {
            try { context_->EndNode(); }
            catch (...) { context_->AbortFrame(); }
        }
    }

    UiContext::NodeScope::NodeScope(NodeScope &&other) noexcept
        : context_(std::exchange(other.context_, nullptr))
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
