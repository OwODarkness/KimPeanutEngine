#ifndef KPENGINE_EDITOR_ACTOR_ACTOR_EDITOR_MODEL_H
#define KPENGINE_EDITOR_ACTOR_ACTOR_EDITOR_MODEL_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "gameplay/actor/actor_types.h"
#include "gameplay/editor_bridge/gameplay_editor_bridge_types.h"
#include "gameplay/editor_bridge/i_gameplay_editor_bridge.h"
#include "reflection/i_reflection_catalog.h"

namespace kpengine::editor
{
    struct ActorEditorModelConfig
    {
        std::size_t max_string_drafts = 128;
        std::size_t max_string_length = 512;
        std::size_t max_feedback_entries = 256;
        std::size_t transient_feedback_frames = 180;
    };

    enum class ActorEditorFeedbackKind : uint8_t
    {
        None,
        Pending,
        Applied,
        Rejected,
    };

    struct ActorEditorPropertyKey
    {
        gameplay::ActorHandle actor;
        gameplay::ComponentInstanceId component;
        reflection::ReflectionPropertyId property;

        friend bool operator==(const ActorEditorPropertyKey &lhs,
                               const ActorEditorPropertyKey &rhs) noexcept
        {
            return lhs.actor == rhs.actor && lhs.component == rhs.component &&
                   lhs.property == rhs.property;
        }
    };

    struct ActorEditorPropertyKeyHash
    {
        std::size_t operator()(const ActorEditorPropertyKey &key) const noexcept;
    };

    struct ActorEditorPropertyFeedback
    {
        ActorEditorFeedbackKind kind = ActorEditorFeedbackKind::None;
        uint64_t request_id = 0;
        gameplay::PropertyEditSubmissionStatus submission_status =
            gameplay::PropertyEditSubmissionStatus::InvalidArgument;
        gameplay::PropertyEditResultStatus result_status =
            gameplay::PropertyEditResultStatus::ValueRejected;
        std::string diagnostic;
        uint64_t created_frame = 0;
    };

    class ActorEditorModel final
    {
    public:
        ActorEditorModel(const reflection::IReflectionCatalog *catalog,
                         gameplay::IGameplayEditorSnapshotSource *snapshot_source,
                         gameplay::IGameplayEditorEditSink *edit_sink,
                         ActorEditorModelConfig config = {});

        void BeginFrame();

        const std::shared_ptr<const gameplay::GameplayEditorSnapshot> &GetSnapshot() const noexcept
        {
            return snapshot_;
        }

        const reflection::IReflectionCatalog *GetCatalog() const noexcept
        {
            return catalog_;
        }

        std::optional<gameplay::ActorHandle> GetSelection() const noexcept
        {
            return selection_;
        }

        bool SelectActor(gameplay::ActorHandle actor);
        void ClearSelection();

        const gameplay::ActorEditorSnapshot *FindActor(gameplay::ActorHandle actor) const noexcept;

        gameplay::PropertyEditSubmission SubmitPropertyEdit(
            gameplay::ActorHandle actor,
            gameplay::ComponentInstanceId component,
            reflection::ReflectionTypeId expected_type,
            reflection::ReflectionPropertyId property,
            reflection::ReflectionValue value);

        bool IsPropertyPending(const ActorEditorPropertyKey &key) const noexcept;
        const ActorEditorPropertyFeedback *FindPropertyFeedback(
            const ActorEditorPropertyKey &key) const noexcept;

        std::string *FindOrCreateStringDraft(const ActorEditorPropertyKey &key,
                                             std::string_view authoritative_value);
        void ClearStringDraft(const ActorEditorPropertyKey &key);

        uint64_t GetFrameNumber() const noexcept { return frame_number_; }

    private:
        using PendingMap = std::unordered_map<ActorEditorPropertyKey,
                                              uint64_t,
                                              ActorEditorPropertyKeyHash>;
        using RequestMap = std::unordered_map<uint64_t,
                                              ActorEditorPropertyKey>;
        using FeedbackMap = std::unordered_map<ActorEditorPropertyKey,
                                               ActorEditorPropertyFeedback,
                                               ActorEditorPropertyKeyHash>;
        using DraftMap = std::unordered_map<ActorEditorPropertyKey,
                                            std::string,
                                            ActorEditorPropertyKeyHash>;

        bool ContainsActor(gameplay::ActorHandle actor) const noexcept;
        uint64_t AllocateRequestId();
        void ClearActorState(gameplay::ActorHandle actor);
        void RecordFeedback(const ActorEditorPropertyKey &key,
                            ActorEditorPropertyFeedback feedback);
        void ReconcileSelection();

        const reflection::IReflectionCatalog *catalog_ = nullptr;
        gameplay::IGameplayEditorSnapshotSource *snapshot_source_ = nullptr;
        gameplay::IGameplayEditorEditSink *edit_sink_ = nullptr;
        ActorEditorModelConfig config_{};

        std::shared_ptr<const gameplay::GameplayEditorSnapshot> snapshot_;
        std::optional<gameplay::ActorHandle> selection_;
        uint64_t frame_number_ = 0;
        uint64_t next_request_id_ = 1;

        PendingMap pending_;
        RequestMap requests_;
        FeedbackMap feedback_;
        DraftMap drafts_;
    };
}

#endif
