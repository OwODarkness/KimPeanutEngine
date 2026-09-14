#include "asset/content_metadata.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace kpengine::asset
{
namespace
{

TEST(ContentMetadataTest, StableIdUsesLogicalIdentity)
{
    const ContentID first = MakeContentID("material", "model/house:0");
    const ContentID second = MakeContentID("material", "model/house:0");
    const ContentID different = MakeContentID("material", "model/house:1");

    EXPECT_TRUE(first.IsValid());
    EXPECT_EQ(first, second);
    EXPECT_NE(first, different);
    EXPECT_EQ(first.ToString().substr(0, 4), "cid-");
}

TEST(ContentMetadataTest, WritesAndCapturesOnlyMetadataOutsideArchive)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kpengine_content_metadata_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    ContentMetadata metadata;
    metadata.id = MakeContentID("model", "model/house");
    metadata.type_name = "model";
    metadata.name = "House";
    metadata.content_path = "model/house";
    metadata.source_path = "model/house.fbx";

    std::string diagnostic;
    ASSERT_TRUE(WriteContentMetadata(root, metadata, &diagnostic)) << diagnostic;

    std::filesystem::create_directories(root / ".archive" / "models");
    std::ofstream archive_product(root / ".archive" / "models" / "hidden.model");
    archive_product << "not a metadata record";

    const ContentRegistrySnapshot snapshot = ContentRegistry(root).Capture();
    ASSERT_EQ(snapshot.diagnostics.size(), 0u);
    ASSERT_EQ(snapshot.records.size(), 1u);
    EXPECT_EQ(snapshot.records.front().id, metadata.id);
    EXPECT_EQ(snapshot.records.front().name, "House");

    std::filesystem::remove_all(root, error);
}

} // namespace
} // namespace kpengine::asset
