#ifndef KPENGINE_RUNTIME_GAMEPLAY_EDITOR_BRIDGE_GAMEPLAY_EDITOR_BRIDGE_H
#define KPENGINE_RUNTIME_GAMEPLAY_EDITOR_BRIDGE_GAMEPLAY_EDITOR_BRIDGE_H

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "gameplay/editor_bridge/i_gameplay_editor_bridge.h"
#include "gameplay/reflection/gameplay_reflection.h"

namespace kpengine::reflection
{
    class IReflectionAccess;
    class IReflectionCatalog;
}

namespace kpengine::gameplay
{
    class GameplayWorld;
    class Actor;
    class ActorComponent;

    class GameplayEditorBridge final : public IGameplayEditorSnapshotSource,
                                       public IGameplayEditorEditSink
    {
    public:
        enum class State : uint8_t
        {
            Constructed,
            Running,
            Stopping,
            Stopped,
        };

        GameplayEditorBridge(GameplayWorld &world,
                             const reflection::IReflectionCatalog &catalog,
                             const reflection::IReflectionAccess &access,
                             GameplayEditorBridgeConfig config = {});
        ~GameplayEditorBridge() override;

        GameplayEditorBridge(const GameplayEditorBridge &) = delete;
        GameplayEditorBridge &operator=(const GameplayEditorBridge &) = delete;
        GameplayEditorBridge(GameplayEditorBridge &&) = delete;
        GameplayEditorBridge &operator=(GameplayEditorBridge &&) = delete;

        reflection::ReflectionResult Initialize();
        void PumpEdits();
        void PublishSnapshot();
        void Shutdown() noexcept;

        State GetState() const noexcept;
        const GameplayEditorBridgeConfig &GetConfig() const noexcept { return config_; }

        std::shared_ptr<const GameplayEditorSnapshot>
        GetLatestSnapshot() const override;
        std::vector<PropertyEditResult> ConsumeEditResults() override;
        PropertyEditSubmission SubmitPropertyEdit(PropertyEditCommand command) override;

    private:
        struct ResolvedBinding
        {
            GameplayReflectionBinding binding;
            reflection::ReflectionTypeId type;
        };

        PropertyEditResult ApplyEdit(const PropertyEditCommand &command);
        const ResolvedBinding *FindBinding(const ActorComponent &component) const;
        const ResolvedBinding *FindBinding(reflection::ReflectionTypeId type) const;
        void AppendActorSnapshot(const Actor &actor,
                                 GameplayEditorSnapshot &snapshot,
                                 std::size_t &component_count,
                                 std::size_t &property_count,
                                 std::size_t &value_bytes);
        void CopyStringWithinBudget(std::string_view source,
                                    std::string &destination,
                                    GameplayEditorSnapshot &snapshot,
                                    std::size_t &value_bytes) const;
        std::size_t CountReadableProperties(const ActorComponent &component) const;
        static std::size_t EstimateValueBytes(const reflection::ReflectionValue &value) noexcept;
        static std::string MakeActorDisplayName(ActorHandle handle);

        GameplayWorld &world_;
        const reflection::IReflectionCatalog &catalog_;
        const reflection::IReflectionAccess &access_;
        GameplayEditorBridgeConfig config_;
        State state_ = State::Constructed;
        std::thread::id game_thread_id_;
        std::vector<ResolvedBinding> bindings_;
        uint64_t next_snapshot_revision_ = 1;

        mutable std::mutex mutex_;
        std::condition_variable processing_condition_;
        std::deque<PropertyEditCommand> pending_edits_;
        std::deque<PropertyEditResult> edit_results_;
        std::unordered_set<uint64_t> outstanding_request_ids_;
        std::size_t outstanding_edits_ = 0;
        bool processing_edit_ = false;
        bool building_snapshot_ = false;
        std::shared_ptr<const GameplayEditorSnapshot> latest_snapshot_;
    };
}

#endif
