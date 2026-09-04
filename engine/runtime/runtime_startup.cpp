#include "runtime_startup.h"

#include <algorithm>
#include <utility>

namespace kpengine::runtime
{
    namespace
    {
        float ClampFraction(float fraction) noexcept
        {
            return std::max(0.0f, std::min(1.0f, fraction));
        }
    }

    void StartupAccessBarrier::Begin() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = true;
    }

    void StartupAccessBarrier::End() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = false;
        }
        changed_cv_.notify_all();
    }

    void StartupAccessBarrier::WaitForEnd() const noexcept
    {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_cv_.wait(lock, [this] { return !active_; });
    }

    uint64_t StartupCoordinator::Begin()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = {};
        state_.transaction_id = next_transaction_id_++;
        if (state_.transaction_id == 0)
        {
            state_.transaction_id = next_transaction_id_++;
        }
        state_.revision = 1;
        state_.phase = StartupPhase::Cold;
        asset_session_.reset();
        first_failure_.clear();
        changed_cv_.notify_all();
        return state_.transaction_id;
    }

    bool StartupCoordinator::SetPhase(StartupPhase phase, std::string display_label)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsLegalTransition(state_.phase, phase))
            {
                return false;
            }
            state_.phase = phase;
            state_.display_label = std::move(display_label);
            ++state_.revision;
        }
        changed_cv_.notify_all();
        return true;
    }

    void StartupCoordinator::SetProgress(StartupProgress progress)
    {
        progress.fraction = ClampFraction(progress.fraction);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (IsTerminal(state_.phase))
            {
                return;
            }
            state_.progress = progress;
            ++state_.revision;
        }
        changed_cv_.notify_all();
    }

    void StartupCoordinator::SetAssetSession(asset::AssetLoadSession session)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (IsTerminal(state_.phase))
            {
                return;
            }
            asset_session_ = std::move(session);
            ++state_.revision;
        }
        changed_cv_.notify_all();
    }

    void StartupCoordinator::Fail(std::string diagnostic)
    {
        std::optional<asset::AssetLoadSession> asset_session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (IsTerminal(state_.phase))
            {
                return;
            }
            if (!first_failure_.empty())
            {
                diagnostic = first_failure_;
            }
            else
            {
                first_failure_ = diagnostic;
            }
            state_.phase = StartupPhase::Failed;
            state_.diagnostic = first_failure_;
            asset_session = asset_session_;
            ++state_.revision;
        }
        if (asset_session)
        {
            asset_session->Seal();
        }
        changed_cv_.notify_all();
    }

    void StartupCoordinator::Cancel(std::string diagnostic)
    {
        std::optional<asset::AssetLoadSession> asset_session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (IsTerminal(state_.phase))
            {
                return;
            }
            if (first_failure_.empty() && !diagnostic.empty())
            {
                first_failure_ = diagnostic;
            }
            state_.phase = StartupPhase::Cancelled;
            state_.diagnostic = first_failure_.empty() ? std::move(diagnostic)
                                                       : first_failure_;
            asset_session = asset_session_;
            ++state_.revision;
        }
        if (asset_session)
        {
            asset_session->Seal();
        }
        changed_cv_.notify_all();
    }

    void StartupCoordinator::Rollback() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_.phase == StartupPhase::Ready ||
                state_.phase == StartupPhase::RolledBack)
            {
                return;
            }
            if (state_.phase != StartupPhase::Failed &&
                state_.phase != StartupPhase::Cancelled)
            {
                return;
            }
            state_.phase = StartupPhase::RolledBack;
            state_.diagnostic = first_failure_.empty() ? state_.diagnostic
                                                       : first_failure_;
            ++state_.revision;
        }
        changed_cv_.notify_all();
    }

    void StartupCoordinator::SetReady()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsLegalTransition(state_.phase, StartupPhase::Ready))
            {
                return;
            }
            state_.phase = StartupPhase::Ready;
            state_.progress = {1, 1, true, 1.0f};
            state_.display_label = "Ready";
            ++state_.revision;
        }
        changed_cv_.notify_all();
    }

    StartupSnapshot StartupCoordinator::GetSnapshot() const
    {
        StartupSnapshot snapshot;
        std::optional<asset::AssetLoadSession> asset_session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = state_;
            asset_session = asset_session_;
        }
        if (asset_session && asset_session->IsValid())
        {
            snapshot.asset = asset_session->GetSnapshot();
        }
        return snapshot;
    }

    StartupSnapshot StartupCoordinator::WaitForRevision(
        uint64_t revision, std::optional<uint64_t> asset_revision) const
    {
        for (;;)
        {
            std::optional<asset::AssetLoadSession> asset_session;
            bool outer_changed = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                outer_changed = state_.revision != revision;
                asset_session = asset_session_;
            }

            if (asset_session && asset_session->IsValid())
            {
                const asset::AssetLoadSnapshot asset_snapshot = asset_session->GetSnapshot();
                if (outer_changed ||
                    (asset_revision.has_value() &&
                     asset_snapshot.revision != *asset_revision))
                {
                    return GetSnapshot();
                }
            }
            else if (outer_changed)
            {
                return GetSnapshot();
            }

            std::unique_lock<std::mutex> lock(mutex_);
            changed_cv_.wait_for(lock, std::chrono::milliseconds(10),
                                 [this, revision] { return state_.revision != revision; });
        }
    }

    bool StartupCoordinator::IsTerminal(StartupPhase phase) noexcept
    {
        return phase == StartupPhase::Ready || phase == StartupPhase::Failed ||
               phase == StartupPhase::Cancelled || phase == StartupPhase::RolledBack;
    }

    bool StartupCoordinator::IsLegalTransition(StartupPhase from,
                                                StartupPhase to) noexcept
    {
        if (from == to)
        {
            return !IsTerminal(from);
        }
        if (IsTerminal(from))
        {
            return from == StartupPhase::Failed && to == StartupPhase::RolledBack;
        }
        if (to == StartupPhase::Failed || to == StartupPhase::Cancelled)
        {
            return true;
        }
        switch (from)
        {
        case StartupPhase::Cold:
            return to == StartupPhase::PresentationStarting;
        case StartupPhase::PresentationStarting:
            return to == StartupPhase::PresentationReady;
        case StartupPhase::PresentationReady:
            return to == StartupPhase::LoadingAssets;
        case StartupPhase::LoadingAssets:
            return to == StartupPhase::PreparingCpuArtifacts;
        case StartupPhase::PreparingCpuArtifacts:
            return to == StartupPhase::PromotingSceneRenderer;
        case StartupPhase::PromotingSceneRenderer:
            return to == StartupPhase::InstantiatingLevel;
        case StartupPhase::InstantiatingLevel:
            return to == StartupPhase::ActivatingEditorWorkspace;
        case StartupPhase::ActivatingEditorWorkspace:
            return to == StartupPhase::Ready;
        case StartupPhase::Ready:
        case StartupPhase::Failed:
        case StartupPhase::Cancelled:
        case StartupPhase::RolledBack:
            return false;
        }
        return false;
    }
}
