#include <gtest/gtest.h>

#include <atomic>
#include <fstream>
#include <string>
#include <utility>

#include "database/database.h"
#include "asset/model_archive.h"

namespace
{
    using kpengine::asset::ArchiveProbeStatus;
    using kpengine::asset::ArchiveProductType;
    using kpengine::asset::ContentHash;
    using kpengine::asset::HashImportKey;
    using kpengine::asset::HashSourcePackage;
    using kpengine::asset::ImportKeyInput;
    using kpengine::asset::MaterialOverrideRecord;
    using kpengine::asset::ModelArchiveDatabase;
    using kpengine::asset::ModelArchiveError;
    using kpengine::asset::ModelArchiveErrorCode;
    using kpengine::asset::NormalizeAssetRelativePath;
    using kpengine::asset::ProductRecord;
    using kpengine::asset::ProductRelativePath;
    using kpengine::asset::Sha256;
    using kpengine::asset::Sha256WithZeroedRange;
    using kpengine::asset::SourceArchiveSnapshot;
    using kpengine::asset::SourceDependencyRecord;
    using kpengine::asset::SourceFingerprintInput;
    using kpengine::asset::SourceImportStatus;
    using kpengine::asset::SourceProbeRequest;
    using kpengine::asset::SourceProductRecord;
    using kpengine::asset::SourceRecord;
    using kpengine::database::Database;
    using kpengine::asset::VerifyArchiveProduct;

    class TemporaryArchive final
    {
    public:
        TemporaryArchive()
        {
            static std::atomic_uint64_t sequence{};
            root_ = std::filesystem::temp_directory_path() /
                    ("kpengine_model_archive_test_" +
                     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            std::filesystem::create_directories(root_ / ".archive");
        }

        ~TemporaryArchive() noexcept
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        const std::filesystem::path &Root() const noexcept
        {
            return root_;
        }

        std::filesystem::path DatabasePath() const
        {
            return root_ / ".archive" / "archive.sqlite3";
        }

    private:
        std::filesystem::path root_;
    };

    std::vector<std::byte> Bytes(std::string_view value)
    {
        std::vector<std::byte> bytes;
        bytes.reserve(value.size());
        for (const unsigned char character : value)
        {
            bytes.push_back(static_cast<std::byte>(character));
        }
        return bytes;
    }

    void WriteBytes(const std::filesystem::path &path, const std::vector<std::byte> &bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(file.good());
    }

    template <typename Function>
    ModelArchiveErrorCode CatchArchiveError(Function &&function)
    {
        try
        {
            function();
        }
        catch (const ModelArchiveError &error)
        {
            return error.Code();
        }
        ADD_FAILURE() << "expected ModelArchiveError";
        return ModelArchiveErrorCode::IoError;
    }

    struct PublishedSource
    {
        SourceRecord source;
        std::vector<SourceDependencyRecord> dependencies;
        ProductRecord product;
        SourceProductRecord source_product;
        MaterialOverrideRecord material_override;
        std::vector<std::byte> product_bytes;
    };

    PublishedSource MakePublishedSource()
    {
        PublishedSource result;
        result.product_bytes = Bytes("model-product-v1");
        result.product.content_hash = Sha256(result.product_bytes);
        result.product.asset_type = ArchiveProductType::Model;
        result.product.relative_path = ProductRelativePath(ArchiveProductType::Model,
                                                            result.product.content_hash);
        result.product.byte_size = result.product_bytes.size();
        result.product.schema_version = 1;

        result.source.normalized_path = "models/triangle.obj";
        result.source.path_hash = Sha256(result.source.normalized_path);
        result.source.display_name = "Triangle";
        result.source.importer_id = "assimp";
        result.source.importer_version = 1;
        result.source.settings_hash = Sha256("settings-v1");
        result.source.native_model_version = 1;
        result.source.status = SourceImportStatus::Ready;

        result.dependencies.push_back(
            {result.source.normalized_path, Sha256("source-v1")});
        result.source.package_hash = HashSourcePackage(
            {{result.dependencies.front().normalized_path, result.dependencies.front().content_hash}});

        result.source_product.content_hash = result.product.content_hash;
        result.source_product.asset_type = result.product.asset_type;
        result.source_product.role = 0;
        result.source_product.slot = -1;
        result.source_product.display_name = "Triangle model";

        result.material_override = {0, "materials/triangle.material"};
        return result;
    }

