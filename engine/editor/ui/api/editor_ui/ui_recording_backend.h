#ifndef KPENGINE_EDITOR_UI_API_UI_RECORDING_BACKEND_H
#define KPENGINE_EDITOR_UI_API_UI_RECORDING_BACKEND_H

#include "ui_backend.h"

#include <unordered_map>
#include <vector>

namespace kpengine::editor::ui
{
    struct UiScriptedInput
    {
        UiScriptedInput() = default;
        UiScriptedInput(UiNodeId node, UiValue input,
                        UiActionKind action_kind = UiActionKind::Edit)
            : id(node), value(std::move(input)), action(action_kind) {}
        UiScriptedInput(UiSessionId session_id, std::uint64_t frame_id,
                        UiNodeId node, UiValue input, UiActionKind action_kind)
            : session(session_id), frame(frame_id), id(node), value(std::move(input)),
              action(action_kind) {}
        UiSessionId session = 0;
        std::uint64_t frame = 0;
        UiNodeId id = 0;
        UiValue value{false};
        UiActionKind action = UiActionKind::Edit;
    };
    struct UiOperation
    {
        enum class Kind : std::uint8_t
        { BeginWindow, EndWindow, BeginTabBar, EndTabBar, BeginTabItem, EndTabItem,
          Control, Text, StyledText, SameLine, Canvas, PushTheme, PopTheme };
        Kind kind;
        UiControl control{};
        std::string text;
        UiVec2 canvas_size{};
        std::vector<UiCanvasPrimitive> canvas_primitives;
        UiTextRole text_role = UiTextRole::Primary;
        UiTheme theme{};
    };

    class UiRecordingBackend final : public IUiBackend
    {
    public:
        explicit UiRecordingBackend(std::vector<UiScriptedInput> input = {});
        bool Initialize(const UiViewportInfo &viewport) override;
        void BeginFrame(const UiFrameInfo &frame) override;
        bool BeginWindow(const UiControl &window) override;
        bool BeginTabBar(const UiControl &control) override;
        void EndTabBar() override;
        bool BeginTabItem(const UiControl &control) override;
        void EndTabItem() override;
        void PushTheme(const UiTheme &theme) override;
        void PopTheme() override;
        void EndWindow() override;
        void SetHostContainer(const UiControl &container) override;
        UiEditResult Control(const UiControl &control) override;
        UiCanvasResult Canvas(const UiControl &control, UiVec2 size,
                              std::span<const UiCanvasPrimitive> primitives) override;
        void Text(std::string_view text) override;
        void StyledText(std::string_view text, UiTextRole role) override;
        void SameLine() override;
        UiFrameResult EndFrame(std::uint64_t frame) override;
        void AbortFrame() noexcept override;
        void Shutdown() noexcept override;
        const std::vector<UiOperation> &Operations() const noexcept { return operations_; }
        std::vector<UiNodeObservation> Observations() const;
        void SetFixtureBounds(UiNodeId id, UiRect bounds);
        void QueueInput(UiSessionId session, std::uint64_t frame,
                        UiNodeId id, UiValue value, UiActionKind action);

    private:
        std::vector<UiScriptedInput> input_;
        std::vector<UiOperation> operations_;
        std::vector<UiNodeObservation> observations_;
        std::vector<UiNodeObservation> pending_observations_;
        std::unordered_map<UiNodeId, UiRect> fixture_bounds_;
        UiSessionId session_ = 0;
        std::uint64_t frame_ = 0;
        UiNodeId parent_ = 0;
        bool initialized_ = false;
        bool in_frame_ = false;
        bool window_open_ = false;
    };
}

#endif
