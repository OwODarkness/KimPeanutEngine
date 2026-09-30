#ifndef KPENGINE_TERRAIN_GENERATION_H
#define KPENGINE_TERRAIN_GENERATION_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <mutex>

#include <nlohmann/json.hpp>

#include "product/terrain_core.h"

namespace kpengine::terrain
{
    enum class PortType : std::uint8_t { ScalarField2D, Heightfield };
    using TerrainValue = std::shared_ptr<const ScalarField2D>;
    using PortTypes = std::map<std::string, PortType, std::less<>>;

    struct OutputRef
    {
        std::string node;
        std::string port;
    };

    struct RecipeNode
    {
        std::string id;
        std::string operator_id;
        std::uint32_t operator_version = 0;
        nlohmann::json parameters = nlohmann::json::object();
        std::map<std::string, OutputRef, std::less<>> inputs;
    };

    struct TerrainRecipe
    {
        std::uint32_t schema_version = 1;
        std::uint64_t seed = 0;
        GridDomain2D domain;
        std::vector<RecipeNode> nodes;

        nlohmann::json ToJson() const;
        static TerrainRecipe FromJson(const nlohmann::json &json);
    };

    struct OperatorContext
    {
        const GridDomain2D &domain;
        std::uint64_t seed;
        std::size_t maximum_samples;
        std::uint64_t node_seed;
        const std::atomic_bool *cancelled;
    };

    using OperatorInputs = std::map<std::string, TerrainValue, std::less<>>;
    using OperatorOutputs = std::map<std::string, TerrainValue, std::less<>>;
    using OperatorFunction = std::function<OperatorOutputs(
        const OperatorContext &, const nlohmann::json &, const OperatorInputs &)>;

    struct OperatorDescriptor
    {
        std::string id;
        std::uint32_t version = 0;
        PortTypes inputs;
        PortTypes outputs;
        OperatorFunction evaluate;
    };

    class OperatorRegistry final
    {
    public:
        bool Register(OperatorDescriptor descriptor, std::string &diagnostic);
        const OperatorDescriptor *Find(std::string_view id, std::uint32_t version) const;
        bool RegisterBuiltins(std::string &diagnostic);

    private:
        std::map<std::pair<std::string, std::uint32_t>, OperatorDescriptor> descriptors_;
    };

    struct EvaluationOptions
    {
        std::size_t maximum_samples = 4u * 1024u * 1024u;
        std::size_t maximum_nodes = 4096;
        std::size_t maximum_result_bytes = 512u * 1024u * 1024u;
        std::size_t maximum_cached_nodes = 128;
        std::size_t maximum_cache_bytes = 256u * 1024u * 1024u;
    };

    struct NodeResult
    {
        std::map<std::string, TerrainValue, std::less<>> outputs;
        std::uint64_t content_hash = 0;
        double evaluation_time_ms = 0.0;
        std::size_t output_bytes = 0;
        float minimum_value = 0.0f;
        float maximum_value = 0.0f;
        bool all_values_finite = true;
        bool cache_hit = false;
    };
    using EvaluationProgressCallback =
        std::function<void(std::string_view, const NodeResult &)>;

    class EvaluationExecutionControl final
    {
    public:
        void Pause();
        void Resume();
        void Step();
        bool IsPaused() const;
        bool WaitForNode(const std::atomic_bool *cancelled);

    private:
        mutable std::mutex mutex_;
        std::condition_variable wake_;
        bool paused_ = false;
        std::size_t step_tokens_ = 0;
    };

    struct EvaluationResult
    {
        bool succeeded = false;
        bool cancelled = false;
        std::string diagnostic;
        double evaluation_time_ms = 0.0;
        std::map<std::string, NodeResult, std::less<>> nodes;
    };

    class TerrainEvaluator final
    {
    public:
        TerrainEvaluator(std::shared_ptr<const OperatorRegistry> registry,
                         EvaluationOptions options = {});
        EvaluationResult Evaluate(const TerrainRecipe &recipe,
                                  const std::atomic_bool *cancelled = nullptr,
                                  EvaluationExecutionControl *control = nullptr,
                                  const EvaluationProgressCallback &progress = {});
        void ClearCache();

    private:
        struct CacheEntry { NodeResult result; std::size_t bytes = 0; };
        std::shared_ptr<const OperatorRegistry> registry_;
        EvaluationOptions options_;
        std::map<std::uint64_t, CacheEntry> cache_;
        std::size_t cache_bytes_ = 0;
    };

    struct GenerationJobResult
    {
        std::uint64_t revision = 0;
        EvaluationResult evaluation;
    };

    class GenerationExecutor final
    {
    public:
        GenerationExecutor(std::shared_ptr<const OperatorRegistry> registry,
                           std::size_t worker_count, std::size_t pending_capacity,
                           std::size_t result_capacity, EvaluationOptions options = {});
        ~GenerationExecutor();
        GenerationExecutor(const GenerationExecutor &) = delete;
        GenerationExecutor &operator=(const GenerationExecutor &) = delete;

        bool Submit(std::uint64_t revision, TerrainRecipe recipe,
                    std::shared_ptr<EvaluationExecutionControl> control = {},
                    EvaluationProgressCallback progress = {});
        bool TryPop(GenerationJobResult &result);
        void CancelBefore(std::uint64_t revision);
        void Shutdown() noexcept;

    private:
        struct Job
        {
            std::uint64_t revision;
            TerrainRecipe recipe;
            std::shared_ptr<std::atomic_bool> cancelled;
            std::shared_ptr<EvaluationExecutionControl> control;
            EvaluationProgressCallback progress;
        };
        void WorkerLoop();

        std::shared_ptr<const OperatorRegistry> registry_;
        EvaluationOptions options_;
        const std::size_t pending_capacity_;
        const std::size_t result_capacity_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::deque<Job> pending_;
        std::deque<GenerationJobResult> completed_;
        std::vector<std::pair<std::uint64_t, std::shared_ptr<std::atomic_bool>>> active_;
        std::vector<std::thread> workers_;
        std::uint64_t minimum_revision_ = 0;
        bool stopping_ = false;
    };
}

#endif
