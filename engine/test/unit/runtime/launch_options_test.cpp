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
    namespace command = kpengine::runtime::command;

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
    EXPECT_FALSE(result.options.startup_resize.has_value());
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
    // Opting into the agent port is what authorizes the mutating commands it
    // exposes; without it the transport serves read-only commands only.
    EXPECT_TRUE(command::HasCommandCapability(
        result.options.command_transport_config.capabilities,
        command::CommandCapability::Mutating));
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

    // A standalone host owns its window and backend directly, so it can serve
    // Runtime commands -- including window.resize -- without Scene3D services.
    const auto agent_port = Parse({"--agent-port", "37373", "--mode", "live2d-viewer"});
    ASSERT_TRUE(agent_port) << agent_port.diagnostic;
    ASSERT_TRUE(agent_port.options.command_transport_config.enabled);
    EXPECT_EQ(agent_port.options.command_transport_config.port, 37373);
    EXPECT_TRUE(command::HasCommandCapability(
        agent_port.options.command_transport_config.capabilities,
        command::CommandCapability::Mutating));

    const auto scene_capture = Parse({"--mode", "scene3d", "--capture",
                                      "save/screenshots/validation/scene.png"});
    EXPECT_FALSE(scene_capture);
    EXPECT_NE(scene_capture.diagnostic.find("--capture"), std::string::npos);
}

TEST(RuntimeLaunchOptionsTest, ParsesCaptureViewAndDefaultsToPresentation)
{
    const auto defaulted = Parse(
        {"--capture", "save/screenshots/validation/hiyori.png",
         "--mode", "live2d-viewer"});
    ASSERT_TRUE(defaulted) << defaulted.diagnostic;
    EXPECT_EQ(defaulted.options.startup_capture_view,
              kpengine::runtime::StartupCaptureView::Presentation);

    const auto product = Parse({"--capture", "save/screenshots/validation/hiyori.png",
                                "--capture-view", "live2d", "--mode", "live2d-viewer"});
    ASSERT_TRUE(product) << product.diagnostic;
    EXPECT_EQ(product.options.startup_capture_view,
              kpengine::runtime::StartupCaptureView::Product);

    const auto presentation = Parse(
        {"--capture", "save/screenshots/validation/hiyori.png",
         "--capture-view", "window", "--mode", "live2d-viewer"});
    ASSERT_TRUE(presentation) << presentation.diagnostic;
    EXPECT_EQ(presentation.options.startup_capture_view,
              kpengine::runtime::StartupCaptureView::Presentation);

    const auto invalid_value =
        Parse({"--capture", "save/screenshots/validation/hiyori.png",
               "--capture-view", "offscreen", "--mode", "live2d-viewer"});
    EXPECT_FALSE(invalid_value);
    EXPECT_NE(invalid_value.diagnostic.find("--capture-view"), std::string::npos);

    const auto without_capture =
        Parse({"--capture-view", "live2d", "--mode", "live2d-viewer"});
    EXPECT_FALSE(without_capture);
    EXPECT_NE(without_capture.diagnostic.find("--capture-view"), std::string::npos);

    const auto scene_view =
        Parse({"--mode", "scene3d", "--capture-view", "live2d"});
    EXPECT_FALSE(scene_view);
    EXPECT_NE(scene_view.diagnostic.find("--capture-view"), std::string::npos);
}

