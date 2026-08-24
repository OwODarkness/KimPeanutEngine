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