    SourceProbeRequest RequestFor(const SourceRecord &source)
    {
        return {source.normalized_path, source.package_hash, source.importer_id,
                source.importer_version, source.settings_hash, source.native_model_version};
    }
}

TEST(ModelArchiveHashTest, ProducesStableVectorsAndCanonicalPaths)
{
    EXPECT_EQ(Sha256("").ToHex(),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(Sha256("abc").ToHex(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::optional<ContentHash> parsed = ContentHash::FromHex(Sha256("abc").ToHex());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, Sha256("abc"));
    EXPECT_EQ(NormalizeAssetRelativePath("Models\\./Characters/../Triangle.OBJ"),
              "models/triangle.obj");
    EXPECT_EQ(ProductRelativePath(ArchiveProductType::Model, Sha256("product")),
              "models/" + Sha256("product").ToHex() + ".model");
    EXPECT_EQ(ProductRelativePath(ArchiveProductType::Texture, Sha256("texture"), "PNG"),
              "textures/" + Sha256("texture").ToHex() + ".png");

    const std::vector<std::byte> product_bytes = Bytes("product-with-digest");
    const auto hashes = Sha256WithZeroedRange(product_bytes, 3, 5);
    ASSERT_TRUE(hashes.has_value());
    EXPECT_EQ(hashes->content_hash, Sha256(product_bytes));
    std::vector<std::byte> zeroed_bytes = product_bytes;
    std::fill(zeroed_bytes.begin() + 3, zeroed_bytes.begin() + 8, std::byte{0});
    EXPECT_EQ(hashes->zeroed_range_hash, Sha256(zeroed_bytes));
    EXPECT_FALSE(Sha256WithZeroedRange(product_bytes, product_bytes.size() + 1, 0));

    EXPECT_EQ(CatchArchiveError([] { (void)NormalizeAssetRelativePath("../outside.obj"); }),
              ModelArchiveErrorCode::InvalidArgument);
    EXPECT_EQ(CatchArchiveError(
                  [] { (void)ProductRelativePath(ArchiveProductType::Texture, Sha256("x"), "p/g"); }),
              ModelArchiveErrorCode::InvalidArgument);
}

TEST(ModelArchiveHashTest, VerifiesNativeTextureProductsUsingTheirExtension)
{
    const std::vector<std::byte> bytes = Bytes("native-texture-product");
    const ContentHash hash = Sha256(bytes);
    const std::filesystem::path archive_root = "test-archive";
    const std::filesystem::path product =
        archive_root / ProductRelativePath(ArchiveProductType::Texture, hash, "texture");
    std::string diagnostic;

    EXPECT_TRUE(VerifyArchiveProduct(product, ArchiveProductType::Texture, bytes,
                                     diagnostic, archive_root));
    EXPECT_TRUE(VerifyArchiveProduct(product, ArchiveProductType::Texture, bytes,
                                     diagnostic, archive_root, hash));
    EXPECT_TRUE(diagnostic.empty());

    diagnostic.clear();
    EXPECT_FALSE(VerifyArchiveProduct(archive_root / "textures" / hash.ToHex(),
                                     ArchiveProductType::Texture, bytes, diagnostic,
                                     archive_root));
    EXPECT_EQ(diagnostic, "archive texture product has no extension");
}

TEST(ModelArchiveHashTest, OrdersSourceClosureAndSeparatesImportSettings)
{
    const ContentHash first = Sha256("first");
    const ContentHash second = Sha256("second");
    const std::vector<SourceFingerprintInput> ordered{{"models/a.obj", first},
                                                      {"textures/a.png", second}};
    const std::vector<SourceFingerprintInput> reversed{{"textures\\a.PNG", second},
                                                       {"models/a.obj", first}};
    EXPECT_EQ(HashSourcePackage(ordered), HashSourcePackage(reversed));
    EXPECT_EQ(CatchArchiveError([&]
                                { (void)HashSourcePackage({{"models/a.obj", first},
                                                            {"models\\a.obj", second}}); }),
              ModelArchiveErrorCode::InvalidArgument);

    ImportKeyInput input{HashSourcePackage(ordered), "assimp", 1, Sha256("settings-v1"), 1, 1};
    const ContentHash original = HashImportKey(input);
    input.settings_hash = Sha256("settings-v2");
    EXPECT_NE(original, HashImportKey(input));
    input.settings_hash = Sha256("settings-v1");
    input.material_schema_version = 2;
    EXPECT_NE(original, HashImportKey(input));
}

TEST(ModelArchiveDatabaseTest, PublishesSourcesAndDistinguishesProbeStates)
{
    TemporaryArchive temporary;
    ModelArchiveDatabase archive{temporary.DatabasePath()};
    PublishedSource published = MakePublishedSource();
    WriteBytes(archive.ArchiveRoot() / published.product.relative_path, published.product_bytes);

    archive.ReplaceSource(published.source, published.dependencies, {published.product},
                          {published.source_product}, {published.material_override});

    const std::optional<SourceArchiveSnapshot> snapshot =
        archive.FindSource(published.source.normalized_path);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->source.normalized_path, published.source.normalized_path);
    const std::optional<SourceArchiveSnapshot> logical_snapshot =
        archive.FindSourceByLogicalPath("models/triangle");
    ASSERT_TRUE(logical_snapshot.has_value());
    EXPECT_EQ(logical_snapshot->source.normalized_path, published.source.normalized_path);
    const std::optional<std::filesystem::path> resolved_product =
        archive.ResolveModelProductPath("models/triangle");
    ASSERT_TRUE(resolved_product.has_value());
    EXPECT_EQ(*resolved_product, archive.ArchiveRoot() / published.product.relative_path);

    ModelArchiveDatabase read_only_archive{temporary.DatabasePath(), 2500,
                                           kpengine::asset::ModelArchiveOpenMode::ReadOnly};
    const std::optional<std::filesystem::path> read_only_product =
        read_only_archive.ResolveModelProductPath("models/triangle");
    ASSERT_TRUE(read_only_product.has_value());
    EXPECT_EQ(*read_only_product, *resolved_product);

    EXPECT_EQ(snapshot->dependencies.size(), 1u);
    ASSERT_EQ(snapshot->products.size(), 1u);
    EXPECT_EQ(snapshot->products.front().content_hash, published.product.content_hash);
    ASSERT_EQ(snapshot->source_products.size(), 1u);
    EXPECT_EQ(snapshot->source_products.front().display_name, "Triangle model");
    ASSERT_EQ(snapshot->material_overrides.size(), 1u);

    SourceProbeRequest request = RequestFor(published.source);
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::UpToDate);

    request.package_hash = Sha256("changed-package");
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::SourcePackageChanged);
    request = RequestFor(published.source);
    request.importer_version++;
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::ImporterChanged);
    request = RequestFor(published.source);
    request.settings_hash = Sha256("changed-settings");
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::SettingsChanged);
    request = RequestFor(published.source);
    request.native_model_version++;
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::NativeSchemaChanged);
    request = RequestFor(published.source);
    request.normalized_path = "models/missing.obj";
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::SourceNotFound);

    std::filesystem::remove(archive.ArchiveRoot() / published.product.relative_path);
    request = RequestFor(published.source);
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::MissingProduct);

    WriteBytes(archive.ArchiveRoot() / published.product.relative_path, Bytes("model-product-v2"));
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::CorruptProduct);

    WriteBytes(archive.ArchiveRoot() / published.product.relative_path, published.product_bytes);
    EXPECT_EQ(archive.ProbeSource(request).status, ArchiveProbeStatus::UpToDate);

    archive.RemoveSource(published.source.normalized_path);
    EXPECT_FALSE(archive.FindSource(published.source.normalized_path).has_value());
    EXPECT_FALSE(archive.FindSourceByLogicalPath("models/triangle").has_value());
}

