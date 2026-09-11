#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine.h"
#include "host/scene_3d_host.h"
#include "launch_options.h"

namespace
{
    kpengine::runtime::RuntimeLaunchOptionsParseResult Parse(
        std::initializer_list<std::string_view> arguments)
    {
        return kpengine::runtime::ParseRuntimeLaunchOptions(
            std::vector<std::string_view>{arguments});
    }

    kpengine::runtime::RuntimeLaunchOptionsParseResult ParseStrings(
        const std::vector<std::string> &arguments)
    {
        std::vector<std::string_view> views;
        views.reserve(arguments.size());
        for (const std::string &argument : arguments)
        {
            views.emplace_back(argument);
        }
        return kpengine::runtime::ParseRuntimeLaunchOptions(views);
    }
}

TEST(RuntimeLaunchOptionsTest, DefaultsPreserveExistingLaunchBehavior)
{
    const auto result = Parse({});

    ASSERT_TRUE(result) << result.diagnostic;
    EXPECT_EQ(result.options.application_mode,
              kpengine::runtime::ApplicationMode::Scene3D);
    EXPECT_EQ(result.options.graphics_api_type,
              kpengine::GraphicsAPIType::GRAPHICS_API_UNKNOW);
    EXPECT_FALSE(result.options.command_transport_config.enabled);
    EXPECT_FALSE(result.options.startup_level_override.has_value());
    EXPECT_FALSE(result.options.startup_capture_override.has_value());
}

TEST(RuntimeLaunchOptionsTest, ParsesOptionsInAnyOrderAndNormalizesLevel)
{
    const auto result = Parse({"--startup-level", "level\\.\\point_shadow_validation.level",
                               "--agent-port", "37373", "--graphics-api", "vulkan",
                               "--mode", "scene3d"});

    ASSERT_TRUE(result) << result.diagnostic;
    EXPECT_EQ(result.options.application_mode,
              kpengine::runtime::ApplicationMode::Scene3D);
    EXPECT_EQ(result.options.graphics_api_type,
              kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN);
    ASSERT_TRUE(result.options.command_transport_config.enabled);
    EXPECT_EQ(result.options.command_transport_config.port, 37373);
    ASSERT_TRUE(result.options.startup_level_override.has_value());
    EXPECT_EQ(*result.options.startup_level_override,
              "level/point_shadow_validation.level");
}

TEST(RuntimeLaunchOptionsTest, ParsesViewerCaptureAndRejectsSceneOnlyOptions)
{
    const auto result = Parse({"--capture", "save/screenshots/validation/hiyori.png",
                               "--graphics-api", "vulkan", "--mode", "live2d-viewer"});

    ASSERT_TRUE(result) << result.diagnostic;
    ASSERT_TRUE(result.options.startup_capture_override.has_value());
    EXPECT_EQ(*result.options.startup_capture_override,
              "save/screenshots/validation/hiyori.png");

    const auto startup_level = Parse({"--mode", "live2d-viewer", "--startup-level",
                                      "level/pbr_showcase.level"});
    EXPECT_FALSE(startup_level);
    EXPECT_NE(startup_level.diagnostic.find("--startup-level"), std::string::npos);

    const auto agent_port = Parse({"--agent-port", "37373", "--mode", "live2d-viewer"});
    EXPECT_FALSE(agent_port);
    EXPECT_NE(agent_port.diagnostic.find("--agent-port"), std::string::npos);

    const auto scene_capture = Parse({"--mode", "scene3d", "--capture",
                                      "save/screenshots/validation/scene.png"});
    EXPECT_FALSE(scene_capture);
    EXPECT_NE(scene_capture.diagnostic.find("--capture"), std::string::npos);
}

TEST(RuntimeLaunchOptionsTest, RejectsUnknownOptions)
{
    const auto result = Parse({"--startup-leevl", "level/pbr_showcase.level"});

    EXPECT_FALSE(result);
    EXPECT_NE(result.diagnostic.find("--startup-leevl"), std::string::npos);
}

TEST(RuntimeLaunchOptionsTest, RejectsMissingAndInvalidValues)
{
    const std::vector<std::vector<std::string>> invalid_arguments{
        {"--agent-port"},
        {"--agent-port", "0"},
        {"--agent-port", "37373x"},
        {"--graphics-api"},
        {"--graphics-api", "metal"},
        {"--mode"},
        {"--mode", "editor"},
        {"--startup-level"},
        {"--startup-level", "level/pbr_showcase.json"},
    };

    for (const auto &arguments : invalid_arguments)
    {
        const auto result = ParseStrings(arguments);
        EXPECT_FALSE(result) << "unexpectedly accepted invalid launch options";
        EXPECT_FALSE(result.diagnostic.empty());
    }
}

