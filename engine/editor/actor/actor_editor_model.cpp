#include "editor/actor/actor_editor_model.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>

namespace kpengine::editor
{
    std::size_t ActorEditorPropertyKeyHash::operator()(
        const ActorEditorPropertyKey &key) const noexcept
    {
        std::size_t hash = std::hash<uint32_t>{}(key.actor.id);
        hash ^= std::hash<uint16_t>{}(key.actor.generation) + static_cast<std::size_t>(0x9e3779b9) +
                (hash << 6U) + (hash >> 2U);
        hash ^= std::hash<uint32_t>{}(key.component.value) + static_cast<std::size_t>(0x9e3779b9) +
                (hash << 6U) + (hash >> 2U);
        hash ^= std::hash<uint32_t>{}(key.property.value) + static_cast<std::size_t>(0x9e3779b9) +
                (hash << 6U) + (hash >> 2U);
        return hash;
    }

    ActorEditorModel::ActorEditorModel(
        const reflection::IReflectionCatalog *catalog,
        gameplay::IGameplayEditorSnapshotSource *snapshot_source,
        gameplay::IGameplayEditorEditSink *edit_sink,
        ActorEditorModelConfig config)
        : catalog_(catalog), snapshot_source_(snapshot_source), edit_sink_(edit_sink),
          config_(config)
    {
        if (config_.max_string_length == 0)
        {
            config_.max_string_length = 1;
        }
        if (config_.max_feedback_entries == 0)
        {
            config_.max_feedback_entries = 1;
        }
    }

