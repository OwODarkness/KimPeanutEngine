#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "runtime/engine.h"
#include "runtime/host/application_host.h"
#include "application/scene_3d_host.h"

TEST(Scene3DHostTest, OwnsOnlySceneModeLifecycle)
{
    kpengine::runtime::Engine engine;
    kpengine::runtime::application::Scene3DHost host;
    std::string diagnostic;

    EXPECT_TRUE(host.Initialize(engine, diagnostic)) << diagnostic;
    EXPECT_TRUE(host.Tick(1.0f / 60.0f, diagnostic)) << diagnostic;
    EXPECT_TRUE(host.RecordFrame(diagnostic)) << diagnostic;
    host.Shutdown();
    EXPECT_FALSE(host.RecordFrame(diagnostic));
    EXPECT_EQ(diagnostic, "3DSceneHost is not initialized");
}

TEST(Scene3DHostTest, RejectsAnotherApplicationMode)
{
    std::string registration_diagnostic;
    ASSERT_TRUE(kpengine::runtime::ApplicationHostRegistry::Global().Register(
        {"test-owned-window",
         [](kpengine::runtime::Engine &) {
             return std::unique_ptr<kpengine::runtime::IApplicationHost>{};
         },
         kpengine::runtime::ApplicationHostLayout::OwnedWindow,
         false}, registration_diagnostic)) << registration_diagnostic;

    kpengine::runtime::Engine engine;
    engine.SetApplicationMode("test-owned-window");
    kpengine::runtime::application::Scene3DHost host;
    std::string diagnostic;

    EXPECT_FALSE(host.Initialize(engine, diagnostic));
    EXPECT_EQ(diagnostic, "3DSceneHost can only initialize in scene3d mode");
}
