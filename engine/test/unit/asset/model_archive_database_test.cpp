#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "database/database.h"
#include "asset/model_archive.h"

namespace
{
    using kpengine::asset::ArchiveCatalogSource;
    using kpengine::asset::ArchiveProbeStatus;
    using kpengine::asset::ArchiveProductType;
    using kpengine::asset::ContentHash;
    using kpengine::asset::HashImportKey;
    using kpengine::asset::HashSourcePackage;
    using kpengine::asset::ImportKeyInput;
    using kpengine::asset::MaterialOverrideRecord;
    using kpengine::asset::ModelArchiveCatalogReadLimits;
    using kpengine::asset::ModelArchiveCatalogSnapshot;
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

    std::vector<std::byte> ToBlob(const ContentHash &hash)
    {
        std::vector<std::byte> blob;
        blob.reserve(hash.bytes.size());
        for (const std::uint8_t value : hash.bytes)
        {
            blob.push_back(static_cast<std::byte>(value));
        }
        return blob;
    }

    ProductRecord CatalogProduct(ArchiveProductType type, std::string_view contents,
                                 std::string_view texture_extension = {})
    {
        ProductRecord product;
        product.content_hash = Sha256(contents);
        product.asset_type = type;
        product.relative_path =
            ProductRelativePath(type, product.content_hash, texture_extension);
        product.byte_size = contents.size();
        product.schema_version = 1;
        return product;
    }

    // Writes one source with its products and links, mirroring what the
    // importer publishes.
    void PublishCatalogSource(ModelArchiveDatabase &archive, std::string_view source_path,
                              std::string_view display_name,
                              const std::vector<std::pair<ProductRecord, std::vector<std::byte>>>
                                  &products,
                              const std::vector<SourceProductRecord> &links,
                              SourceImportStatus status = SourceImportStatus::Ready,
                              std::string diagnostic = {})
    {
        SourceRecord source;
        source.normalized_path = std::string(source_path);
        source.path_hash = Sha256(source.normalized_path);
        source.display_name = std::string(display_name);
        source.importer_id = "assimp";
        source.importer_version = 1;
        source.settings_hash = Sha256("settings-v1");
        source.native_model_version = 1;
        source.status = status;
        source.diagnostic = std::move(diagnostic);

        const std::vector<SourceDependencyRecord> dependencies{
            {source.normalized_path, Sha256(source.normalized_path)}};
        source.package_hash =
            HashSourcePackage({{dependencies.front().normalized_path,
                                dependencies.front().content_hash}});

        std::vector<ProductRecord> records;
        records.reserve(products.size());
        for (const auto &[product, bytes] : products)
        {
            WriteBytes(archive.ArchiveRoot() / product.relative_path, bytes);
            records.push_back(product);
        }
        archive.ReplaceSource(source, dependencies, records, links, {});
    }

    std::vector<SourceProductRecord> Link(ArchiveProductType type, const ProductRecord &product,
                                          std::int32_t role, std::int32_t slot,
                                          std::string display_name)
    {
        return {{product.content_hash, type, role, slot, std::move(display_name)}};
    }

    std::string CatalogKeyOf(const ModelArchiveCatalogSnapshot &catalog, std::size_t index)
    {
        const ProductRecord &product = catalog.products[index];
        return std::to_string(static_cast<unsigned>(product.asset_type)) + "/" +
               product.content_hash.ToHex();
    }
}

TEST(ModelArchiveCatalogTest, EmptyReadOnlyArchiveReturnsEmptyOrderedValues)
{
    TemporaryArchive temporary;
    {
        ModelArchiveDatabase initialized{temporary.DatabasePath()};
    }
    ModelArchiveDatabase archive{temporary.DatabasePath(), 2500,
                                 kpengine::asset::ModelArchiveOpenMode::ReadOnly};
    const ModelArchiveCatalogSnapshot catalog = archive.ReadCatalog();
    EXPECT_TRUE(catalog.sources.empty());
    EXPECT_TRUE(catalog.products.empty());
}

