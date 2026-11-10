#ifndef KPENGINE_EDITOR_UI_API_UI_BACKEND_H
#define KPENGINE_EDITOR_UI_API_UI_BACKEND_H

#include "ui_types.h"

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
    struct UiFrameResult
    {
        std::uint64_t frame = 0;
        bool complete = false;
        bool wants_keyboard = false;
        bool wants_pointer = false;
        std::vector<UiNodeObservation> observations;
    };

    class IUiBackend
    {
    public:
        virtual ~IUiBackend() = default;
        virtual bool Initialize(const UiViewportInfo &viewport) = 0;
        virtual void BeginFrame(const UiFrameInfo &frame) = 0;
        virtual bool BeginWindow(const UiControl &window) = 0;
        virtual void EndWindow() = 0;
        virtual void SetHostContainer(const UiControl &container) = 0;
        virtual UiEditResult Control(const UiControl &control) = 0;
        virtual void Text(std::string_view text) = 0;
        virtual UiFrameResult EndFrame(std::uint64_t frame) = 0;
        virtual void AbortFrame() noexcept = 0;
        virtual void Shutdown() noexcept = 0;
    };

    class UiContext final
    {
    public:
        class WindowScope;
        UiContext(UiSessionId session, IUiBackend &backend) noexcept;
        void BeginFrame(std::uint64_t frame, const UiFrameInfo &info);
        bool BeginWindow(const UiControl &window);
        WindowScope Window(const UiControl &window);
        void EndWindow();
        void SetHostContainer(const UiControl &container);
        UiEditResult Control(const UiControl &control);
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

        UiEditResult ComboValue(const UiControlDescriptor &descriptor, UiValue selected,
                                std::vector<UiChoice> choices);
        UiEditResult NumericInputValue(const UiControlDescriptor &descriptor, UiValue value,
                                       std::optional<UiValue> minimum = {},
                                       std::optional<UiValue> maximum = {},
                                       std::optional<UiValue> step = {});
        UiTypedEditResult<std::string> TextInput(const UiControlDescriptor &descriptor,
                                                 std::string_view value);
        void Text(std::string_view text);
        UiFrameResult EndFrame();
        void AbortFrame() noexcept;
        UiSessionId SessionId() const noexcept { return session_; }
        std::uint64_t FrameId() const noexcept { return frame_; }

    private:
        UiSessionId session_;
        IUiBackend &backend_;
        std::uint64_t frame_ = 0;
        unsigned int window_depth_ = 0;
        bool in_frame_ = false;
        std::unordered_set<UiNodeId> frame_ids_;
        std::unordered_map<UiNodeId, std::string> named_ids_;
        std::thread::id owner_thread_ = std::this_thread::get_id();
        void VerifyThread() const;
        UiNodeId HashNamedId(std::string_view name);
        UiEditResult DispatchControl(const UiControl &control,
                                     std::optional<std::string_view> named_id);
    };

    class UiContext::WindowScope final
    {
    public:
        WindowScope(UiContext &context, const UiControl &window);
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
