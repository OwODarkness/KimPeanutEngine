#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "render/render_pass.h"

namespace
{
    using kpengine::render::FixedRenderPassEntry;
    using kpengine::render::FixedRenderPassFrame;
    using kpengine::render::FixedRenderPassId;
    using kpengine::render::FixedRenderPassSequence;
    using kpengine::render::RenderPassAccess;
    using kpengine::render::RenderPassCondition;
    using kpengine::render::RenderPassExecutionOwner;
    using kpengine::render::RenderPassOutcome;
    using kpengine::render::RenderPassResource;
    using kpengine::render::RenderPassResourceUse;

    std::vector<FixedRenderPassEntry> MakeCanonicalEntries()
    {
        return {
            {FixedRenderPassId::DirectionalShadow, "DirectionalShadowPass",
             {{RenderPassResource::DirectionalShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::SpotShadow, "SpotShadowPass",
             {{RenderPassResource::SpotShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::PointShadow, "PointShadowPass",
             {{RenderPassResource::PointShadow, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::GBuffer, "GBufferPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::DeferredLighting, "DeferredLightingPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
              {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
              {RenderPassResource::SpotShadow, RenderPassAccess::Read},
              {RenderPassResource::PointShadow, RenderPassAccess::Read},
              {RenderPassResource::SceneHdr, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::ToneMap, "ToneMapPass",
             {{RenderPassResource::SceneHdr, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer, RenderPassCondition::Always, false},
            {FixedRenderPassId::CaptureView, "CaptureViewPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
              {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
              {RenderPassResource::SpotShadow, RenderPassAccess::Read},
              {RenderPassResource::PointShadow, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Read},
              {RenderPassResource::CaptureOutput, RenderPassAccess::Write}},
             RenderPassExecutionOwner::Renderer,
             RenderPassCondition::DiagnosticCaptureRequested, false},
            {FixedRenderPassId::EditorComposite, "EditorCompositePass",
             {{RenderPassResource::SceneColor, RenderPassAccess::Read}},
             RenderPassExecutionOwner::External, RenderPassCondition::ExternalRequest, true},
        };
    }

    std::optional<FixedRenderPassSequence> MakeCanonical(std::string &error)
    {
        return FixedRenderPassSequence::Create(MakeCanonicalEntries(), error);
    }
}

TEST(FixedRenderPassSequenceTest, AcceptsCanonicalEightEntrySequence)
{
    std::string error;
    const auto sequence = MakeCanonical(error);
    ASSERT_TRUE(sequence.has_value()) << error;
    EXPECT_EQ(sequence->Entries().size(),
              static_cast<std::size_t>(FixedRenderPassId::Count));
}

TEST(FixedRenderPassSequenceTest, RejectsMissingOrDuplicateTypedIdentity)
{
    auto entries = MakeCanonicalEntries();
    entries.back().id = FixedRenderPassId::ToneMap;
    std::string error;
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(FixedRenderPassSequenceTest, RejectsUniqueIDsInTheWrongCanonicalOrdinal)
{
    auto entries = MakeCanonicalEntries();
    std::swap(entries[3].id, entries[5].id);
    std::string error;
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());
    EXPECT_NE(error.find("canonical ordinal"), std::string::npos);
}

TEST(FixedRenderPassSequenceTest, RejectsDuplicateHumanNameAndResourceUse)
{
    auto entries = MakeCanonicalEntries();
    entries[1].name = entries[0].name;
    std::string error;
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());

    entries = MakeCanonicalEntries();
    entries[0].resources.push_back(entries[0].resources.front());
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());
}

TEST(FixedRenderPassSequenceTest, RejectsReadBeforeWriterAndDuplicateWriter)
{
    auto entries = MakeCanonicalEntries();
    entries[4].resources.front().resource = RenderPassResource::CaptureOutput;
    std::string error;
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());

    entries = MakeCanonicalEntries();
    entries[5].resources[1] =
        {RenderPassResource::SceneHdr, RenderPassAccess::Write};
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());
}

TEST(FixedRenderPassSequenceTest, RejectsConditionalWriterDependency)
{
    auto entries = MakeCanonicalEntries();
    entries[6].resources = {
        {RenderPassResource::GBuffer, RenderPassAccess::Read},
        {RenderPassResource::CaptureOutput, RenderPassAccess::Write}};
    entries[7].resources = {{RenderPassResource::CaptureOutput, RenderPassAccess::Read}};
    entries[7].condition = RenderPassCondition::ExternalRequest;
    std::string error;
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());

    entries = MakeCanonicalEntries();
    entries[6].resources = {{RenderPassResource::CaptureOutput, RenderPassAccess::Write}};
    entries[4].resources.push_back(
        {RenderPassResource::CaptureOutput, RenderPassAccess::Read});
    std::swap(entries[4], entries[6]);
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());
}

TEST(FixedRenderPassSequenceTest, RejectsInvalidExternalAndTerminalRules)
{
    auto entries = MakeCanonicalEntries();
    entries[7].terminal = false;
    std::string error;
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());

    entries = MakeCanonicalEntries();
    entries[7].owner = RenderPassExecutionOwner::Renderer;
    EXPECT_FALSE(FixedRenderPassSequence::Create(std::move(entries), error).has_value());
}

TEST(FixedRenderPassFrameTest, VisitsCanonicalOrderAndSkipsUnrequestedCapture)
{
    std::string error;
    auto sequence = MakeCanonical(error);
    ASSERT_TRUE(sequence.has_value()) << error;
    FixedRenderPassFrame frame(*sequence, false);
    std::vector<FixedRenderPassId> visited;
    ASSERT_TRUE(frame.ExecuteRenderer([&](FixedRenderPassId id) {
        visited.push_back(id);
        return true;
    }));
    ASSERT_EQ(visited.size(), 6U);
    EXPECT_EQ(visited[0], FixedRenderPassId::DirectionalShadow);
    EXPECT_EQ(visited[5], FixedRenderPassId::ToneMap);
    EXPECT_EQ(frame.GetOutcome(FixedRenderPassId::CaptureView),
              RenderPassOutcome::SkippedCondition);
    EXPECT_EQ(frame.GetOutcome(FixedRenderPassId::EditorComposite), RenderPassOutcome::Pending);
    ASSERT_TRUE(frame.Finalize(error)) << error;
    EXPECT_EQ(frame.GetOutcome(FixedRenderPassId::EditorComposite),
              RenderPassOutcome::SkippedExternal);
    EXPECT_FALSE(frame.ExecuteRenderer([](FixedRenderPassId) { return true; }));
}

TEST(FixedRenderPassFrameTest, ContinuesAfterRequiredFailureAndRecordsOutcome)
{
    std::string error;
    auto sequence = MakeCanonical(error);
    ASSERT_TRUE(sequence.has_value()) << error;
    FixedRenderPassFrame frame(*sequence, false);
    ASSERT_TRUE(frame.ExecuteRenderer([](FixedRenderPassId id) {
        return id != FixedRenderPassId::SpotShadow;
    }));
    EXPECT_EQ(frame.GetOutcome(FixedRenderPassId::SpotShadow), RenderPassOutcome::Failed);
    EXPECT_EQ(frame.GetOutcome(FixedRenderPassId::GBuffer), RenderPassOutcome::Executed);
    EXPECT_TRUE(frame.HasRequiredFailure());
    ASSERT_TRUE(frame.Finalize(error)) << error;
}

TEST(FixedRenderPassFrameTest, ExternalTerminalIsExactlyOnceAndCannotRunPrematurely)
{
    std::string error;
    auto sequence = MakeCanonical(error);
    ASSERT_TRUE(sequence.has_value()) << error;
    FixedRenderPassFrame frame(*sequence, false);
    EXPECT_FALSE(frame.ExecuteExternal([] {}));
    ASSERT_TRUE(frame.ExecuteRenderer([](FixedRenderPassId) { return true; }));
    int callback_count = 0;
    ASSERT_TRUE(frame.ExecuteExternal([&] { ++callback_count; }));
    EXPECT_FALSE(frame.ExecuteExternal([&] { ++callback_count; }));
    EXPECT_EQ(callback_count, 1);
    ASSERT_TRUE(frame.Finalize(error)) << error;
    EXPECT_TRUE(frame.IsFinalized());
    EXPECT_FALSE(frame.ExecuteExternal([] {}));
}

TEST(FixedRenderPassFrameTest, ExecutesDiagnosticCaptureOnlyWhenRequested)
{
    std::string error;
    auto sequence = MakeCanonical(error);
    ASSERT_TRUE(sequence.has_value()) << error;
    FixedRenderPassFrame frame(*sequence, true);
    ASSERT_TRUE(frame.ExecuteRenderer([](FixedRenderPassId) { return true; }));
    EXPECT_EQ(frame.GetOutcome(FixedRenderPassId::CaptureView), RenderPassOutcome::Executed);
    ASSERT_TRUE(frame.Finalize(error)) << error;
}

TEST(FixedRenderPassFrameTest, MovingFrameInvalidatesTheMovedFromCursor)
{
    std::string error;
    auto sequence = MakeCanonical(error);
    ASSERT_TRUE(sequence.has_value()) << error;
    FixedRenderPassFrame frame(*sequence, false);
    FixedRenderPassFrame moved(std::move(frame));
    int moved_callback_count = 0;
    ASSERT_TRUE(moved.ExecuteRenderer([&](FixedRenderPassId) {
        ++moved_callback_count;
        return true;
    }));
    EXPECT_EQ(moved_callback_count, 6);
    EXPECT_FALSE(frame.ExecuteRenderer([](FixedRenderPassId) { return true; }));
    EXPECT_FALSE(frame.ExecuteExternal([] {}));
    EXPECT_FALSE(frame.Finalize(error));
}

TEST(FixedRenderPassSequenceTest, MovingSequenceInvalidatesTheMovedFromValue)
{
    std::string error;
    auto sequence = MakeCanonical(error);
    ASSERT_TRUE(sequence.has_value()) << error;
    FixedRenderPassSequence moved(std::move(*sequence));
    EXPECT_TRUE(moved.IsValid());
    EXPECT_FALSE(sequence->IsValid());
}