    void ActorEditorModel::BeginFrame()
    {
        ++frame_number_;
        snapshot_ = snapshot_source_ != nullptr ? snapshot_source_->GetLatestSnapshot() : nullptr;

        if (snapshot_source_ != nullptr)
        {
            for (const gameplay::PropertyEditResult &result :
                 snapshot_source_->ConsumeEditResults())
            {
                const auto request = requests_.find(result.request_id);
                if (request == requests_.end())
                {
                    continue;
                }

                const ActorEditorPropertyKey key = request->second;
                requests_.erase(request);
                pending_.erase(key);

                ActorEditorPropertyFeedback feedback{};
                feedback.kind = result.status == gameplay::PropertyEditResultStatus::Applied
                                    ? ActorEditorFeedbackKind::Applied
                                    : ActorEditorFeedbackKind::Rejected;
                feedback.request_id = result.request_id;
                feedback.result_status = result.status;
                feedback.diagnostic = result.diagnostic;
                RecordFeedback(key, std::move(feedback));
            }
        }

        for (auto iterator = feedback_.begin(); iterator != feedback_.end();)
        {
            if (frame_number_ > iterator->second.created_frame &&
                frame_number_ - iterator->second.created_frame > config_.transient_feedback_frames)
            {
                iterator = feedback_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }

        ReconcileSelection();
    }

    bool ActorEditorModel::SelectActor(gameplay::ActorHandle actor)
    {
        if (!ContainsActor(actor))
        {
            return false;
        }
        if (!selection_.has_value() || !(*selection_ == actor))
        {
            if (selection_.has_value())
            {
                ClearActorState(*selection_);
            }
            selection_ = actor;
        }
        return true;
    }

    void ActorEditorModel::ClearSelection()
    {
        if (selection_.has_value())
        {
            ClearActorState(*selection_);
            selection_.reset();
        }
    }

    const gameplay::ActorEditorSnapshot *ActorEditorModel::FindActor(
        gameplay::ActorHandle actor) const noexcept
    {
        if (!snapshot_)
        {
            return nullptr;
        }
        const auto iterator = std::find_if(
            snapshot_->actors.begin(), snapshot_->actors.end(),
            [actor](const gameplay::ActorEditorSnapshot &candidate)
            { return candidate.actor == actor; });
        return iterator != snapshot_->actors.end() ? &*iterator : nullptr;
    }

    gameplay::PropertyEditSubmission ActorEditorModel::SubmitPropertyEdit(
        gameplay::ActorHandle actor,
        gameplay::ComponentInstanceId component,
        reflection::ReflectionTypeId expected_type,
        reflection::ReflectionPropertyId property,
        reflection::ReflectionValue value)
    {
        gameplay::PropertyEditSubmission submission{};
        const ActorEditorPropertyKey key{actor, component, property};
        if (edit_sink_ == nullptr || !selection_.has_value() || !(*selection_ == actor) ||
            !ContainsActor(actor) || !component.IsValid() || !expected_type.IsValid() ||
            !property.IsValid())
        {
            submission.status = gameplay::PropertyEditSubmissionStatus::InvalidArgument;
            submission.diagnostic = "Actor editor property target is unavailable";
            RecordFeedback(key, ActorEditorPropertyFeedback{
                                  ActorEditorFeedbackKind::Rejected,
                                  0,
                                  submission.status,
                                  gameplay::PropertyEditResultStatus::ValueRejected,
                                  submission.diagnostic});
            return submission;
        }
        if (pending_.find(key) != pending_.end())
        {
            submission.status = gameplay::PropertyEditSubmissionStatus::InvalidArgument;
            submission.diagnostic = "A property edit is already pending";
            return submission;
        }

        const uint64_t request_id = AllocateRequestId();
        if (request_id == 0)
        {
            submission.status = gameplay::PropertyEditSubmissionStatus::InvalidArgument;
            submission.diagnostic = "Actor editor request IDs are exhausted";
            return submission;
        }

        gameplay::PropertyEditCommand command{};
        command.request_id = request_id;
        command.actor = actor;
        command.component = component;
        command.expected_type = expected_type;
        command.property = property;
        command.value = std::move(value);
        submission = edit_sink_->SubmitPropertyEdit(std::move(command));
        if (submission.IsQueued())
        {
            const uint64_t accepted_id = submission.request_id != 0 ? submission.request_id
                                                                      : request_id;
            pending_[key] = accepted_id;
            requests_[accepted_id] = key;
            ActorEditorPropertyFeedback feedback{};
            feedback.kind = ActorEditorFeedbackKind::Pending;
            feedback.request_id = accepted_id;
            feedback.submission_status = submission.status;
            RecordFeedback(key, std::move(feedback));
            drafts_.erase(key);
        }
        else
        {
            RecordFeedback(key, ActorEditorPropertyFeedback{
                                  ActorEditorFeedbackKind::Rejected,
                                  0,
                                  submission.status,
                                  gameplay::PropertyEditResultStatus::ValueRejected,
                                  submission.diagnostic});
        }
        return submission;
    }

    bool ActorEditorModel::IsPropertyPending(const ActorEditorPropertyKey &key) const noexcept
    {
        return pending_.find(key) != pending_.end();
    }

    const ActorEditorPropertyFeedback *ActorEditorModel::FindPropertyFeedback(
        const ActorEditorPropertyKey &key) const noexcept
    {
        const auto iterator = feedback_.find(key);
        return iterator != feedback_.end() ? &iterator->second : nullptr;
    }

    std::string *ActorEditorModel::FindOrCreateStringDraft(
        const ActorEditorPropertyKey &key, std::string_view authoritative_value)
    {
        auto iterator = drafts_.find(key);
        if (iterator == drafts_.end())
        {
            if (drafts_.size() >= config_.max_string_drafts)
            {
                return nullptr;
            }
            iterator = drafts_.emplace(key, std::string(authoritative_value)).first;
            if (iterator->second.size() > config_.max_string_length)
            {
                iterator->second.resize(config_.max_string_length);
            }
        }
        return &iterator->second;
    }

    void ActorEditorModel::ClearStringDraft(const ActorEditorPropertyKey &key)
    {
        drafts_.erase(key);
    }

    bool ActorEditorModel::ContainsActor(gameplay::ActorHandle actor) const noexcept
    {
        return FindActor(actor) != nullptr;
    }

    uint64_t ActorEditorModel::AllocateRequestId()
    {
        constexpr uint64_t max_id = std::numeric_limits<uint64_t>::max();
        for (uint64_t attempt = 0; attempt < requests_.size() + 1; ++attempt)
        {
            const uint64_t candidate = next_request_id_ == 0 ? 1 : next_request_id_;
            next_request_id_ = candidate == max_id ? 1 : candidate + 1;
            if (candidate != 0 && requests_.find(candidate) == requests_.end())
            {
                return candidate;
            }
        }
        return 0;
    }

    void ActorEditorModel::ClearActorState(gameplay::ActorHandle actor)
    {
        for (auto iterator = pending_.begin(); iterator != pending_.end();)
        {
            if (iterator->first.actor == actor)
            {
                requests_.erase(iterator->second);
                iterator = pending_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        for (auto iterator = feedback_.begin(); iterator != feedback_.end();)
        {
            iterator = iterator->first.actor == actor ? feedback_.erase(iterator) : ++iterator;
        }
        for (auto iterator = drafts_.begin(); iterator != drafts_.end();)
        {
            iterator = iterator->first.actor == actor ? drafts_.erase(iterator) : ++iterator;
        }
    }

    void ActorEditorModel::RecordFeedback(const ActorEditorPropertyKey &key,
                                          ActorEditorPropertyFeedback feedback)
    {
        // request_id doubles as the local creation frame for bounded expiry. Request IDs are
        // never exposed as frame state, so preserve a nonzero local marker for immediate errors.
        if (feedback.request_id == 0)
        {
            feedback.request_id = frame_number_ == 0 ? 1 : frame_number_;
        }
        feedback.created_frame = frame_number_;
        if (feedback_.find(key) == feedback_.end() &&
            feedback_.size() >= config_.max_feedback_entries && !feedback_.empty())
        {
            auto oldest = feedback_.begin();
            for (auto iterator = std::next(feedback_.begin()); iterator != feedback_.end(); ++iterator)
            {
                if (iterator->second.created_frame < oldest->second.created_frame)
                {
                    oldest = iterator;
                }
            }
            feedback_.erase(oldest);
        }
        feedback_[key] = std::move(feedback);
    }

    void ActorEditorModel::ReconcileSelection()
    {
        if (!snapshot_ || !selection_.has_value() || !ContainsActor(*selection_))
        {
            ClearSelection();
        }
    }
}
