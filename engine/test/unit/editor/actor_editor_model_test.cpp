#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "editor/actor/actor_editor_model.h"
#include "editor/actor/actor_property_widget.h"

namespace
{
    using namespace kpengine;

    class SnapshotSource final : public gameplay::IGameplayEditorSnapshotSource
    {
    public:
        std::shared_ptr<const gameplay::GameplayEditorSnapshot> snapshot;
        std::vector<gameplay::PropertyEditResult> results;
        mutable int snapshot_reads = 0;
        int result_reads = 0;

        std::shared_ptr<const gameplay::GameplayEditorSnapshot>
        GetLatestSnapshot() const override
        {
            ++snapshot_reads;
            return snapshot;
        }

        std::vector<gameplay::PropertyEditResult> ConsumeEditResults() override
        {
            ++result_reads;
            std::vector<gameplay::PropertyEditResult> consumed;
            consumed.swap(results);
            return consumed;
        }
    };

    class EditSink final : public gameplay::IGameplayEditorEditSink
    {
    public:
        gameplay::PropertyEditSubmissionStatus status =
            gameplay::PropertyEditSubmissionStatus::Queued;
        std::vector<gameplay::PropertyEditCommand> commands;

        gameplay::PropertyEditSubmission SubmitPropertyEdit(
            gameplay::PropertyEditCommand command) override
        {
            commands.push_back(command);
            gameplay::PropertyEditSubmission submission{};
            submission.status = status;
            submission.request_id = command.request_id;
            submission.diagnostic = status == gameplay::PropertyEditSubmissionStatus::Queued
                                        ? std::string{}
                                        : std::string{"synthetic queue failure"};
            return submission;
        }
    };

    std::shared_ptr<gameplay::GameplayEditorSnapshot> MakeSnapshot(
        gameplay::ActorHandle actor)
    {
        auto snapshot = std::make_shared<gameplay::GameplayEditorSnapshot>();
        snapshot->revision = 1;
        gameplay::ActorEditorSnapshot actor_snapshot{};
        actor_snapshot.actor = actor;
        actor_snapshot.display_name = "Actor";
        snapshot->actors.push_back(std::move(actor_snapshot));
        return snapshot;
    }

    reflection::ReflectionPropertyDescriptor MakeDescriptor(
        reflection::ReflectionValueType type,
        reflection::ReflectionPropertyFlags flags)
    {
        reflection::ReflectionPropertyDescriptor descriptor{};
        descriptor.id = {1};
        descriptor.name = "Value";
        descriptor.value_type = type;
        descriptor.flags = flags;
        return descriptor;
    }

    gameplay::PropertyValueSnapshot MakeValue(reflection::ReflectionValue value)
    {
        gameplay::PropertyValueSnapshot snapshot{};
        snapshot.property = {1};
        snapshot.value = std::move(value);
        return snapshot;
    }
}

TEST(ActorEditorModelTest, LoadsOncePerFrameAndReconcilesFullHandle)
{
    SnapshotSource source;
    EditSink sink;
    const gameplay::ActorHandle actor{4, 9};
    const std::shared_ptr<gameplay::GameplayEditorSnapshot> first = MakeSnapshot(actor);
    source.snapshot = first;
    editor::ActorEditorModel model(nullptr, &source, &sink);

    model.BeginFrame();
    EXPECT_EQ(source.snapshot_reads, 1);
    EXPECT_EQ(source.result_reads, 1);
    EXPECT_TRUE(model.SelectActor(actor));
    EXPECT_TRUE(model.GetSelection().has_value());

    const std::shared_ptr<gameplay::GameplayEditorSnapshot> refreshed = MakeSnapshot(actor);
    refreshed->revision = 2;
    source.snapshot = refreshed;
    model.BeginFrame();
    EXPECT_EQ(source.snapshot_reads, 2);
    EXPECT_TRUE(model.GetSelection().has_value());

    source.snapshot = MakeSnapshot(gameplay::ActorHandle{4, 10});
    model.BeginFrame();
    EXPECT_FALSE(model.GetSelection().has_value());
}

