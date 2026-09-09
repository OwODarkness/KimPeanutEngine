#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <string>

#include "asset/model_archive.h"
#include "asset/material_promotion.h"
#include "asset/model_import_service.h"

namespace
{
    using kpengine::asset::ArchiveProductType;
    using kpengine::asset::ContentHash;
    using kpengine::asset::ModelArchiveDatabase;
    using kpengine::asset::ModelImportError;
    using kpengine::asset::ModelImportErrorCode;
    using kpengine::asset::ModelImportRequest;
    using kpengine::asset::ModelImportService;
    using kpengine::asset::ModelImportStatus;
    using kpengine::asset::MaterialPromotionError;
    using kpengine::asset::MaterialPromotionErrorCode;
    using kpengine::asset::MaterialPromotionRequest;
    using kpengine::asset::PromoteGeneratedMaterial;

    class ImportFixture final
    {
    public:
        ImportFixture()
        {
            static std::atomic_uint64_t sequence{};
            root_ = std::filesystem::temp_directory_path() /
                    ("kpengine_model_import_test_" +
                     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            std::filesystem::create_directories(root_ / "models");
            Write("models/triangle.obj",
                  "mtllib triangle.mtl\n"
                  "o Triangle\n"
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "vt 0 0\n"
                  "vt 1 0\n"
                  "vt 0 1\n"
                  "vn 0 0 1\n"
                  "usemtl TriangleMaterial\n"
                  "f 1/1/1 2/2/1 3/3/1\n");
            Write("models/triangle.mtl",
                  "newmtl TriangleMaterial\n"
                  "Kd 0.8 0.7 0.6\n"
                  "Ns 16.0\n");
        }

        ~ImportFixture() noexcept
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        void Write(const std::string &relative, const std::string &contents) const
        {
            const std::filesystem::path path = root_ / relative;
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(file.is_open());
            file << contents;
            ASSERT_TRUE(file.good());
        }

        ModelImportRequest Request(const std::string &source = "models/triangle.obj") const
        {
            return {root_, root_ / ".archive", source, {}};
        }

        const std::filesystem::path &Root() const noexcept
        {
            return root_;
        }

    private:
        std::filesystem::path root_;
    };

    ModelImportErrorCode CatchImportError(const std::function<void()> &function)
    {
        try
        {
            function();
        }
        catch (const ModelImportError &error)
        {
            return error.Code();
        }
        ADD_FAILURE() << "expected ModelImportError";
        return ModelImportErrorCode::PublicationFailed;
    }

    MaterialPromotionErrorCode CatchPromotionError(const std::function<void()> &function)
    {
        try
        {
            function();
        }
        catch (const MaterialPromotionError &error)
        {
            return error.Code();
        }
        ADD_FAILURE() << "expected MaterialPromotionError";
        return MaterialPromotionErrorCode::ArchiveCommitFailed;
    }

