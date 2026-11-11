#include "editor/ui/editor_icons.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace kpengine::editor
{
    namespace
    {
        class EditorIconsTest : public testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                root = std::filesystem::temp_directory_path() / ("kp-editor-icons-" + std::to_string(stamp));
                std::filesystem::create_directories(root / "custom");
                configuration = root / "icons.json";
                std::ofstream(configuration) <<
                    R"({"icons":{"document":"custom/document.png","model":"model.png","play":"custom/play.png"}})";
            }

            void TearDown() override
            {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }

            std::filesystem::path root;
            std::filesystem::path configuration;
        };
    }

    TEST_F(EditorIconsTest, LoadsAllCheckedInBuiltinResourcesWithoutToolkitOrGpu)
    {
        EditorBuiltinIcons icons;
        icons.Load();
        for (std::size_t index = 0; index < static_cast<std::size_t>(EditorIcon::Count); ++index)
        {
            const auto icon = static_cast<EditorIcon>(index);
            SCOPED_TRACE(EditorIconName(icon));
            EXPECT_TRUE(icons.Get(icon).IsValid()) << icons.Diagnostic(icon);
            EXPECT_TRUE(icons.Diagnostic(icon).empty());
        }
        EXPECT_FALSE(icons.Get(EditorIcon::Count).IsValid());
        EXPECT_FALSE(icons.Get(static_cast<EditorIcon>(255)).IsValid());
    }

    TEST_F(EditorIconsTest, CropsTransparentPaddingAndPresentsTopOriginMask)
    {
        image_io::ImageBuffer image{6, 4, image_io::ImagePixelFormat::Rgba8,
                                   std::vector<std::uint8_t>(6 * 4 * 4)};
        image.pixels[(0 * 6 + 1) * 4 + 3] = 128;
        image.pixels[(3 * 6 + 4) * 4 + 3] = 255;
        ASSERT_TRUE(image_io::WritePng(image, (root / "custom/document.png").generic_string()).success);

        EditorBuiltinIcons icons;
        icons.Load(configuration);
        const auto mask = icons.Get(EditorIcon::Document);
        ASSERT_TRUE(mask.IsValid());
        EXPECT_EQ(mask.alpha[1 * mask.width + 1], 128);
        EXPECT_EQ(mask.alpha[22 * mask.width + 22], 255);
        EXPECT_EQ(mask.alpha.front(), 0);
        EXPECT_EQ(mask.alpha.back(), 0);
        EXPECT_TRUE(icons.Diagnostic(EditorIcon::Document).empty());
    }

    TEST_F(EditorIconsTest, MissingCorruptAndTransparentImagesLeaveNoStaleMask)
    {
        EditorBuiltinIcons icons;
        icons.Load();
        ASSERT_TRUE(icons.Get(EditorIcon::Play).IsValid());
        std::ofstream(root / "custom/document.png") << "invalid png";
        image_io::ImageBuffer transparent{2, 2, image_io::ImagePixelFormat::Rgba8,
                                         std::vector<std::uint8_t>(16)};
        ASSERT_TRUE(image_io::WritePng(transparent, (root / "model.png").generic_string()).success);
        icons.Load(configuration);
        for (const auto icon : {EditorIcon::Document, EditorIcon::Model, EditorIcon::Play})
        {
            EXPECT_FALSE(icons.Get(icon).IsValid());
            EXPECT_FALSE(icons.Diagnostic(icon).empty());
        }
    }

    TEST_F(EditorIconsTest, LoadsRemappedResourcesRelativeToTheJsonDirectory)
    {
        image_io::ImageBuffer image{3, 5, image_io::ImagePixelFormat::Rgba8,
                                   std::vector<std::uint8_t>(3 * 5 * 4, 255)};
        ASSERT_TRUE(image_io::WritePng(image, (root / "custom/play.png").generic_string()).success);
        EditorIconCatalog catalog;
        ASSERT_TRUE(catalog.Load(configuration)) << catalog.Diagnostic();
        const auto decoded = catalog.LoadImage(EditorIcon::Play);
        ASSERT_TRUE(decoded.result.success) << decoded.result.diagnostic;
        EXPECT_EQ(decoded.image.width, 3);
        EXPECT_EQ(decoded.image.height, 5);
        EXPECT_FALSE(catalog.LoadImage(EditorIcon::Count).result.success);
        EXPECT_FALSE(catalog.LoadImage("pause").result.success);
    }

    TEST_F(EditorIconsTest, InvalidConfigurationClearsMappingsAndHasNoFilenameFallback)
    {
        EditorIconCatalog catalog;
        ASSERT_TRUE(catalog.Load(configuration));
        std::ofstream(configuration) << "{invalid json";
        EXPECT_FALSE(catalog.Load(configuration));
        EXPECT_TRUE(catalog.Entries().empty());
        EXPECT_FALSE(catalog.Diagnostic().empty());
        EXPECT_FALSE(catalog.LoadImage(EditorIcon::Play).result.success);

        std::ofstream(configuration) << R"({"icons":{"play":123}})";
        EXPECT_FALSE(catalog.Load(configuration));
        std::ofstream(configuration) << R"({"icons":{"Play":"one.png","play":"two.png"}})";
        EXPECT_FALSE(catalog.Load(configuration));
    }

    TEST_F(EditorIconsTest, RejectsParentTraversalAndAbsoluteResourcePaths)
    {
        EditorIconCatalog catalog;
        std::ofstream(configuration) << R"({"icons":{"play":"../outside.png"}})";
        EXPECT_FALSE(catalog.Load(configuration));
        std::ofstream(configuration) << R"({"icons":{"play":")" <<
            (root / "absolute.png").generic_string() << R"("}})";
        EXPECT_FALSE(catalog.Load(configuration));
    }
}
