#ifndef KPENGINE_RUNTIME_ASSET_LOAD_OBSERVATION_INTERNAL_H
#define KPENGINE_RUNTIME_ASSET_LOAD_OBSERVATION_INTERNAL_H

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "asset_load_observation.h"

namespace kpengine::asset::detail
{
    using AssetLoadObservationClock =
        std::function<std::chrono::steady_clock::time_point()>;

    AssetLoadSessionID AllocateAssetLoadSessionID() noexcept;

    class AssetLoadSessionState
    {
    public:
        explicit AssetLoadSessionState(
            AssetLoadSessionID session,
            AssetLoadObservationClock clock = {});

        AssetLoadSessionID SessionID() const noexcept { return session_; }
        std::chrono::steady_clock::time_point Now() const noexcept;

        AssetLoadOperationID BeginOperation(
            std::string display_path,
            AssetType expected_type,
            std::optional<AssetLoadOperationID> parent) noexcept;
        void UpdatePhase(AssetLoadOperationID operation,
                         AssetLoadPhase phase) noexcept;
        void UpdateTiming(AssetLoadOperationID operation,
                          const AssetLoadTiming &timing) noexcept;
        void UpdateSizeCost(AssetLoadOperationID operation,
                            const AssetLoadSizeCost &size_cost) noexcept;
        void SetKnownChildren(AssetLoadOperationID operation,
                              uint32_t known_children) noexcept;
        void ChildCompleted(AssetLoadOperationID operation) noexcept;
        void Complete(AssetLoadOperationID operation,
                      AssetLoadState state,
                      std::optional<AssetLoadDisposition> disposition,
                      std::optional<AssetID> result,
                      const AssetLoadTiming &timing,
                      const AssetLoadSizeCost &size_cost,
                      const std::string &diagnostic) noexcept;

        void Seal() noexcept;
        bool IsSealed() const noexcept;
        AssetLoadSnapshot GetSnapshot() const;

    private:
        struct ActiveOperation
        {
            AssetLoadObservation observation;
            std::chrono::steady_clock::time_point started;
        };

        static uint64_t ElapsedMicroseconds(
            std::chrono::steady_clock::time_point begin,
            std::chrono::steady_clock::time_point end) noexcept;
        static uint32_t ToCount(std::size_t value) noexcept;
        static void AddCost(uint64_t &total, uint64_t value) noexcept;
        static void AddCount(uint32_t &total) noexcept;

        void IncrementRevision() noexcept;
        void TrySetFirstFailure(const std::string &diagnostic) noexcept;

        mutable std::mutex mutex_;
        AssetLoadSessionID session_ = 0;
        uint64_t revision_ = 0;
        AssetLoadObservationClock clock_;
        std::chrono::steady_clock::time_point started_;
        std::chrono::steady_clock::time_point terminal_time_{};
        bool sealed_ = false;
        bool terminal_ = false;
        bool recording_disabled_ = false;
        AssetLoadOperationID next_operation_ = 1;
        AssetLoadSummary summary_;
        std::unordered_map<AssetLoadOperationID, ActiveOperation> active_;
        std::deque<AssetLoadObservation> recent_terminal_;
    };
}

#endif
