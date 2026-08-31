#include <gtest/gtest.h>

#include "render/render_pass.h"

namespace
{
    using kpengine::render::RenderPassAccess;
    using kpengine::render::RenderPassDeclaration;
    using kpengine::render::RenderPassResource;
    using kpengine::render::RenderPassResourceUse;
    using kpengine::render::RenderPassSchedule;
}

TEST(RenderPassScheduleTest, AcceptsSceneThenTerminalEditorComposite)
{
    RenderPassSchedule schedule;
    EXPECT_TRUE(schedule.AddPass(
        {"ScenePass", {{RenderPassResource::SceneColor, RenderPassAccess::Write}}, false}));
    EXPECT_TRUE(schedule.AddPass(
        {"EditorCompositePass", {{RenderPassResource::SceneColor, RenderPassAccess::Read}}, true}));

    std::string error;
    EXPECT_TRUE(schedule.Validate(error));
    EXPECT_TRUE(error.empty());
}

TEST(RenderPassScheduleTest, AcceptsGBufferPassChainToTerminalEditorComposite)
{
    RenderPassSchedule schedule;
    EXPECT_TRUE(schedule.AddPass(
        {"ShadowDepthPass", {{RenderPassResource::DirectionalShadow, RenderPassAccess::Write}}, false}));
    EXPECT_TRUE(schedule.AddPass(
        {"GBufferPass", {{RenderPassResource::GBuffer, RenderPassAccess::Write}}, false}));
    EXPECT_TRUE(schedule.AddPass(
        {"DeferredLightingPass",
         {{RenderPassResource::GBuffer, RenderPassAccess::Read},
          {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
          {RenderPassResource::SceneHdr, RenderPassAccess::Write}},
         false}));
    EXPECT_TRUE(schedule.AddPass(
        {"ToneMapPass",
         {{RenderPassResource::SceneHdr, RenderPassAccess::Read},
          {RenderPassResource::SceneColor, RenderPassAccess::Write}},
         false}));
    EXPECT_TRUE(schedule.AddPass(
        {"CaptureViewPass",
         {{RenderPassResource::GBuffer, RenderPassAccess::Read},
          {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
          {RenderPassResource::SceneColor, RenderPassAccess::Read},
          {RenderPassResource::CaptureOutput, RenderPassAccess::Write}},
         false}));
    EXPECT_TRUE(schedule.AddPass(
        {"EditorCompositePass", {{RenderPassResource::SceneColor, RenderPassAccess::Read}}, true}));

    std::string error;
    EXPECT_TRUE(schedule.Validate(error));
    EXPECT_TRUE(error.empty());
}

TEST(RenderPassScheduleTest, AcceptsDirectionalShadowBeforeGBuffer)
{
    RenderPassSchedule schedule;
    EXPECT_TRUE(schedule.AddPass(
        {"ShadowDepthPass", {{RenderPassResource::DirectionalShadow, RenderPassAccess::Write}}, false}));
    EXPECT_TRUE(schedule.AddPass(
        {"GBufferPass", {{RenderPassResource::GBuffer, RenderPassAccess::Write}}, false}));
    EXPECT_TRUE(schedule.AddPass(
        {"DeferredLightingPass",
         {{RenderPassResource::GBuffer, RenderPassAccess::Read},
          {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
          {RenderPassResource::SceneHdr, RenderPassAccess::Write}},
         false}));
    EXPECT_TRUE(schedule.AddPass(
        {"ToneMapPass",
         {{RenderPassResource::SceneHdr, RenderPassAccess::Read},
          {RenderPassResource::SceneColor, RenderPassAccess::Write}},
         false}));
    EXPECT_TRUE(schedule.AddPass(
        {"CaptureViewPass",
         {{RenderPassResource::GBuffer, RenderPassAccess::Read},
          {RenderPassResource::DirectionalShadow, RenderPassAccess::Read},
          {RenderPassResource::SceneColor, RenderPassAccess::Read},
          {RenderPassResource::CaptureOutput, RenderPassAccess::Write}},
         false}));
    EXPECT_TRUE(schedule.AddPass(
        {"EditorCompositePass", {{RenderPassResource::SceneColor, RenderPassAccess::Read}}, true}));
    std::string error;
    EXPECT_TRUE(schedule.Validate(error));
}

TEST(RenderPassScheduleTest, RejectsReadOfGBufferBeforeWriter)
{
    RenderPassSchedule schedule;
    EXPECT_TRUE(schedule.AddPass(
        {"DeferredLightingPass",
         {{RenderPassResource::GBuffer, RenderPassAccess::Read},
          {RenderPassResource::SceneColor, RenderPassAccess::Write}},
         true}));

    std::string error;
    EXPECT_FALSE(schedule.Validate(error));
    EXPECT_FALSE(error.empty());
}

TEST(RenderPassScheduleTest, RejectsToneMapBeforeSceneHdrWriter)
{
    RenderPassSchedule schedule;
    EXPECT_TRUE(schedule.AddPass(
        {"ToneMapPass",
         {{RenderPassResource::SceneHdr, RenderPassAccess::Read},
          {RenderPassResource::SceneColor, RenderPassAccess::Write}},
         true}));

    std::string error;
    EXPECT_FALSE(schedule.Validate(error));
    EXPECT_FALSE(error.empty());
}

TEST(RenderPassScheduleTest, RejectsEmptySchedule)
{
    RenderPassSchedule schedule;

    std::string error;
    EXPECT_FALSE(schedule.Validate(error));
    EXPECT_FALSE(error.empty());
}

TEST(RenderPassScheduleTest, RejectsReadBeforeWriter)
{
    RenderPassSchedule schedule;
    EXPECT_TRUE(schedule.AddPass(
        {"EditorCompositePass", {{RenderPassResource::SceneColor, RenderPassAccess::Read}}, true}));

    std::string error;
    EXPECT_FALSE(schedule.Validate(error));
    EXPECT_FALSE(error.empty());
}

TEST(RenderPassScheduleTest, RejectsPassAfterTerminalPass)
{
    RenderPassSchedule schedule;
    EXPECT_TRUE(schedule.AddPass(
        {"ScenePass", {{RenderPassResource::SceneColor, RenderPassAccess::Write}}, true}));
    EXPECT_TRUE(schedule.AddPass(
        {"EditorCompositePass", {{RenderPassResource::SceneColor, RenderPassAccess::Read}}, false}));

    std::string error;
    EXPECT_FALSE(schedule.Validate(error));
    EXPECT_FALSE(error.empty());
}
