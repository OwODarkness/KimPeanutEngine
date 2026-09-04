#include "asset_load_observation_internal.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace kpengine::asset::detail
{
    namespace
    {
        std::atomic<AssetLoadSessionID> next_session_id{1};
    }

    AssetLoadSessionID AllocateAssetLoadSessionID() noexcept
    {
        AssetLoadSessionID id = next_session_id.fetch_add(1, std::memory_order_relaxed);
        while (id == 0)
        {
            id = next_session_id.fetch_add(1, std::memory_order_relaxed);
        }
        return id;
    }

    AssetLoadSessionState::AssetLoadSessionState(
        AssetLoadSessionID session,
        AssetLoadObservationClock clock)
        : session_(session),
          clock_(std::move(clock)),
          started_(std::chrono::steady_clock::now())
    {
        if (!clock_)
        {
            clock_ = [] { return std::chrono::steady_clock::now(); };
        }
        started_ = Now();
    }

    std::chrono::steady_clock::time_point AssetLoadSessionState::Now() const noexcept
    {
        try
        {
            return clock_();
        }
        catch (...)
        {
            return std::chrono::steady_clock::now();
        }
    }

    AssetLoadOperationID AssetLoadSessionState::BeginOperation(
        std::string display_path,
        AssetType expected_type,
        std::optional<AssetLoadOperationID> parent) noexcept
    {
        const auto started = Now();
        std::lock_guard<std::mutex> lock(mutex_);
        if (recording_disabled_ ||
            (sealed_ && (!parent || active_.find(*parent) == active_.end())))
        {
            return 0;
        }

        try
        {
            AssetLoadOperationID operation = next_operation_++;
            if (operation == 0)
            {
                operation = next_operation_++;
            }

            ActiveOperation active{};
            active.observation.operation = operation;
            active.observation.parent = parent;
            active.observation.display_path = std::move(display_path);
            active.observation.expected_type = expected_type;
            active.started = started;
            active_.emplace(operation, std::move(active));
            AddCount(summary_.operations_started);
            AddCount(summary_.operations_active);
            IncrementRevision();
            return operation;
        }
        catch (...)
        {
            recording_disabled_ = true;
            return 0;
        }
    }

    void AssetLoadSessionState::UpdatePhase(
        AssetLoadOperationID operation,
        AssetLoadPhase phase) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = active_.find(operation);
        if (it == active_.end() || it->second.observation.phase == phase)
        {
            return;
        }
        it->second.observation.phase = phase;
        IncrementRevision();
    }

    void AssetLoadSessionState::UpdateTiming(
        AssetLoadOperationID operation,
        const AssetLoadTiming &timing) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = active_.find(operation);
        if (it == active_.end())
        {
            return;
        }
        it->second.observation.timing = timing;
        IncrementRevision();
    }

    void AssetLoadSessionState::UpdateSizeCost(
        AssetLoadOperationID operation,
        const AssetLoadSizeCost &size_cost) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = active_.find(operation);
        if (it == active_.end())
        {
            return;
        }
        it->second.observation.size_cost = size_cost;
        IncrementRevision();
    }

    void AssetLoadSessionState::SetKnownChildren(
        AssetLoadOperationID operation,
        uint32_t known_children) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = active_.find(operation);
        if (it == active_.end() ||
            it->second.observation.known_children == known_children)
        {
            return;
        }
        it->second.observation.known_children = known_children;
        IncrementRevision();
    }

    void AssetLoadSessionState::ChildCompleted(
        AssetLoadOperationID operation) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = active_.find(operation);
        if (it == active_.end())
        {
            return;
        }
        AddCount(it->second.observation.completed_children);
        IncrementRevision();
    }

    void AssetLoadSessionState::Complete(
        AssetLoadOperationID operation,
        AssetLoadState state,
        std::optional<AssetLoadDisposition> disposition,
        std::optional<AssetID> result,
        const AssetLoadTiming &timing,
        const AssetLoadSizeCost &size_cost,
        const std::string &diagnostic) noexcept
    {
        const auto completed = Now();
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = active_.find(operation);
        if (it == active_.end())
        {
            return;
        }

        ActiveOperation &active = it->second;
        active.observation.state = state;
        active.observation.timing = timing;
        active.observation.timing.inclusive_elapsed_us =
            ElapsedMicroseconds(active.started, completed);
        active.observation.size_cost = size_cost;
        active.observation.disposition = disposition;
        active.observation.result = result;
        try
        {
            active.observation.diagnostic = diagnostic;
        }
        catch (...)
        {
            recording_disabled_ = true;
        }

        if (state == AssetLoadState::Succeeded)
        {
            AddCount(summary_.operations_succeeded);
            if (disposition == AssetLoadDisposition::CacheHit)
            {
                AddCount(summary_.cache_hits);
            }
            else if (disposition == AssetLoadDisposition::LoadedThenDeduplicated)
            {
                AddCount(summary_.post_load_deduplications);
            }
        }
        else if (state == AssetLoadState::Failed)
        {
            AddCount(summary_.operations_failed);
            TrySetFirstFailure(diagnostic);
        }

        AddCost(summary_.cost.cumulative_cache_lookup_us,
                timing.cache_lookup_us);
        AddCost(summary_.cost.cumulative_loader_queue_wait_us,
                timing.loader_queue_wait_us);
        AddCost(summary_.cost.cumulative_source_load_us,
                timing.source_load_us);
        AddCost(summary_.cost.cumulative_registration_us,
                timing.registration_us);
        if (size_cost.source_file_bytes)
        {
            AddCost(summary_.cost.measured_source_file_bytes,
                    *size_cost.source_file_bytes);
            AddCount(summary_.cost.source_file_measurement_count);
        }
        if (size_cost.decoded_payload_bytes)
        {
            AddCost(summary_.cost.measured_decoded_payload_bytes,
                    *size_cost.decoded_payload_bytes);
            AddCount(summary_.cost.decoded_payload_measurement_count);
        }

        try
        {
            recent_terminal_.push_back(active.observation);
            while (recent_terminal_.size() > 16)
            {
                recent_terminal_.pop_front();
            }
        }
        catch (...)
        {
            recording_disabled_ = true;
        }

        active_.erase(it);
        if (summary_.operations_active > 0)
        {
            --summary_.operations_active;
        }
        IncrementRevision();
        if (sealed_ && active_.empty() && !terminal_)
        {
            terminal_ = true;
            terminal_time_ = completed;
            IncrementRevision();
        }
    }

    void AssetLoadSessionState::Seal() noexcept
    {
        const auto sealed_time = Now();
        std::lock_guard<std::mutex> lock(mutex_);
        if (sealed_)
        {
            return;
        }
        sealed_ = true;
        IncrementRevision();
        if (active_.empty())
        {
            terminal_ = true;
            terminal_time_ = sealed_time;
            IncrementRevision();
        }
    }

    bool AssetLoadSessionState::IsSealed() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sealed_;
    }

    AssetLoadSnapshot AssetLoadSessionState::GetSnapshot() const
    {
        const auto snapshot_time = Now();
        std::lock_guard<std::mutex> lock(mutex_);
        AssetLoadSnapshot snapshot{};
        snapshot.session = session_;
        snapshot.revision = revision_;
        snapshot.sealed = sealed_;
        snapshot.terminal = terminal_;
        snapshot.summary = summary_;
        snapshot.summary.wall_elapsed_us = ElapsedMicroseconds(
            started_, terminal_ ? terminal_time_ : snapshot_time);

        std::vector<AssetLoadOperationID> operations;
        operations.reserve(active_.size());
        for (const auto &[operation, record] : active_)
        {
            (void)record;
            operations.push_back(operation);
        }
        std::sort(operations.rbegin(), operations.rend());
        const std::size_t visible_count = std::min<std::size_t>(16, operations.size());
        snapshot.omitted_active_operations = ToCount(operations.size() - visible_count);
        snapshot.active_operations.reserve(visible_count);
        for (std::size_t index = 0; index < visible_count; ++index)
        {
            const ActiveOperation &record = active_.at(operations[index]);
            AssetLoadObservation copy = record.observation;
            copy.timing.inclusive_elapsed_us =
                ElapsedMicroseconds(record.started, snapshot_time);
            snapshot.active_operations.push_back(std::move(copy));
        }
        snapshot.recent_terminal_operations.assign(recent_terminal_.begin(),
                                                    recent_terminal_.end());
        return snapshot;
    }

    uint64_t AssetLoadSessionState::ElapsedMicroseconds(
        std::chrono::steady_clock::time_point begin,
        std::chrono::steady_clock::time_point end) noexcept
    {
        if (end <= begin)
        {
            return 0;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
        return elapsed.count() < 0 ? 0 : static_cast<uint64_t>(elapsed.count());
    }

    uint32_t AssetLoadSessionState::ToCount(std::size_t value) noexcept
    {
        return value > std::numeric_limits<uint32_t>::max()
                   ? std::numeric_limits<uint32_t>::max()
                   : static_cast<uint32_t>(value);
    }

    void AssetLoadSessionState::AddCost(uint64_t &total, uint64_t value) noexcept
    {
        if (std::numeric_limits<uint64_t>::max() - total < value)
        {
            total = std::numeric_limits<uint64_t>::max();
            return;
        }
        total += value;
    }

    void AssetLoadSessionState::AddCount(uint32_t &total) noexcept
    {
        if (total != std::numeric_limits<uint32_t>::max())
        {
            ++total;
        }
    }

    void AssetLoadSessionState::IncrementRevision() noexcept
    {
        if (revision_ != std::numeric_limits<uint64_t>::max())
        {
            ++revision_;
        }
    }

    void AssetLoadSessionState::TrySetFirstFailure(
        const std::string &diagnostic) noexcept
    {
        if (!summary_.first_failure.empty())
        {
            return;
        }
        try
        {
            summary_.first_failure = diagnostic;
        }
        catch (...)
        {
            recording_disabled_ = true;
        }
    }
}

namespace kpengine::asset
{
    AssetLoadSession::AssetLoadSession(
        std::shared_ptr<detail::AssetLoadSessionState> state)
        : state_(std::move(state))
    {
    }

    bool AssetLoadSession::IsValid() const noexcept
    {
        return state_ != nullptr && state_->SessionID() != 0;
    }

    void AssetLoadSession::Seal() noexcept
    {
        if (state_)
        {
            state_->Seal();
        }
    }

    AssetLoadSnapshot AssetLoadSession::GetSnapshot() const
    {
        return state_ ? state_->GetSnapshot() : AssetLoadSnapshot{};
    }
}