TEST(ModelArchiveCatalogTest, EnumeratesGlobalProductsOrderedByTypeThenHash)
{
    TemporaryArchive temporary;
    ModelArchiveDatabase archive{temporary.DatabasePath()};

    const ProductRecord model = CatalogProduct(ArchiveProductType::Model, "model-bytes");
    const ProductRecord material = CatalogProduct(ArchiveProductType::Material, "material-bytes");
    // A Texture row with no source link is still part of the catalog.
    const ProductRecord texture =
        CatalogProduct(ArchiveProductType::Texture, "texture-bytes", "texture");

    PublishCatalogSource(archive, "models/b.obj", "B",
                         {{model, Bytes("model-bytes")}},
                         Link(ArchiveProductType::Model, model, 0, -1, "B model"));
    PublishCatalogSource(archive, "models/a.obj", "A",
                         {{material, Bytes("material-bytes")}, {texture, Bytes("texture-bytes")}},
                         Link(ArchiveProductType::Material, material, 1, 0, "A material"));
    // The texture is published by a product-row-only source and keeps no link.
    PublishCatalogSource(archive, "models/c.obj", "C",
                         {{CatalogProduct(ArchiveProductType::Model, "model-c"),
                           Bytes("model-c")}},
                         {});

    const ModelArchiveCatalogSnapshot catalog = archive.ReadCatalog();
    ASSERT_EQ(catalog.sources.size(), 3u);
    EXPECT_EQ(catalog.sources[0].source.normalized_path, "models/a.obj");
    EXPECT_EQ(catalog.sources[1].source.normalized_path, "models/b.obj");
    EXPECT_EQ(catalog.sources[2].source.normalized_path, "models/c.obj");

    ASSERT_EQ(catalog.products.size(), 4u);
    for (std::size_t index = 1; index < catalog.products.size(); ++index)
    {
        const ProductRecord &previous = catalog.products[index - 1];
        const ProductRecord &current = catalog.products[index];
        const bool ordered =
            previous.asset_type == current.asset_type
                ? previous.content_hash < current.content_hash
                : static_cast<std::uint8_t>(previous.asset_type) <
                      static_cast<std::uint8_t>(current.asset_type);
        EXPECT_TRUE(ordered) << "product ordering broke at index " << index;
    }

    const auto found_texture = std::find_if(
        catalog.products.begin(), catalog.products.end(),
        [](const ProductRecord &product)
        { return product.asset_type == ArchiveProductType::Texture; });
    ASSERT_NE(found_texture, catalog.products.end());
    EXPECT_EQ(found_texture->content_hash, texture.content_hash);

    // Dependency metadata stays attached to its own source.
    ASSERT_EQ(catalog.sources[0].dependencies.size(), 1u);
    EXPECT_EQ(catalog.sources[0].dependencies.front().normalized_path, "models/a.obj");
    EXPECT_EQ(catalog.sources[0].dependencies.front().content_hash, Sha256("models/a.obj"));
    EXPECT_EQ(catalog.sources[0].source_products.size(), 1u);
    EXPECT_TRUE(catalog.sources[2].source_products.empty());
}

TEST(ModelArchiveCatalogTest, SharesOneProductAcrossSourcesAndPreservesFailedStatus)
{
    TemporaryArchive temporary;
    ModelArchiveDatabase archive{temporary.DatabasePath()};
    const ProductRecord shared = CatalogProduct(ArchiveProductType::Model, "shared-model");

    PublishCatalogSource(archive, "models/a.obj", "A",
                         {{shared, Bytes("shared-model")}},
                         Link(ArchiveProductType::Model, shared, 0, -1, "Shared model"));
    PublishCatalogSource(archive, "models/b.obj", "B",
                         {{shared, Bytes("shared-model")}},
                         Link(ArchiveProductType::Model, shared, 0, -1, "Shared model again"),
                         SourceImportStatus::Failed, "assimp rejected the file");

    const ModelArchiveCatalogSnapshot catalog = archive.ReadCatalog();
    ASSERT_EQ(catalog.products.size(), 1u);
    ASSERT_EQ(catalog.sources.size(), 2u);
    EXPECT_EQ(catalog.sources[0].source_products.front().display_name, "Shared model");
    EXPECT_EQ(catalog.sources[1].source_products.front().display_name, "Shared model again");
    EXPECT_EQ(catalog.sources[1].source.status, SourceImportStatus::Failed);
    EXPECT_EQ(catalog.sources[1].source.diagnostic, "assimp rejected the file");

    ModelArchiveDatabase read_only{temporary.DatabasePath(), 2500,
                                   kpengine::asset::ModelArchiveOpenMode::ReadOnly};
    const ModelArchiveCatalogSnapshot read_only_catalog = read_only.ReadCatalog();
    EXPECT_EQ(CatalogKeyOf(read_only_catalog, 0), CatalogKeyOf(catalog, 0));
    EXPECT_EQ(read_only_catalog.sources[1].source.diagnostic, "assimp rejected the file");
}