TEST(ModelArchiveDatabaseTest, RejectsNewerAndCorruptDatabases)
{
    TemporaryArchive temporary;
    {
        Database future{(temporary.Root() / "future.sqlite3").string()};
        future.Execute("PRAGMA user_version=99;");
    }
    EXPECT_EQ(CatchArchiveError([&]
                                { ModelArchiveDatabase database{temporary.Root() / "future.sqlite3"}; }),
              ModelArchiveErrorCode::NewerSchema);

    const std::filesystem::path corrupt_path = temporary.Root() / "corrupt.sqlite3";
    WriteBytes(corrupt_path, Bytes("not a sqlite database"));
    EXPECT_EQ(CatchArchiveError([&]
                                { ModelArchiveDatabase database{corrupt_path}; }),
              ModelArchiveErrorCode::InvalidDatabase);
}

TEST(ModelArchiveDatabaseTest, ReportsBusyWriterAndDoesNotPublishPartialSource)
{
    TemporaryArchive temporary;
    ModelArchiveDatabase archive{temporary.DatabasePath(), 25};
    PublishedSource published = MakePublishedSource();
    WriteBytes(archive.ArchiveRoot() / published.product.relative_path, published.product_bytes);
    archive.ReplaceSource(published.source, published.dependencies, {published.product},
                          {published.source_product}, {published.material_override});

    ModelArchiveDatabase writer{temporary.DatabasePath(), 25};
    Database lock{temporary.DatabasePath().string()};
    lock.Execute("BEGIN IMMEDIATE;");

    PublishedSource replacement = published;
    replacement.source.display_name = "Replacement";
    EXPECT_EQ(CatchArchiveError([&]
                                {
                                    writer.ReplaceSource(replacement.source,
                                                          replacement.dependencies,
                                                          {replacement.product},
                                                          {replacement.source_product},
                                                          {replacement.material_override});
                                }),
              ModelArchiveErrorCode::ArchiveBusy);
    lock.Execute("ROLLBACK;");

    const std::optional<SourceArchiveSnapshot> snapshot =
        archive.FindSource(published.source.normalized_path);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->source.display_name, "Triangle");
}