TEST(RuntimeLaunchOptionsTest, RejectsUnsafeOrWrongNamespaceLevelPaths)
{
    const std::vector<std::vector<std::string>> invalid_arguments{
        {"--startup-level", ""},
        {"--startup-level", "../level/pbr_showcase.level"},
        {"--startup-level", "C:/level/pbr_showcase.level"},
        {"--startup-level", "/level/pbr_showcase.level"},
        {"--startup-level", "material/pbr.material"},
        {"--startup-level", "level/../material/pbr.material"},
        {"--startup-level", std::string{"level/pbr\0showcase.level", 19}},
    };

    for (const auto &arguments : invalid_arguments)
    {
        const auto result = ParseStrings(arguments);
        EXPECT_FALSE(result) << "unexpectedly accepted invalid startup level";
        EXPECT_NE(result.diagnostic.find("--startup-level"), std::string::npos);
    }
}

TEST(RuntimeLaunchOptionsTest, RejectsDuplicateOptions)
{
    EXPECT_FALSE(Parse({"--agent-port", "37373", "--agent-port", "37374"}));
    EXPECT_FALSE(Parse({"--graphics-api", "vulkan", "--graphics-api", "opengl"}));
    EXPECT_FALSE(Parse({"--mode", "scene3d", "--mode", "live2d-viewer"}));
    EXPECT_FALSE(Parse({"--startup-level", "level/pbr_showcase.level",
                        "--startup-level", "level/point_shadow_validation.level"}));
}

namespace
{
    class TestApplicationHost final : public kpengine::runtime::IApplicationHost
    {
    public:
        const char *Name() const noexcept override { return "test"; }
        bool Initialize(kpengine::runtime::Engine &, std::string &) override { return true; }
        bool Tick(float, std::string &) override { return true; }
        bool RecordFrame(std::string &) override { return true; }
        void Shutdown() noexcept override {}
    };
}

TEST(ApplicationHostRegistryTest, RegistersAndCreatesOneProviderPerMode)
{
    kpengine::runtime::ApplicationHostRegistry registry;
    std::string diagnostic;
    const bool registered = registry.Register(
        kpengine::runtime::ApplicationMode::Live2DViewer,
        [](kpengine::runtime::Engine &) {
            return std::make_unique<TestApplicationHost>();
        },
        diagnostic);

    ASSERT_TRUE(registered) << diagnostic;
    EXPECT_TRUE(registry.Contains(kpengine::runtime::ApplicationMode::Live2DViewer));
    EXPECT_FALSE(registry.Contains(kpengine::runtime::ApplicationMode::Scene3D));
    kpengine::runtime::Engine engine;
    const std::unique_ptr<kpengine::runtime::IApplicationHost> host = registry.Create(
        kpengine::runtime::ApplicationMode::Live2DViewer, engine, diagnostic);
    ASSERT_NE(host, nullptr) << diagnostic;
    EXPECT_STREQ(host->Name(), "test");
    EXPECT_FALSE(registry.Register(
        kpengine::runtime::ApplicationMode::Live2DViewer,
        [](kpengine::runtime::Engine &) {
            return std::make_unique<TestApplicationHost>();
        },
        diagnostic));
    EXPECT_NE(diagnostic.find("already registered"), std::string::npos);
}

TEST(Scene3DHostTest, OwnsOnlySceneModeLifecycle)
{
    kpengine::runtime::Engine engine;
    kpengine::runtime::Scene3DHost host;
    std::string diagnostic;

    EXPECT_TRUE(host.Initialize(engine, diagnostic)) << diagnostic;
    EXPECT_TRUE(host.Tick(1.0f / 60.0f, diagnostic)) << diagnostic;
    EXPECT_TRUE(host.RecordFrame(diagnostic)) << diagnostic;
    host.Shutdown();
    EXPECT_FALSE(host.RecordFrame(diagnostic));
    EXPECT_EQ(diagnostic, "3DSceneHost is not initialized");
}

TEST(Scene3DHostTest, RejectsViewerMode)
{
    kpengine::runtime::Engine engine;
    engine.SetApplicationMode(kpengine::runtime::ApplicationMode::Live2DViewer);
    kpengine::runtime::Scene3DHost host;
    std::string diagnostic;

    EXPECT_FALSE(host.Initialize(engine, diagnostic));
    EXPECT_EQ(diagnostic, "3DSceneHost can only initialize in scene3d mode");
}