TEST(ModelArchiveCatalogTest, EnforcesConfiguredReadLimits)
{
    TemporaryArchive temporary;
    ModelArchiveDatabase archive{temporary.DatabasePath()};
    const ProductRecord model = CatalogProduct(ArchiveProductType::Model, "model-bytes");
    PublishCatalogSource(archive, "models/a.obj", "A", {{model, Bytes("model-bytes")}},
                         Link(ArchiveProductType::Model, model, 0, -1, "A model"));

    kpengine::asset::ModelArchiveCatalogReadLimits limits;
    limits.max_products = 0;
    EXPECT_EQ(CatchArchiveError([&] { (void)archive.ReadCatalog(limits); }),
              ModelArchiveErrorCode::InvalidArgument);

    limits = {};
    limits.max_sources = 0;
    EXPECT_EQ(CatchArchiveError([&] { (void)archive.ReadCatalog(limits); }),
              ModelArchiveErrorCode::InvalidArgument);

    limits = {};
    limits.max_related_records = 0;
    EXPECT_EQ(CatchArchiveError([&] { (void)archive.ReadCatalog(limits); }),
              ModelArchiveErrorCode::InvalidArgument);

    EXPECT_EQ(archive.ReadCatalog().products.size(), 1u);
}

TEST(ModelArchiveCatalogTest, AbsentDependencyMetadataKeepsZeroMetricsAndPresentMetadataMerges)
{
    TemporaryArchive temporary;
    ModelArchiveDatabase archive{temporary.DatabasePath()};
    const ProductRecord model = CatalogProduct(ArchiveProductType::Model, "model-bytes");
    PublishCatalogSource(archive, "models/a.obj", "A", {{model, Bytes("model-bytes")}},
                         Link(ArchiveProductType::Model, model, 0, -1, "A model"));

    // An archive written before the optional metadata table existed carries no
    // row for a dependency. The dependency must still be reported, with zero
    // metrics rather than a dropped row or a synthesized value.
    Database raw{temporary.DatabasePath().string()};
    raw.Execute("DELETE FROM source_dependency_metadata;");

    const ModelArchiveCatalogSnapshot absent = archive.ReadCatalog();
    ASSERT_EQ(absent.sources.size(), 1u);
    ASSERT_EQ(absent.sources.front().dependencies.size(), 1u);
    const SourceDependencyRecord &plain = absent.sources.front().dependencies.front();
    EXPECT_EQ(plain.normalized_path, "models/a.obj");
    EXPECT_EQ(plain.content_hash, Sha256("models/a.obj"));
    EXPECT_EQ(plain.byte_size, 0u);
    EXPECT_EQ(plain.last_write_time, 0);

    // A present row merges into that same record without changing its identity.
    std::int64_t source_id = 0;
    {
        auto query = raw.Prepare("SELECT id FROM sources WHERE normalized_path = ?;");
        query.Bind(1, std::string("models/a.obj"));
        ASSERT_EQ(query.Step(), kpengine::database::StatementStep::Row);
        source_id = query.ColumnInt64(0);
    }
    {
        auto statement = raw.Prepare(
            "INSERT INTO source_dependency_metadata(source_id, normalized_path, byte_size, "
            "last_write_time) VALUES (?, ?, ?, ?);");
        statement.Bind(1, source_id);
        statement.Bind(2, std::string("models/a.obj"));
        statement.Bind(3, static_cast<std::int64_t>(4096));
        statement.Bind(4, static_cast<std::int64_t>(1700000000));
        EXPECT_EQ(statement.Step(), kpengine::database::StatementStep::Done);
    }

    const ModelArchiveCatalogSnapshot present = archive.ReadCatalog();
    ASSERT_EQ(present.sources.size(), 1u);
    ASSERT_EQ(present.sources.front().dependencies.size(), 1u);
    const SourceDependencyRecord &enriched = present.sources.front().dependencies.front();
    EXPECT_EQ(enriched.normalized_path, plain.normalized_path);
    EXPECT_EQ(enriched.content_hash, plain.content_hash);
    EXPECT_EQ(enriched.byte_size, 4096u);
    EXPECT_EQ(enriched.last_write_time, 1700000000);
}

