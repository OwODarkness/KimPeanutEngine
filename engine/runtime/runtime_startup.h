#ifndef KPENGINE_RUNTIME_STARTUP_H
#define KPENGINE_RUNTIME_STARTUP_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "asset/asset_load_observation.h"

namespace kpengine::runtime
{
    enum class StartupPhase : uint8_t
    {
        Cold,
        PresentationStarting,
        PresentationReady,
        LoadingAssets,
        PreparingCpuArtifacts,
        PromotingSceneRenderer,
        InstantiatingLevel,
        ActivatingEditorWorkspace,
        Ready,
        Failed,
        Cancelled,
        RolledBack,
    };

    struct StartupProgress
    {
        uint32_t completed_units = 0;
        uint32_t total_units = 0;
        bool total_known = false;
        float fraction = 0.0f;
    };

    struct StartupSnapshot
    {
        uint64_t transaction_id = 0;
        uint64_t revision = 0;
        StartupPhase phase = StartupPhase::Cold;
        StartupProgress progress;
        std::optional<asset::AssetLoadSnapshot> asset;
        std::string display_label;
        std::string diagnostic;
    };

    // A small quiescence barrier for the shared RuntimeContext. Cancellation
    // may be requested from the render thread, but teardown cannot begin until
    // the startup lane explicitly leaves its current Runtime operation.
    class StartupAccessBarrier final
    {
    public:
        void Begin() noexcept;
        void End() noexcept;
        void WaitForEnd() const noexcept;

    private:
        mutable std::mutex mutex_;
        mutable std::condition_variable changed_cv_;
        bool active_ = false;
    };

    // Runtime-owned startup state. It publishes copied facts and never calls
    // Asset, Render, Graphics, Gameplay, or Editor while holding its mutex.
    class StartupCoordinator final
    {
    public:
        StartupCoordinator() = default;

        uint64_t Begin();
        bool SetPhase(StartupPhase phase, std::string display_label = {});
        void SetProgress(StartupProgress progress);
        void SetAssetSession(asset::AssetLoadSession session);
        void Fail(std::string diagnostic);
        void Cancel(std::string diagnostic);
        void Rollback() noexcept;
        void SetReady();

        StartupSnapshot GetSnapshot() const;
        // Waits for the outer revision. Supplying the nested Asset revision
        // also wakes when Asset-only progress changes the observable snapshot.
        StartupSnapshot WaitForRevision(
            uint64_t revision, std::optional<uint64_t> asset_revision = std::nullopt) const;

    private:
        static bool IsTerminal(StartupPhase phase) noexcept;
        static bool IsLegalTransition(StartupPhase from,
                                      StartupPhase to) noexcept;

        mutable std::mutex mutex_;
        mutable std::condition_variable changed_cv_;
        uint64_t next_transaction_id_ = 1;
        StartupSnapshot state_;
        std::optional<asset::AssetLoadSession> asset_session_;
        std::string first_failure_;
    };
}

#endif