TEST(RuntimeLaunchOptionsTest, ParsesCaptureAlphaAndDefaultsToOpaqueClear)
{
    const auto defaulted = Parse(
        {"--capture", "save/screenshots/validation/hiyori.png",
         "--mode", "live2d-viewer"});
    ASSERT_TRUE(defaulted) << defaulted.diagnostic;
    EXPECT_FALSE(defaulted.options.startup_capture_transparent_clear);

    const auto transparent =
        Parse({"--capture", "save/screenshots/validation/hiyori.png",
               "--capture-alpha", "transparent", "--mode", "live2d-viewer"});
    ASSERT_TRUE(transparent) << transparent.diagnostic;
    EXPECT_TRUE(transparent.options.startup_capture_transparent_clear);

    const auto opaque = Parse({"--capture", "save/screenshots/validation/hiyori.png",
                               "--capture-alpha", "opaque", "--mode",
                               "live2d-viewer"});
    ASSERT_TRUE(opaque) << opaque.diagnostic;
    EXPECT_FALSE(opaque.options.startup_capture_transparent_clear);

    const auto invalid_value =
        Parse({"--capture", "save/screenshots/validation/hiyori.png",
               "--capture-alpha", "premultiplied", "--mode", "live2d-viewer"});
    EXPECT_FALSE(invalid_value);
    EXPECT_NE(invalid_value.diagnostic.find("--capture-alpha"), std::string::npos);

    const auto without_capture =
        Parse({"--capture-alpha", "transparent", "--mode", "live2d-viewer"});
    EXPECT_FALSE(without_capture);
    EXPECT_NE(without_capture.diagnostic.find("--capture-alpha"), std::string::npos);

    const auto scene_alpha = Parse({"--mode", "scene3d", "--capture-alpha",
                                    "transparent"});
    EXPECT_FALSE(scene_alpha);
    EXPECT_NE(scene_alpha.diagnostic.find("--capture-alpha"), std::string::npos);
}

TEST(RuntimeLaunchOptionsTest, ParsesExitAfterCaptureAndRequiresCapture)
{
    const auto defaulted = Parse(
        {"--capture", "save/screenshots/validation/hiyori.png",
         "--mode", "live2d-viewer"});
    ASSERT_TRUE(defaulted) << defaulted.diagnostic;
    EXPECT_FALSE(defaulted.options.startup_exit_after_capture);

    const auto enabled =
        Parse({"--capture", "save/screenshots/validation/hiyori.png",
               "--exit-after-capture", "--mode", "live2d-viewer"});
    ASSERT_TRUE(enabled) << enabled.diagnostic;
    EXPECT_TRUE(enabled.options.startup_exit_after_capture);

    const auto without_capture =
        Parse({"--exit-after-capture", "--mode", "live2d-viewer"});
    EXPECT_FALSE(without_capture);
    EXPECT_NE(without_capture.diagnostic.find("--exit-after-capture"),
              std::string::npos);

    const auto scene_mode = Parse({"--mode", "scene3d", "--exit-after-capture"});
    EXPECT_FALSE(scene_mode);
    EXPECT_NE(scene_mode.diagnostic.find("--exit-after-capture"),
              std::string::npos);

    const auto duplicated =
        Parse({"--capture", "save/screenshots/validation/hiyori.png",
               "--exit-after-capture", "--exit-after-capture",
               "--mode", "live2d-viewer"});
    EXPECT_FALSE(duplicated);
    EXPECT_NE(duplicated.diagnostic.find("--exit-after-capture"),
              std::string::npos);
}

TEST(RuntimeLaunchOptionsTest, ParsesResizeExtentAndRestrictsItToViewerMode)
{
    const auto defaulted = Parse({"--mode", "live2d-viewer"});
    ASSERT_TRUE(defaulted) << defaulted.diagnostic;
    EXPECT_FALSE(defaulted.options.startup_resize.has_value());

    // A resize is meaningful on its own: it is what lets a run drive the
    // output-resize path, whether or not a capture accompanies it.
    const auto resized = Parse({"--resize", "1024x768", "--mode", "live2d-viewer"});
    ASSERT_TRUE(resized) << resized.diagnostic;
    ASSERT_TRUE(resized.options.startup_resize.has_value());
    EXPECT_EQ(resized.options.startup_resize->width, 1024u);
    EXPECT_EQ(resized.options.startup_resize->height, 768u);

    const auto scene_mode = Parse({"--mode", "scene3d", "--resize", "1024x768"});
    EXPECT_FALSE(scene_mode);
    EXPECT_NE(scene_mode.diagnostic.find("--resize"), std::string::npos);

    const auto duplicated = Parse({"--resize", "1024x768", "--resize", "800x600",
                                   "--mode", "live2d-viewer"});
    EXPECT_FALSE(duplicated);
    EXPECT_NE(duplicated.diagnostic.find("--resize"), std::string::npos);
}