TEST(ModelArchiveCatalogTest, ReadOnlyAndReadWriteReadsYieldIdenticalCatalogValues)
{
    TemporaryArchive temporary;
    const ProductRecord shared = CatalogProduct(ArchiveProductType::Model, "shared-model");
    const ProductRecord material = CatalogProduct(ArchiveProductType::Material, "material-bytes");
    const ProductRecord texture =
        CatalogProduct(ArchiveProductType::Texture, "texture-bytes", "texture");
    {
        ModelArchiveDatabase archive{temporary.DatabasePath()};
        PublishCatalogSource(archive, "models/b.obj", "B",
                             {{shared, Bytes("shared-model")},
                              {material, Bytes("material-bytes")}},
                             Link(ArchiveProductType::Material, material, 1, 0, "B material"));
        PublishCatalogSource(archive, "models/a.obj", "A", {{shared, Bytes("shared-model")}},
                             Link(ArchiveProductType::Model, shared, 0, -1, "Shared model"),
                             SourceImportStatus::Failed, "import failed");
        // A product row with no link keeps the products-only path in view.
        PublishCatalogSource(archive, "models/c.obj", "C",
                             {{texture, Bytes("texture-bytes")}}, {});
    }

    ModelArchiveDatabase read_write{temporary.DatabasePath()};
    ModelArchiveDatabase read_only{temporary.DatabasePath(), 2500,
                                   kpengine::asset::ModelArchiveOpenMode::ReadOnly};
    const ModelArchiveCatalogSnapshot from_write = read_write.ReadCatalog();
    const ModelArchiveCatalogSnapshot from_read = read_only.ReadCatalog();

    ASSERT_EQ(from_write.products.size(), from_read.products.size());
    for (std::size_t index = 0; index < from_write.products.size(); ++index)
    {
        EXPECT_EQ(CatalogKeyOf(from_write, index), CatalogKeyOf(from_read, index));
        const ProductRecord &left = from_write.products[index];
        const ProductRecord &right = from_read.products[index];
        EXPECT_EQ(left.relative_path, right.relative_path);
        EXPECT_EQ(left.byte_size, right.byte_size);
        EXPECT_EQ(left.schema_version, right.schema_version);
    }

    ASSERT_EQ(from_write.sources.size(), from_read.sources.size());
    for (std::size_t index = 0; index < from_write.sources.size(); ++index)
    {
        const ArchiveCatalogSource &left = from_write.sources[index];
        const ArchiveCatalogSource &right = from_read.sources[index];
        EXPECT_EQ(left.source.normalized_path, right.source.normalized_path);
        EXPECT_EQ(left.source.display_name, right.source.display_name);
        EXPECT_EQ(left.source.package_hash, right.source.package_hash);
        EXPECT_EQ(left.source.status, right.source.status);
        EXPECT_EQ(left.source.diagnostic, right.source.diagnostic);
        ASSERT_EQ(left.dependencies.size(), right.dependencies.size());
        for (std::size_t link = 0; link < left.dependencies.size(); ++link)
        {
            EXPECT_EQ(left.dependencies[link].normalized_path,
                      right.dependencies[link].normalized_path);
            EXPECT_EQ(left.dependencies[link].content_hash, right.dependencies[link].content_hash);
        }
        ASSERT_EQ(left.source_products.size(), right.source_products.size());
        for (std::size_t link = 0; link < left.source_products.size(); ++link)
        {
            EXPECT_EQ(left.source_products[link].content_hash,
                      right.source_products[link].content_hash);
            EXPECT_EQ(left.source_products[link].asset_type,
                      right.source_products[link].asset_type);
            EXPECT_EQ(left.source_products[link].role, right.source_products[link].role);
            EXPECT_EQ(left.source_products[link].slot, right.source_products[link].slot);
            EXPECT_EQ(left.source_products[link].display_name,
                      right.source_products[link].display_name);
        }
    }
}

