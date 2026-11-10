#ifndef KPENGINE_EDITOR_UI_API_UI_SESSION_H
#define KPENGINE_EDITOR_UI_API_UI_SESSION_H

#include "ui_backend.h"

namespace kpengine::editor::ui
{
    enum class UiSessionState : std::uint8_t
    {
        Uninitialized, Initializing, Ready, InFrame, Closing, Closed
    };

    class UiSession final
    {
    public:
        class FrameScope;
        explicit UiSession(std::unique_ptr<IUiBackend> backend);
        ~UiSession();
        UiSession(const UiSession &) = delete;
        UiSession &operator=(const UiSession &) = delete;
        bool Initialize(const UiViewportInfo &viewport);
        UiContext &Context() noexcept { return context_; }
        bool BeginFrame(const UiFrameInfo &frame);
        FrameScope Frame(const UiFrameInfo &frame);
        UiFrameResult EndFrame();
        void AbortFrame() noexcept;
        void BeginClosing() noexcept;
        void Shutdown() noexcept;
        UiSessionState State() const noexcept { return state_; }
        bool IsInitialized() const noexcept { return state_ == UiSessionState::Ready; }

    private:
        std::unique_ptr<IUiBackend> backend_;
        UiContext context_;
        UiSessionState state_ = UiSessionState::Uninitialized;
        std::uint64_t frame_ = 0;
        bool closing_requested_ = false;
        bool backend_started_ = false;
    };

    class UiSession::FrameScope final
    {
    public:
        explicit FrameScope(UiSession &session) noexcept : session_(&session) {}
        ~FrameScope() noexcept;
        FrameScope(const FrameScope &) = delete;
        FrameScope &operator=(const FrameScope &) = delete;
        FrameScope(FrameScope &&other) noexcept;
        FrameScope &operator=(FrameScope &&) = delete;
        UiFrameResult End();

    private:
        UiSession *session_ = nullptr;
    };
}

#endif