    std::string ReadText(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
}

TEST(ModelImportServiceTest, PublishesProductsAndRepeatsAsVerifiedCacheHit)
{
    ImportFixture fixture;
    ModelImportService service;
    const auto first = service.Import(fixture.Request());
    ASSERT_EQ(first.status, ModelImportStatus::Imported);
    ASSERT_FALSE(first.model_hash.ToHex().empty());
    ASSERT_TRUE(std::filesystem::is_regular_file(first.model_path));
    ASSERT_GE(first.material_hashes.size(), 1u);
    EXPECT_GT(first.metrics.total_seconds, 0.0);
    EXPECT_GT(first.metrics.stage_seconds[static_cast<std::size_t>(
                  kpengine::asset::ModelImportMetricStage::SourceDecode)], 0.0);
    const auto expected_product_count = first.material_hashes.size() +
                                        first.texture_hashes.size() + 1u;
    EXPECT_EQ(first.metrics.product_count, expected_product_count);
    EXPECT_EQ(first.metrics.product_write_count, expected_product_count);
    EXPECT_EQ(first.metrics.peak_active_jobs, 1u);

    const auto second = service.Import(fixture.Request());
    EXPECT_EQ(second.status, ModelImportStatus::UpToDate);
    EXPECT_EQ(second.model_hash, first.model_hash);
    EXPECT_EQ(second.material_hashes, first.material_hashes);
    EXPECT_TRUE(second.metrics.cache_hit);
    EXPECT_EQ(second.metrics.cache_hit_count, 1u);
    EXPECT_EQ(second.metrics.product_count, expected_product_count);
    EXPECT_GT(second.metrics.product_bytes_read, 0u);

    ModelArchiveDatabase archive{fixture.Root() / ".archive" / "archive.sqlite3"};
    const auto snapshot = archive.FindSource("models/triangle.obj");
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->source_products.size(), first.material_hashes.size() + 1u);
    EXPECT_EQ(snapshot->source_products[0].asset_type, ArchiveProductType::Model);
    EXPECT_EQ(snapshot->source_products[1].asset_type, ArchiveProductType::Material);
}

TEST(ModelImportServiceTest, ReimportsWhenRecordedDependencyChangesAndPreservesRootOnFailure)
{
    ImportFixture fixture;
    ModelImportService service;
    const auto first = service.Import(fixture.Request());

    fixture.Write("models/triangle.mtl", "newmtl TriangleMaterial\nKd 0.2 0.3 0.4\n");
    const auto changed = service.Import(fixture.Request());
    EXPECT_EQ(changed.status, ModelImportStatus::Imported);
    EXPECT_NE(changed.source_package_hash, first.source_package_hash);

    std::filesystem::remove(fixture.Root() / "models/triangle.obj");
    EXPECT_EQ(CatchImportError([&] { (void)service.Import(fixture.Request()); }),
              ModelImportErrorCode::IoError);

    ModelArchiveDatabase archive{fixture.Root() / ".archive" / "archive.sqlite3"};
    const auto snapshot = archive.FindSource("models/triangle.obj");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->source.package_hash, changed.source_package_hash);
    EXPECT_TRUE(std::filesystem::is_regular_file(changed.model_path));
}

TEST(ModelImportServiceTest, DeduplicatesEquivalentProductsAcrossSourcesAndCoordinatesConcurrentCalls)
{
    ImportFixture fixture;
    fixture.Write("models/copy.obj",
                  "mtllib triangle.mtl\n"
                  "o Triangle\n"
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "vt 0 0\nvt 1 0\nvt 0 1\n"
                  "vn 0 0 1\nusemtl TriangleMaterial\n"
                  "f 1/1/1 2/2/1 3/3/1\n");

    ModelImportService service;
    const auto first = service.Import(fixture.Request());
    const auto second = service.Import(fixture.Request("models/copy.obj"));
    EXPECT_EQ(first.model_hash, second.model_hash);
    EXPECT_EQ(first.material_hashes, second.material_hashes);

    fixture.Write("models/a.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    auto concurrent_a = std::async(std::launch::async,
                                   [&] { return service.Import(fixture.Request("models/a.obj")); });
    auto concurrent_b = std::async(std::launch::async,
                                   [&] { return service.Import(fixture.Request("models/a.obj")); });
    const auto result_a = concurrent_a.get();
    const auto result_b = concurrent_b.get();
    EXPECT_TRUE(result_a.status == ModelImportStatus::Imported ||
                result_a.status == ModelImportStatus::UpToDate);
    EXPECT_TRUE(result_b.status == ModelImportStatus::Imported ||
                result_b.status == ModelImportStatus::UpToDate);
    EXPECT_EQ(result_a.model_hash, result_b.model_hash);
}