TEST(RuntimeLaunchOptionsTest, RejectsMalformedResizeExtents)
{
    const std::vector<std::string> invalid{
        "",
        "1024",
        "x768",
        "1024x",
        "1024X768",
        "1024x768x900",
        "0x768",
        "1024x0",
        "-1024x768",
        "1024x-768",
        "1024x76a",
        " 1024x768",
        "1024x768 ",
        "1.5x768",
        "99999x768",
        "1024x99999",
    };

    for (const std::string &value : invalid)
    {
        const auto result =
            ParseStrings({"--resize", value, "--mode", "live2d-viewer"});
        EXPECT_FALSE(result) << "unexpectedly accepted --resize " << value;
        EXPECT_NE(result.diagnostic.find("--resize"), std::string::npos) << value;
    }

    // A following option is not a value, so the missing-value diagnostic is
    // reported instead of the option being read as the extent.
    const auto missing = Parse({"--resize", "--mode", "live2d-viewer"});
    EXPECT_FALSE(missing);
    EXPECT_NE(missing.diagnostic.find("--resize"), std::string::npos);
}

// --live2d-model exists so a licensed product that lives only in a local,
// git-ignored tree can be put in front of the viewer without committing that
// choice into config/live2d.json, which every checkout shares.
TEST(RuntimeLaunchOptionsTest, ParsesLive2DModelOverrideAndRestrictsItToViewerMode)
{
    const auto defaulted = Parse({"--mode", "live2d-viewer"});
    ASSERT_TRUE(defaulted) << defaulted.diagnostic;
    EXPECT_FALSE(defaulted.options.live2d_model_override.has_value());

    const auto overridden = Parse({"--mode", "live2d-viewer", "--live2d-model",
                                   "live2d/mao/mao.live2d"});
    ASSERT_TRUE(overridden) << overridden.diagnostic;
    ASSERT_TRUE(overridden.options.live2d_model_override.has_value());
    EXPECT_EQ(*overridden.options.live2d_model_override, "live2d/mao/mao.live2d");

    // Authored separators and redundant segments normalize to the same
    // Asset-root-relative spelling the loader concatenates.
    const auto windows_separators = Parse(
        {"--live2d-model", R"(live2d\mao\.\mao.live2d)", "--mode", "live2d-viewer"});
    ASSERT_TRUE(windows_separators) << windows_separators.diagnostic;
    ASSERT_TRUE(windows_separators.options.live2d_model_override.has_value());
    EXPECT_EQ(*windows_separators.options.live2d_model_override,
              "live2d/mao/mao.live2d");

    const auto viewer_only =
        Parse({"--mode", "scene3d", "--live2d-model", "live2d/mao/mao.live2d"});
    EXPECT_FALSE(viewer_only);
    EXPECT_NE(viewer_only.diagnostic.find("--live2d-model"), std::string::npos);

    const auto duplicated =
        Parse({"--live2d-model", "live2d/mao/mao.live2d", "--live2d-model",
               "live2d/hiyori_pro/hiyori.live2d", "--mode", "live2d-viewer"});
    EXPECT_FALSE(duplicated);
    EXPECT_NE(duplicated.diagnostic.find("--live2d-model"), std::string::npos);
}

TEST(RuntimeLaunchOptionsTest, RejectsUnsafeOrNonProductLive2DModelPaths)
{
    const std::vector<std::string> invalid{
        "",
        "live2d/mao/mao",
        "live2d/mao/mao.moc3",
        "live2d/mao/mao.LIVE2D.png",
        "/live2d/mao/mao.live2d",
        R"(C:\live2d\mao\mao.live2d)",
        "C:/live2d/mao/mao.live2d",
        "../outside/mao.live2d",
        "live2d/../../outside/mao.live2d",
        "live2d/mao.live2d:stream",
        ".live2d",
    };

    for (const std::string &value : invalid)
    {
        const auto result =
            ParseStrings({"--live2d-model", value, "--mode", "live2d-viewer"});
        EXPECT_FALSE(result) << "unexpectedly accepted --live2d-model " << value;
        EXPECT_NE(result.diagnostic.find("--live2d-model"), std::string::npos) << value;
    }

    const auto missing = Parse({"--live2d-model", "--mode", "live2d-viewer"});
    EXPECT_FALSE(missing);
    EXPECT_NE(missing.diagnostic.find("--live2d-model"), std::string::npos);
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
