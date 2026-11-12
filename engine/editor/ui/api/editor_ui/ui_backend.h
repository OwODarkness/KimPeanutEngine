#ifndef KPENGINE_EDITOR_UI_API_UI_BACKEND_H
#define KPENGINE_EDITOR_UI_API_UI_BACKEND_H

#include "ui_types.h"
#include "ui_theme.h"

#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kpengine::editor::ui
{
    struct UiFrameInfo
    {
        UiSessionId session = 0;
        std::uint64_t frame = 0;
        bool observe = false;
        UiViewportInfo viewport{};
    };
    struct UiThemeObservation
    {
        std::string id;
        UiTheme declared{};
    };
    struct UiFrameResult
    {
        std::uint64_t frame = 0;
        bool complete = false;
        bool wants_keyboard = false;
        bool wants_pointer = false;
        std::vector<UiNodeObservation> observations;
        std::vector<UiThemeObservation> styles;
        UiViewportInfo viewport{};
        std::size_t virtual_list_total = 0;
        std::size_t virtual_list_first = 0;
        std::size_t virtual_list_last = 0;
        bool has_virtual_list = false;
        UiSessionId session = 0;
        bool observed = false;
    };

    class IUiBackend
    {
    public:
        virtual ~IUiBackend() = default;
        virtual bool Initialize(const UiViewportInfo &viewport) = 0;
        virtual void BeginFrame(const UiFrameInfo &frame) = 0;
        virtual bool BeginWindow(const UiControl &window) = 0;
        virtual bool BeginWindow(const UiControl &window, bool &open,
                                 UiVec2 first_use_position, UiVec2 first_use_size)
        {
            (void)open;
            (void)first_use_position;
            (void)first_use_size;
            return BeginWindow(window);
        }
        virtual bool BeginWindow(const UiControl &window, UiVec2 first_use_position,
                                 UiVec2 first_use_size)
        {
            (void)first_use_position;
            (void)first_use_size;
            return BeginWindow(window);
        }
        virtual void EndWindow() = 0;
        virtual std::optional<UiRect> WindowBounds(UiNodeId) const noexcept { return std::nullopt; }
        virtual UiVec2 PointerPosition() const noexcept { return {}; }
        virtual bool BeginTabBar(const UiControl &) { return true; }
        virtual void EndTabBar() {}
        virtual bool BeginTabItem(const UiControl &) { return true; }
        virtual void EndTabItem() {}
        virtual void PushTheme(const UiTheme &) {}
        virtual void PopTheme() {}
        virtual void SetHostContainer(const UiControl &container) = 0;
        virtual UiEditResult Control(const UiControl &control) = 0;
        virtual UiCanvasResult Canvas(const UiControl &control, UiVec2 size,
                                      std::span<const UiCanvasPrimitive> primitives)
        {
            (void)control;
            (void)size;
            (void)primitives;
            return {};
        }
        virtual void Text(std::string_view text) = 0;
        virtual void StyledText(std::string_view text, UiTextRole) { Text(text); }
        virtual void SameLine() {}
        virtual UiEditResult IconControl(const UiControl &control, UiIconMask)
        {
            return Control(control);
        }
        virtual UiVec2 AvailableContent() const noexcept { return {}; }
        virtual void Section(std::string_view text) { StyledText(text, UiTextRole::Accent); }
        virtual void WrappedText(std::string_view text) { Text(text); }
        virtual void ObserveItem(const UiControl &) {}
        virtual bool BeginTable(UiNodeId, std::span<const UiTableColumn>, float, bool)
        {
            return true;
        }
        virtual void EndTable() {}
        virtual void TableRow(bool) {}
        virtual void TableColumn() {}
        virtual UiListRange BeginVirtualList(UiNodeId, std::size_t item_count, float, bool)
        {
            return {0, item_count};
        }
        virtual void EndVirtualList() {}
        virtual void SetClipboardText(std::string_view) {}
        virtual UiFrameResult EndFrame(std::uint64_t frame) = 0;
        virtual void AbortFrame() noexcept = 0;
        virtual void Shutdown() noexcept = 0;
    };

    class UiContext final
    {
    public:
        class WindowScope;
        class ThemeScope;
        class TabBarScope;
        class TabItemScope;
        class NodeScope;
        UiContext(UiSessionId session, IUiBackend &backend) noexcept;
        void BeginFrame(std::uint64_t frame, const UiFrameInfo &info);
        bool BeginWindow(const UiControl &window);
        bool BeginWindow(const UiControl &window, bool &open,
                         UiVec2 first_use_position = {}, UiVec2 first_use_size = {});
        bool BeginWindow(const UiControl &window, UiVec2 first_use_position,
                         UiVec2 first_use_size);
        WindowScope Window(const UiControl &window);
        WindowScope Window(const UiControlDescriptor &descriptor,
                           UiVec2 first_use_position, UiVec2 first_use_size);
        WindowScope Window(const UiControlDescriptor &descriptor, bool &open,
                           UiVec2 first_use_position = {}, UiVec2 first_use_size = {});
        TabBarScope TabBar(const UiControlDescriptor &descriptor);
        TabItemScope TabItem(const UiControlDescriptor &descriptor);
        NodeScope Node(const UiControlDescriptor &descriptor, UiRole role,
                       bool selected = false);
        ThemeScope Theme(const UiTheme &theme);
        void PushTheme(const UiTheme &theme);
        void PopTheme() noexcept;
        void EndWindow();
        std::optional<UiRect> WindowBounds(std::string_view id) const;
        UiVec2 PointerPosition() const noexcept { return backend_.PointerPosition(); }
        bool BeginTabBar(const UiControl &tab_bar);
        void EndTabBar();
        bool BeginTabItem(const UiControl &tab_item);
        void EndTabItem();
        void BeginNode(const UiControl &node, bool selected);
        void EndNode();
        void SetHostContainer(const UiControl &container);
        UiEditResult Control(const UiControl &control);
        UiCanvasResult Canvas(const UiControlDescriptor &descriptor, UiVec2 size,
                              std::vector<UiCanvasPrimitive> primitives,
                              std::optional<UiChartObservation> chart = {});
        UiEditResult Button(const UiControlDescriptor &descriptor);
        UiTypedEditResult<bool> Checkbox(const UiControlDescriptor &descriptor, bool checked);
        UiTypedEditResult<bool> Selectable(const UiControlDescriptor &descriptor, bool selected);
        template <typename T>
        UiTypedEditResult<T> Combo(const UiControlDescriptor &descriptor, T selected,
                                   std::vector<UiChoice> choices)
        {
            return MakeTypedEditResult<T>(ComboValue(
                descriptor, UiValue{std::move(selected)}, std::move(choices)));
        }

        template <typename T>
        UiTypedEditResult<T> NumericInput(const UiControlDescriptor &descriptor, T value,
                                          std::optional<std::type_identity_t<T>> minimum = {},
                                          std::optional<std::type_identity_t<T>> maximum = {},
                                          std::optional<std::type_identity_t<T>> step = {})
        {
            static_assert(std::is_same_v<T, std::int64_t> ||
                          std::is_same_v<T, std::uint64_t> ||
                          std::is_same_v<T, double>,
                          "numeric UI controls preserve i64, u64 or double values");
            return MakeTypedEditResult<T>(NumericInputValue(
                descriptor, UiValue{value},
                minimum ? std::optional<UiValue>{UiValue{*minimum}} : std::nullopt,
                maximum ? std::optional<UiValue>{UiValue{*maximum}} : std::nullopt,
                step ? std::optional<UiValue>{UiValue{*step}} : std::nullopt));
        }

        template <typename T>
        UiTypedEditResult<T> Slider(const UiControlDescriptor &descriptor, T value,
                                    std::type_identity_t<T> minimum,
                                    std::type_identity_t<T> maximum)
        {
            static_assert(std::is_same_v<T, std::int64_t> ||
                          std::is_same_v<T, std::uint64_t> ||
                          std::is_same_v<T, double>,
                          "sliders preserve i64, u64 or double values");
            return MakeTypedEditResult<T>(SliderValue(
                descriptor, UiValue{value}, UiValue{minimum}, UiValue{maximum}));
        }

        UiEditResult ComboValue(const UiControlDescriptor &descriptor, UiValue selected,
                                std::vector<UiChoice> choices);
        UiEditResult NumericInputValue(const UiControlDescriptor &descriptor, UiValue value,
                                       std::optional<UiValue> minimum = {},
                                       std::optional<UiValue> maximum = {},
                                       std::optional<UiValue> step = {});
        UiEditResult SliderValue(const UiControlDescriptor &descriptor, UiValue value,
                                 UiValue minimum, UiValue maximum);
        UiTypedEditResult<std::string> TextInput(const UiControlDescriptor &descriptor,
                                                 std::string_view value);
        void Text(std::string_view text);
        void Text(std::string_view text, UiTextRole role);
        void SameLine();
        UiVec2 AvailableContent() const;
        void Section(std::string_view text);
        void WrappedText(std::string_view text);
        // A positive height scrolls the table within the current panel.
        void Table(std::string_view id, std::span<const UiTableColumn> columns,
                   float height, bool headers, const std::function<void()> &draw,
                   std::string_view label = {});
        void TableRow(bool highlighted = false);
        void TableRow(std::string_view id, bool highlighted = false, std::string_view label = {});
        void TableColumn();
        // Virtual lists may be hosted in a table cell; nested lists remain invalid.
        void VirtualList(std::string_view id, std::size_t item_count, float row_height,
                         bool scroll_to_end,
                         const std::function<void(UiListRange)> &draw);
        void SetClipboardText(std::string_view text);
        UiFrameResult EndFrame();
        void AbortFrame() noexcept;
        UiSessionId SessionId() const noexcept { return session_; }
        std::uint64_t FrameId() const noexcept { return frame_; }
        const UiViewportInfo &Viewport() const noexcept { return frame_info_.viewport; }
        bool IsObserving() const noexcept { return in_frame_ && frame_info_.observe; }

    private:
        UiSessionId session_;
        IUiBackend &backend_;
        std::uint64_t frame_ = 0;
        UiFrameInfo frame_info_{};
        unsigned int window_depth_ = 0;
        unsigned int theme_depth_ = 0;
        unsigned int tab_bar_depth_ = 0;
        unsigned int tab_item_depth_ = 0;
        unsigned int node_depth_ = 0;
        bool in_frame_ = false;
        bool in_table_ = false;
        bool in_virtual_list_ = false;
        bool host_container_set_ = false;
        std::unordered_set<UiNodeId> frame_ids_;
        std::unordered_map<UiNodeId, std::string> named_ids_;
        std::vector<UiThemeObservation> frame_styles_;
        std::vector<std::pair<UiTheme, std::string>> theme_stack_;
        std::optional<UiListRange> virtual_list_range_;
        UiNodeId semantic_parent_ = 0;
        std::vector<UiNodeId> semantic_scopes_;
        std::unordered_map<UiNodeId, UiNodeId> observation_parents_;
        std::vector<UiNodeObservation> semantic_observations_;
        std::size_t table_row_index_ = 0;
        std::optional<std::size_t> table_column_;
        std::unordered_map<UiNodeId, std::size_t> observed_table_columns_;
        UiNodeId table_id_ = 0;
        UiNodeId table_parent_ = 0;
        UiNodeId active_row_id_ = 0;
        std::size_t text_index_ = 0;
        std::thread::id owner_thread_ = std::this_thread::get_id();
        void VerifyThread() const;
        UiNodeId HashNamedId(std::string_view name);
        void RecordSemanticNode(UiNodeObservation observation);
        UiEditResult DispatchControl(const UiControl &control,
                                     std::optional<std::string_view> named_id, UiIconMask icon = {});
    };

    class UiContext::TabBarScope final
    {
    public:
        TabBarScope(UiContext &context, const UiControl &control);
        ~TabBarScope() noexcept;
        TabBarScope(const TabBarScope &) = delete;
        TabBarScope &operator=(const TabBarScope &) = delete;
        TabBarScope(TabBarScope &&other) noexcept;
        TabBarScope &operator=(TabBarScope &&) = delete;
        bool Visible() const noexcept { return visible_; }
    private:
        UiContext *context_ = nullptr;
        bool visible_ = false;
    };

    class UiContext::TabItemScope final
    {
    public:
        TabItemScope(UiContext &context, const UiControl &control);
        ~TabItemScope() noexcept;
        TabItemScope(const TabItemScope &) = delete;
        TabItemScope &operator=(const TabItemScope &) = delete;
        TabItemScope(TabItemScope &&other) noexcept;
        TabItemScope &operator=(TabItemScope &&) = delete;
        bool ContentVisible() const noexcept { return visible_; }
    private:
        UiContext *context_ = nullptr;
        bool visible_ = false;
    };

    class UiContext::NodeScope final
    {
    public:
        NodeScope(UiContext &context, const UiControl &control, bool selected);
        ~NodeScope() noexcept;
        NodeScope(const NodeScope &) = delete;
        NodeScope &operator=(const NodeScope &) = delete;
        NodeScope(NodeScope &&other) noexcept;
        NodeScope &operator=(NodeScope &&) = delete;
    private:
        UiContext *context_ = nullptr;
    };

    class UiContext::ThemeScope final
    {
    public:
        ThemeScope(UiContext &context, const UiTheme &theme);
        ~ThemeScope() noexcept;
        ThemeScope(const ThemeScope &) = delete;
        ThemeScope &operator=(const ThemeScope &) = delete;
        ThemeScope(ThemeScope &&other) noexcept;
        ThemeScope &operator=(ThemeScope &&) = delete;
    private:
        UiContext *context_ = nullptr;
    };

    class UiContext::WindowScope final
    {
    public:
        WindowScope(UiContext &context, const UiControl &window);
        WindowScope(UiContext &context, const UiControl &window, bool &open,
                    UiVec2 first_use_position, UiVec2 first_use_size);
        WindowScope(UiContext &context, const UiControl &window,
                    UiVec2 first_use_position, UiVec2 first_use_size);
        ~WindowScope() noexcept;
        WindowScope(const WindowScope &) = delete;
        WindowScope &operator=(const WindowScope &) = delete;
        WindowScope(WindowScope &&other) noexcept;
        WindowScope &operator=(WindowScope &&) = delete;
        bool ContentVisible() const noexcept { return visible_; }
    private:
        UiContext *context_ = nullptr;
        bool visible_ = false;
    };

}

#endif