TEST(ModelImportServiceTest, RejectsSourcePathEscape)
{
    ImportFixture fixture;
    const std::filesystem::path outside = fixture.Root().parent_path() / "outside.obj";
    {
        std::ofstream file(outside, std::ios::binary | std::ios::trunc);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    ModelImportService service;
    ModelImportRequest request = fixture.Request();
    request.source_path = outside;
    EXPECT_EQ(CatchImportError([&] { (void)service.Import(request); }),
              ModelImportErrorCode::InvalidArgument);
    std::error_code error;
    std::filesystem::remove(outside, error);
}

TEST(ModelImportServiceTest, RebuildsMissingProductAndRejectsImmutableCollision)
{
    ImportFixture fixture;
    ModelImportService service;
    const auto first = service.Import(fixture.Request());
    const std::filesystem::path model_path = first.model_path;

    std::filesystem::remove(model_path);
    const auto rebuilt = service.Import(fixture.Request());
    EXPECT_EQ(rebuilt.status, ModelImportStatus::Imported);
    ASSERT_TRUE(std::filesystem::is_regular_file(rebuilt.model_path));

    std::fstream file(rebuilt.model_path, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    char first_byte = 0;
    file.read(&first_byte, 1);
    ASSERT_TRUE(file.good());
    first_byte ^= 1;
    file.seekp(0, std::ios::beg);
    file.write(&first_byte, 1);
    file.close();

    EXPECT_EQ(CatchImportError([&] { (void)service.Import(fixture.Request()); }),
              ModelImportErrorCode::ProductCollision);

    const std::filesystem::path staging = fixture.Root() / ".archive" / "staging";
    if (std::filesystem::exists(staging))
    {
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(staging),
                                std::filesystem::directory_iterator{}), 0);
    }
}

TEST(ModelImportServiceTest, PromotesGeneratedMaterialWithoutMutatingArchiveProduct)
{
    ImportFixture fixture;
    ModelImportService service;
    const auto imported = service.Import(fixture.Request());

    kpengine::asset::ProductRecord material_product{};
    {
        ModelArchiveDatabase archive{fixture.Root() / ".archive" / "archive.sqlite3"};
        const auto before = archive.FindSource("models/triangle.obj");
        ASSERT_TRUE(before.has_value());
        const auto material_source_product = std::find_if(
            before->source_products.begin(), before->source_products.end(),
            [](const auto &product)
            {
                return product.asset_type == ArchiveProductType::Material && product.slot == 0;
            });
        ASSERT_NE(material_source_product, before->source_products.end());
        const auto material_product_iterator = std::find_if(
            before->products.begin(), before->products.end(),
            [&material_source_product](const auto &product)
            {
                return product.asset_type == ArchiveProductType::Material &&
                       product.content_hash == material_source_product->content_hash;
            });
        ASSERT_NE(material_product_iterator, before->products.end());
        material_product = *material_product_iterator;
    }
    const std::filesystem::path product_path =
        fixture.Root() / ".archive" / material_product.relative_path;
    const ContentHash product_hash_before = kpengine::asset::Sha256File(product_path);

    const MaterialPromotionRequest request{
        fixture.Root(), fixture.Root() / ".archive", "models/triangle", 0,
        "material/promoted_triangle.material"};
    const auto promoted = PromoteGeneratedMaterial(request);
    EXPECT_TRUE(promoted.authored_file_created);
    EXPECT_EQ(promoted.generated_material_hash, material_product.content_hash);
    ASSERT_TRUE(std::filesystem::is_regular_file(promoted.authored_material_path));
    EXPECT_NE(ReadText(promoted.authored_material_path).find("\"shader\":\"../shader/"),
              std::string::npos);
    EXPECT_EQ(kpengine::asset::Sha256File(product_path), product_hash_before);

    ModelArchiveDatabase archive{fixture.Root() / ".archive" / "archive.sqlite3"};
    const auto after = archive.FindSource("models/triangle.obj");
    ASSERT_TRUE(after.has_value());
    ASSERT_EQ(after->material_overrides.size(), 1u);
    EXPECT_EQ(after->material_overrides.front().slot, 0);
    EXPECT_EQ(after->material_overrides.front().authored_path,
              "material/promoted_triangle.material");

    const auto repeat = PromoteGeneratedMaterial(request);
    EXPECT_FALSE(repeat.authored_file_created);
    EXPECT_EQ(CatchPromotionError(
                  [&]
                  {
                      MaterialPromotionRequest collision = request;
                      collision.authored_material_path = "material/collision.material";
                      fixture.Write("material/collision.material", "different");
                      (void)PromoteGeneratedMaterial(collision);
                  }),
              MaterialPromotionErrorCode::Collision);
}