TEST(ModelArchiveCatalogTest, RejectsCorruptRowsAndBrokenLinks)
{
    TemporaryArchive temporary;
    ModelArchiveDatabase archive{temporary.DatabasePath()};
    const ProductRecord model = CatalogProduct(ArchiveProductType::Model, "model-bytes");
    PublishCatalogSource(archive, "models/a.obj", "A", {{model, Bytes("model-bytes")}},
                         Link(ArchiveProductType::Model, model, 0, -1, "A model"));

    Database raw{temporary.DatabasePath().string()};
    raw.Execute("PRAGMA foreign_keys=OFF;");

    // An unknown product type is not a row the reader may skip.
    {
        auto statement = raw.Prepare(
            "INSERT INTO products(content_hash, asset_type, relative_path, byte_size, "
            "schema_version) VALUES (?, 99, 'models/unknown.model', 1, 1);");
        statement.Bind(1, ToBlob(Sha256("unknown-product")));
        EXPECT_EQ(statement.Step(), kpengine::database::StatementStep::Done);
    }
    EXPECT_EQ(CatchArchiveError([&] { (void)archive.ReadCatalog(); }),
              ModelArchiveErrorCode::InvalidDatabase);
    raw.Execute("DELETE FROM products WHERE asset_type=99;");
    EXPECT_NO_THROW(archive.ReadCatalog());

    // A negative byte size is corrupt, not zero.
    raw.Execute("UPDATE products SET byte_size=-1;");
    EXPECT_EQ(CatchArchiveError([&] { (void)archive.ReadCatalog(); }),
              ModelArchiveErrorCode::InvalidDatabase);
    raw.Execute("UPDATE products SET byte_size=11;");
    EXPECT_NO_THROW(archive.ReadCatalog());

    // An orphan link is reported instead of being silently joined away.
    {
        auto statement = raw.Prepare(
            "INSERT INTO source_products(source_id, content_hash, asset_type, role, slot, "
            "display_name) VALUES (999, ?, 1, 0, -1, 'Orphan');");
        statement.Bind(1, ToBlob(model.content_hash));
        EXPECT_EQ(statement.Step(), kpengine::database::StatementStep::Done);
    }
    EXPECT_EQ(CatchArchiveError([&] { (void)archive.ReadCatalog(); }),
              ModelArchiveErrorCode::InvalidDatabase);
}

TEST(ModelArchiveCatalogTest, ReadsOneCommittedEpochAcrossAConcurrentWriter)
{
    TemporaryArchive temporary;
    ModelArchiveDatabase archive{temporary.DatabasePath()};
    const ProductRecord model = CatalogProduct(ArchiveProductType::Model, "model-bytes");
    PublishCatalogSource(archive, "models/a.obj", "A", {{model, Bytes("model-bytes")}},
                         Link(ArchiveProductType::Model, model, 0, -1, "A model"));

    ModelArchiveDatabase reader{temporary.DatabasePath(), 50,
                                kpengine::asset::ModelArchiveOpenMode::ReadOnly};
    ASSERT_EQ(reader.ReadCatalog().products.size(), 1u);

    const ProductRecord pending = CatalogProduct(ArchiveProductType::Material, "pending-bytes");
    Database writer{temporary.DatabasePath().string()};
    writer.Execute("BEGIN IMMEDIATE;");
    {
        auto statement = writer.Prepare(
            "INSERT INTO products(content_hash, asset_type, relative_path, byte_size, "
            "schema_version) VALUES (?, 2, ?, 12, 1);");
        statement.Bind(1, ToBlob(pending.content_hash));
        statement.Bind(2, pending.relative_path);
        EXPECT_EQ(statement.Step(), kpengine::database::StatementStep::Done);
    }

    // The uncommitted row is invisible: the read sees one whole epoch.
    const ModelArchiveCatalogSnapshot before = reader.ReadCatalog();
    EXPECT_EQ(before.products.size(), 1u);

    writer.Execute("COMMIT;");
    const ModelArchiveCatalogSnapshot after = reader.ReadCatalog();
    ASSERT_EQ(after.products.size(), 2u);
    EXPECT_EQ(after.products.back().content_hash, pending.content_hash);
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
    EXPECT_EQ(archive.ProbeSourceFast(request).status, ArchiveProbeStatus::UpToDate);
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

    std::vector<std::byte> same_size_corruption = published.product_bytes;
    same_size_corruption.front() = std::byte{
        static_cast<unsigned char>(std::to_integer<unsigned char>(same_size_corruption.front()) ^ 1u)};
    WriteBytes(archive.ArchiveRoot() / published.product.relative_path, same_size_corruption);
    EXPECT_EQ(archive.ProbeSourceFast(request).status, ArchiveProbeStatus::UpToDate);
    EXPECT_EQ(CatchArchiveError([&] { archive.IntegrityCheck(); }),
              ModelArchiveErrorCode::CorruptProduct);
    WriteBytes(archive.ArchiveRoot() / published.product.relative_path, published.product_bytes);
    EXPECT_NO_THROW(archive.IntegrityCheck());

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