TEST(ActorEditorModelTest, CorrelatesPendingAppliedAndImmediateFailures)
{
    SnapshotSource source;
    EditSink sink;
    const gameplay::ActorHandle actor{2, 1};
    source.snapshot = MakeSnapshot(actor);
    editor::ActorEditorModel model(nullptr, &source, &sink);
    model.BeginFrame();
    ASSERT_TRUE(model.SelectActor(actor));

    const editor::ActorEditorPropertyKey key{actor, {3}, {7}};
    const gameplay::PropertyEditSubmission queued = model.SubmitPropertyEdit(
        actor, key.component, {12}, key.property, reflection::ReflectionValue{4.0});
    EXPECT_TRUE(queued.IsQueued());
    EXPECT_TRUE(model.IsPropertyPending(key));
    EXPECT_EQ(sink.commands.size(), 1U);
    EXPECT_EQ(model.SubmitPropertyEdit(actor, key.component, {12}, key.property,
                                       reflection::ReflectionValue{5.0})
                  .status,
              gameplay::PropertyEditSubmissionStatus::InvalidArgument);

    gameplay::PropertyEditResult result{};
    result.request_id = queued.request_id;
    result.status = gameplay::PropertyEditResultStatus::Applied;
    source.results.push_back(result);
    model.BeginFrame();
    EXPECT_EQ(source.result_reads, 2);
    EXPECT_FALSE(model.IsPropertyPending(key));
    ASSERT_NE(model.FindPropertyFeedback(key), nullptr);
    EXPECT_EQ(model.FindPropertyFeedback(key)->kind, editor::ActorEditorFeedbackKind::Applied);

    sink.status = gameplay::PropertyEditSubmissionStatus::QueueFull;
    const editor::ActorEditorPropertyKey failed_key{actor, {3}, {8}};
    const gameplay::PropertyEditSubmission failed = model.SubmitPropertyEdit(
        actor, failed_key.component, {12}, failed_key.property, reflection::ReflectionValue{2});
    EXPECT_EQ(failed.status, gameplay::PropertyEditSubmissionStatus::QueueFull);
    EXPECT_FALSE(model.IsPropertyPending(failed_key));
    EXPECT_EQ(model.FindPropertyFeedback(failed_key)->kind,
              editor::ActorEditorFeedbackKind::Rejected);
}

TEST(ActorPropertyWidgetTest, SelectsSafePolicyForSupportedAndInvalidValues)
{
    const auto editable = reflection::ReflectionPropertyFlags::Readable |
                           reflection::ReflectionPropertyFlags::Writable |
                           reflection::ReflectionPropertyFlags::EditorVisible;
    const auto read_only = reflection::ReflectionPropertyFlags::Readable |
                           reflection::ReflectionPropertyFlags::EditorVisible;

    const auto boolean = editor::ResolveActorPropertyWidget(
        MakeDescriptor(reflection::ReflectionValueType::Bool, editable), MakeValue(true));
    EXPECT_EQ(boolean.kind, editor::ActorPropertyWidgetKind::Checkbox);
    EXPECT_TRUE(boolean.editable);

    const auto integer = editor::ResolveActorPropertyWidget(
        MakeDescriptor(reflection::ReflectionValueType::SignedInteger, read_only),
        MakeValue(int64_t{3}));
    EXPECT_EQ(integer.kind, editor::ActorPropertyWidgetKind::SignedInteger);
    EXPECT_FALSE(integer.editable);

    auto enum_descriptor = MakeDescriptor(reflection::ReflectionValueType::SignedInteger, editable);
    enum_descriptor.metadata.semantic = reflection::ReflectionWidgetSemantic::Enum;
    enum_descriptor.metadata.enum_options.push_back({1, "One"});
    const auto enumeration = editor::ResolveActorPropertyWidget(enum_descriptor,
                                                                 MakeValue(int64_t{1}));
    EXPECT_EQ(enumeration.kind, editor::ActorPropertyWidgetKind::EnumCombo);
    EXPECT_TRUE(enumeration.editable);

    const auto mismatch = editor::ResolveActorPropertyWidget(
        MakeDescriptor(reflection::ReflectionValueType::FloatingPoint, editable),
        MakeValue(int64_t{1}));
    EXPECT_FALSE(mismatch.editable);
    EXPECT_FALSE(mismatch.diagnostic.empty());
}
