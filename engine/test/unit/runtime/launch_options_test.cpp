#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

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
    EXPECT_EQ(result.options.graphics_api_type,
              kpengine::GraphicsAPIType::GRAPHICS_API_UNKNOW);
    EXPECT_FALSE(result.options.command_transport_config.enabled);
    EXPECT_FALSE(result.options.startup_level_override.has_value());
}

TEST(RuntimeLaunchOptionsTest, ParsesOptionsInAnyOrderAndNormalizesLevel)
{
    const auto result = Parse({"--startup-level", "level\\.\\point_shadow_validation.level",
                               "--agent-port", "37373", "--graphics-api", "vulkan"});

    ASSERT_TRUE(result) << result.diagnostic;
    EXPECT_EQ(result.options.graphics_api_type,
              kpengine::GraphicsAPIType::GRAPHICS_API_VULKAN);
    ASSERT_TRUE(result.options.command_transport_config.enabled);
    EXPECT_EQ(result.options.command_transport_config.port, 37373);
    ASSERT_TRUE(result.options.startup_level_override.has_value());
    EXPECT_EQ(*result.options.startup_level_override,
              "level/point_shadow_validation.level");
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
    EXPECT_FALSE(Parse({"--startup-level", "level/pbr_showcase.level",
                        "--startup-level", "level/point_shadow_validation.level"}));
}
