#include "editor_ui/ui_session.h"

#include <atomic>
#include <stdexcept>
#include <utility>

namespace kpengine::editor::ui
{
    namespace
    {
        UiSessionId NextSessionId() noexcept
        {
            static std::atomic<UiSessionId> next{1};
            return next.fetch_add(1, std::memory_order_relaxed);
        }

        IUiBackend &RequireBackend(const std::unique_ptr<IUiBackend> &backend)
        {
            if (!backend)
                throw std::invalid_argument("UI session requires a backend");
            return *backend;
        }
    }

    UiSession::UiSession(std::unique_ptr<IUiBackend> backend)
        : backend_(std::move(backend)), context_(NextSessionId(), RequireBackend(backend_))
    {
    }

    UiSession::~UiSession()
    {
        Shutdown();
    }

    bool UiSession::Initialize(const UiViewportInfo &viewport)
    {
        if (state_ == UiSessionState::Ready)
            return true;
        if (state_ != UiSessionState::Uninitialized)
            return false;
        state_ = UiSessionState::Initializing;
        try
        {
            backend_started_ = true;
            if (!backend_->Initialize(viewport))
            {
                backend_->Shutdown();
                backend_started_ = false;
                state_ = UiSessionState::Uninitialized;
                return false;
            }
        }
        catch (...)
        {
            if (backend_started_) backend_->Shutdown();
            backend_started_ = false;
            state_ = UiSessionState::Uninitialized;
            throw;
        }
        state_ = UiSessionState::Ready;
        return true;
    }

    bool UiSession::BeginFrame(const UiFrameInfo &frame)
    {
        if (state_ != UiSessionState::Ready && state_ != UiSessionState::Closing)
            return false;
        context_.BeginFrame(++frame_, frame);
        state_ = UiSessionState::InFrame;
        return true;
    }

    UiSession::FrameScope UiSession::Frame(const UiFrameInfo &frame)
    {
        if (!BeginFrame(frame))
            throw std::logic_error("UI session is not ready to begin a frame");
        return FrameScope(*this);
    }

    UiFrameResult UiSession::EndFrame()
    {
        if (state_ != UiSessionState::InFrame)
            throw std::logic_error("UI session has no active frame");
        UiFrameResult result{};
        try
        {
            result = context_.EndFrame();
        }
        catch (...)
        {
            state_ = closing_requested_ ? UiSessionState::Closing : UiSessionState::Ready;
            throw;
        }
        state_ = closing_requested_ ? UiSessionState::Closing : UiSessionState::Ready;
        return result;
    }

    void UiSession::AbortFrame() noexcept
    {
        if (state_ != UiSessionState::InFrame)
            return;
        context_.AbortFrame();
        state_ = closing_requested_ ? UiSessionState::Closing : UiSessionState::Ready;
    }

    void UiSession::BeginClosing() noexcept
    {
        if (state_ == UiSessionState::InFrame)
        {
            closing_requested_ = true;
        }
        else if (state_ == UiSessionState::Ready)
        {
            closing_requested_ = true;
            state_ = UiSessionState::Closing;
        }
    }

    void UiSession::Shutdown() noexcept
    {
        if (state_ == UiSessionState::Closed)
            return;
        if (backend_started_)
        {
            backend_->Shutdown();
            backend_started_ = false;
        }
        state_ = UiSessionState::Closed;
    }

    UiSession::FrameScope::~FrameScope() noexcept
    {
        if (session_ != nullptr)
            session_->AbortFrame();
    }

    UiSession::FrameScope::FrameScope(FrameScope &&other) noexcept
        : session_(std::exchange(other.session_, nullptr))
    {
    }

    UiFrameResult UiSession::FrameScope::End()
    {
        if (session_ == nullptr)
            throw std::logic_error("UI frame scope is no longer active");
        UiSession *const session = std::exchange(session_, nullptr);
        try
        {
            return session->EndFrame();
        }
        catch (...)
        {
            session->AbortFrame();
            throw;
        }
    }
}
