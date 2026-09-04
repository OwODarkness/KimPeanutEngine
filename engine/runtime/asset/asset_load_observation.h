#ifndef KPENGINE_RUNTIME_ASSET_LOAD_OBSERVATION_H
#define KPENGINE_RUNTIME_ASSET_LOAD_OBSERVATION_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common.h"

namespace kpengine::asset
{
    namespace detail
    {
        class AssetLoadSessionState;
    }

    using AssetLoadSessionID = uint64_t;
    using AssetLoadOperationID = uint64_t;

    enum class AssetLoadState : uint8_t
    {
        Running,
        Succeeded,
        Failed,
    };

    enum class AssetLoadPhase : uint8_t
    {
        CacheLookup,
        WaitingForLoader,
        LoadSource,
        ResolveDependencies,
        Register,
    };

    enum class AssetLoadDisposition : uint8_t
    {
        CacheHit,
        LoadedAndRegistered,
        LoadedThenDeduplicated,
    };

    struct AssetLoadTiming
    {
        uint64_t cache_lookup_us = 0;
        uint64_t loader_queue_wait_us = 0;
        uint64_t source_load_us = 0;
        uint64_t dependency_wait_us = 0;
        uint64_t registration_us = 0;
        uint64_t inclusive_elapsed_us = 0;
    };

    struct AssetLoadSizeCost
    {
        std::optional<uint64_t> source_file_bytes;
        std::optional<uint64_t> decoded_payload_bytes;
    };

    struct AssetLoadObservation
    {
        AssetLoadOperationID operation = 0;
        std::optional<AssetLoadOperationID> parent;
        std::string display_path;
        AssetType expected_type = AssetType::Undefined;
        AssetLoadState state = AssetLoadState::Running;
        AssetLoadPhase phase = AssetLoadPhase::CacheLookup;
        uint32_t completed_children = 0;
        uint32_t known_children = 0;
        std::optional<AssetLoadDisposition> disposition;
        std::optional<AssetID> result;
        AssetLoadTiming timing;
        AssetLoadSizeCost size_cost;
        std::string diagnostic;
    };

    struct AssetLoadCostTotals
    {
        uint64_t cumulative_cache_lookup_us = 0;
        uint64_t cumulative_loader_queue_wait_us = 0;
        uint64_t cumulative_source_load_us = 0;
        uint64_t cumulative_registration_us = 0;
        uint64_t measured_source_file_bytes = 0;
        uint64_t measured_decoded_payload_bytes = 0;
        uint32_t source_file_measurement_count = 0;
        uint32_t decoded_payload_measurement_count = 0;
    };

    struct AssetLoadSummary
    {
        uint32_t operations_started = 0;
        uint32_t operations_active = 0;
        uint32_t operations_succeeded = 0;
        uint32_t operations_failed = 0;
        uint32_t cache_hits = 0;
        uint32_t post_load_deduplications = 0;
        uint64_t wall_elapsed_us = 0;
        AssetLoadCostTotals cost;
        std::string first_failure;
    };

    struct AssetLoadSnapshot
    {
        AssetLoadSessionID session = 0;
        uint64_t revision = 0;
        bool sealed = false;
        bool terminal = false;
        uint32_t omitted_active_operations = 0;
        AssetLoadSummary summary;
        std::vector<AssetLoadObservation> active_operations;
        std::vector<AssetLoadObservation> recent_terminal_operations;
    };

    class AssetLoadSession
    {
    public:
        AssetLoadSession() = default;

        bool IsValid() const noexcept;
        // Closes the observation scope; existing operations and their children
        // remain observable, but new root operations are excluded.
        void Seal() noexcept;
        AssetLoadSnapshot GetSnapshot() const;

    private:
        explicit AssetLoadSession(std::shared_ptr<detail::AssetLoadSessionState> state);

        std::shared_ptr<detail::AssetLoadSessionState> state_;

        friend class AssetManager;
    };
}

#endif
