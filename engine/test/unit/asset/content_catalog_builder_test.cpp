#include "asset/content_metadata.h"
#include "asset/detail/content_catalog_builder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace kpengine::asset
{
namespace
{
    class ContentCatalogFixture
    {
    public:
        ContentCatalogFixture()
            : root_(std::filesystem::temp_directory_path() /
                    "kpengine_content_catalog_builder_test")
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
            std::filesystem::create_directories(root_ / ".archive", error);
        }

        ~ContentCatalogFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        const std::filesystem::path &root() const noexcept { return root_; }

        ContentHash AddProduct(ArchiveProductType type, std::string_view bytes)
        {
            const ContentHash hash = Sha256(bytes);
            const std::filesystem::path path = root_ / ".archive" /
                                               ProductRelativePath(
                                                   type, hash,
                                                   type == ArchiveProductType::Texture ? "texture"
                                                                                       : "");
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            EXPECT_FALSE(error) << error.message();
            std::ofstream output(path, std::ios::binary);
            output << bytes;
            EXPECT_TRUE(output.good());
            return hash;
        }

    private:
        std::filesystem::path root_;
    };

    ContentMetadata Metadata(std::string id, AssetType type, std::string type_name,
                             std::string name, std::string path,
                             ArchiveProductType product_type,
                             const ContentHash &product_hash)
    {
        ContentMetadata metadata;
        metadata.id = ContentID(std::move(id));
        metadata.asset_type = type;
        metadata.type_name = std::move(type_name);
        metadata.name = std::move(name);
        metadata.content_path = std::move(path);
        metadata.products.push_back({product_type, product_hash});
        return metadata;
    }
}

TEST(ContentCatalogBuilderTest, PublishesVisibleMetadataAndContentIdEdgesOnly)
{
    ContentCatalogFixture fixture;
    const ContentHash model_hash = fixture.AddProduct(ArchiveProductType::Model, "model");
    const ContentHash material_hash =
        fixture.AddProduct(ArchiveProductType::Material, "material");

    ContentMetadata model = Metadata("model-id", AssetType::KPAT_Model, "model", "Model_Sponza",
                                    "model/sponza", ArchiveProductType::Model, model_hash);
    ContentMetadata material = Metadata("material-id", AssetType::KPAT_Material,
                                       "material", "Mat_Bricks", "material/bricks",
                                       ArchiveProductType::Material, material_hash);
    model.references.push_back({"material", material.id});
    model.references.push_back({"missing", ContentID("missing-texture-id")});

    ASSERT_TRUE(WriteContentMetadata(fixture.root(), model));
    ASSERT_TRUE(WriteContentMetadata(fixture.root(), material));

    ContentMetadata shader = Metadata("shader-id", AssetType::KPAT_Shader, "shader",
                                     "Shader_Internal", "shader/pbr",
                                     ArchiveProductType::Model, model_hash);
    shader.visibility = ContentVisibility::Internal;
    ASSERT_TRUE(WriteContentMetadata(fixture.root(), shader));

    const ContentRegistrySnapshot registry = ContentRegistry(fixture.root()).Capture();
    ASSERT_TRUE(registry.has_metadata_files);
    ASSERT_EQ(registry.records.size(), 3u);

    detail::ContentCatalogBuildInput input;
    input.revision = 7;
    input.archive_root = fixture.root() / ".archive";
    input.registry = &registry;

    const AssetCatalogSnapshot snapshot = detail::BuildContentAssetCatalog(input);
    ASSERT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial)
        << "the unresolved ContentID should be explicit";
    ASSERT_EQ(snapshot.nodes.size(), 3u);
    ASSERT_EQ(snapshot.edges.size(), 2u);

    const auto model_node = std::find_if(
        snapshot.nodes.begin(), snapshot.nodes.end(),
        [](const AssetCatalogNode &node) { return node.content_id == "model-id"; });
    ASSERT_NE(model_node, snapshot.nodes.end());
    EXPECT_EQ(model_node->logical_path, "model/sponza");
    EXPECT_EQ(model_node->availability, AssetCatalogAvailability::ArchiveOnly);
    EXPECT_EQ(model_node->content_hash, model_hash);

    EXPECT_EQ(std::count_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                            [](const AssetCatalogNode &node)
                            { return node.type == AssetType::KPAT_Shader; }),
              0);
    EXPECT_EQ(std::count_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                            [](const AssetCatalogNode &node)
                            { return node.kind == AssetCatalogNodeKind::MissingReference; }),
              1);
    std::string diagnostic;
    EXPECT_TRUE(ValidateAssetCatalogSnapshot(snapshot, diagnostic)) << diagnostic;
}

TEST(ContentCatalogBuilderTest, MissingProductsAreNotPresentedAsHealthyRows)
{
    ContentCatalogFixture fixture;
    ContentMetadata metadata = Metadata("missing-id", AssetType::KPAT_Texture, "texture",
                                       "Tex_Missing", "texture/missing",
                                       ArchiveProductType::Texture,
                                       Sha256("not-published"));
    ASSERT_TRUE(WriteContentMetadata(fixture.root(), metadata));

    const ContentRegistrySnapshot registry = ContentRegistry(fixture.root()).Capture();
    detail::ContentCatalogBuildInput input;
    input.revision = 1;
    input.archive_root = fixture.root() / ".archive";
    input.registry = &registry;

    const AssetCatalogSnapshot snapshot = detail::BuildContentAssetCatalog(input);
    EXPECT_TRUE(snapshot.nodes.empty());
    EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
    EXPECT_FALSE(snapshot.diagnostics.empty());
}

} // namespace kpengine::asset
