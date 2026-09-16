#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset.h"
#include "asset/asset_catalog.h"
#include "asset/asset_catalog_snapshot_provider.h"
#include "asset/asset_manager.h"
#include "asset/asset_payload.h"
#include "asset/common.h"
#include "asset/content_metadata.h"
#include "asset/mesh.h"
#include "asset/utility.h"
#include "config/path.h"

// This target owns its own executable so the process-wide AssetManager
// singleton, type registry, and caches start from a known state. The provider
// is exercised against real manager storage and real SQLite files; the graph
// assembly itself is covered by AssetCatalogBuilderTest.
namespace
{
    using kpengine::asset::AssetCatalogAvailability;
    using kpengine::asset::AssetCatalogDiagnosticCode;
    using kpengine::asset::AssetCatalogNode;
    using kpengine::asset::AssetCatalogProviderConfig;
    using kpengine::asset::AssetCatalogSnapshot;
    using kpengine::asset::AssetCatalogSnapshotProvider;
    using kpengine::asset::AssetCatalogSnapshotStatus;
    using kpengine::asset::AssetID;
    using kpengine::asset::ArchiveProductType;
    using kpengine::asset::AssetManager;
    using kpengine::asset::AssetRegisterInfo;
    using kpengine::asset::AssetType;
    using kpengine::asset::AssetTypeDescriptor;
    using kpengine::asset::ContentHash;
    using kpengine::asset::IAssetCatalogSnapshotSource;
    using kpengine::asset::MeshResource;

    constexpr AssetType kProbeAssetType = static_cast<AssetType>(
        kpengine::asset::kFirstCustomAssetTypeValue + 0x2A0u);
    constexpr const char *kProbeExtension = "axcatalogprobe";
    constexpr const char *kProbeFileName = "catalog_provider_probe.axcatalogprobe";
    constexpr const char *kProbeName = "AXCatalogProbe";
    constexpr const char *kProbeTypeName = "AXCatalogProbeType";

    // Placed under the asset root so the catalog reports a project-relative
    // logical path. Nothing is ever read from disk: the probe loader is a stub.
    std::string ProbeFilePath()
    {
        return (std::filesystem::path(kpengine::GetAssetDirectory()) / kProbeFileName)
            .generic_string();
    }

    struct ProbePayload final : kpengine::asset::IAssetPayload
    {
        AssetType GetAssetType() const noexcept override
        {
            return kProbeAssetType;
        }
    };

    // A temporary archive root that mirrors the production layout.
    class TemporaryArchive final
    {
    public:
        TemporaryArchive()
        {
            static std::atomic_uint64_t sequence{};
            root_ = std::filesystem::temp_directory_path() /
                    ("kpengine_catalog_provider_test_" +
                     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            std::filesystem::create_directories(root_ / ".archive");
        }

        ~TemporaryArchive() noexcept
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        TemporaryArchive(const TemporaryArchive &) = delete;
        TemporaryArchive &operator=(const TemporaryArchive &) = delete;

        std::filesystem::path DatabasePath() const
        {
            return root_ / ".archive" / "archive.sqlite3";
        }

        const std::filesystem::path &Root() const noexcept { return root_; }

    private:
        std::filesystem::path root_;
    };

    // The manager is a process-wide singleton, so the probe type is declared
    // once and every test reuses the same registration.
    bool EnsureProbeType()
    {
        static const bool registered = []
        {
            AssetManager &manager = AssetManager::GetInstance();
            std::string diagnostic;
            AssetTypeDescriptor descriptor{};
            descriptor.type = kProbeAssetType;
            descriptor.name = kProbeTypeName;
            descriptor.extensions = {kProbeExtension};
            descriptor.loader = [](const std::string &path, AssetRegisterInfo &info)
            {
                info.resource = std::make_shared<ProbePayload>();
                // The manager stores the loader-provided path verbatim, and the
                // path index is keyed from it.
                info.path = path;
                info.name = kProbeName;
                info.type = kProbeAssetType;
                return true;
            };
            return manager.RegisterAssetType(std::move(descriptor), diagnostic);
        }();
        return registered;
    }

    std::size_t CountDiagnostics(const AssetCatalogSnapshot &snapshot,
                                 AssetCatalogDiagnosticCode code)
    {
        return static_cast<std::size_t>(std::count_if(
            snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
            [code](const kpengine::asset::AssetCatalogDiagnostic &entry)
            { return entry.code == code; }));
    }

    const AssetCatalogNode *FindNodeByPackedId(const AssetCatalogSnapshot &snapshot,
                                               std::uint64_t packed_id)
    {
        for (const AssetCatalogNode &node : snapshot.nodes)
        {
            if (node.packed_runtime_asset_id.has_value() &&
                *node.packed_runtime_asset_id == packed_id)
            {
                return &node;
            }
        }
        return nullptr;
    }

    void ExpectValidSnapshot(const AssetCatalogSnapshot &snapshot)
    {
        std::string diagnostic;
        EXPECT_TRUE(kpengine::asset::ValidateAssetCatalogSnapshot(snapshot, diagnostic))
            << diagnostic;
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::CatalogAssemblyFailed),
                  0u);
    }

    AssetRegisterInfo MakeMeshInfo(const std::string &path, const std::string &name)
    {
        AssetRegisterInfo info{};
        info.resource = std::make_shared<MeshResource>();
        info.path = path;
        info.name = name;
        info.type = AssetType::KPAT_Mesh;
        return info;
    }

    TEST(AssetCatalogProviderTest, MissingArchivePublishesAValidPartialLiveSnapshot)
    {
        ASSERT_TRUE(EnsureProbeType());

        TemporaryArchive archive;
        const AssetID probe = AssetManager::GetInstance().LoadSync(ProbeFilePath());
        ASSERT_TRUE(probe.IsValid());

        AssetCatalogSnapshotProvider provider(
            AssetManager::GetInstance(),
            AssetCatalogProviderConfig{archive.DatabasePath(), 50, {}});

        const auto started = std::chrono::steady_clock::now();
        const AssetCatalogSnapshot snapshot = provider.CaptureAssetCatalog();
        const auto elapsed = std::chrono::steady_clock::now() - started;

        ExpectValidSnapshot(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::ArchiveUnavailable), 1u);
        // The live half is still published, including the probe.
        EXPECT_NE(FindNodeByPackedId(snapshot, probe.Pack()), nullptr);
        // A missing database is answered promptly, not after a retry storm.
        EXPECT_LT(elapsed, std::chrono::seconds(5));

        AssetManager::GetInstance().UnRegisterAsset(probe);
    }

    TEST(AssetCatalogProviderTest, UnreadableArchiveFileIsReportedWithoutThrowing)
    {
        TemporaryArchive archive;
        {
            std::ofstream file(archive.DatabasePath(), std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(file.is_open());
            file << "this file is not a sqlite database";
        }

        AssetCatalogSnapshotProvider provider(
            AssetManager::GetInstance(),
            AssetCatalogProviderConfig{archive.DatabasePath(), 50, {}});

        const AssetCatalogSnapshot snapshot = provider.CaptureAssetCatalog();

        ExpectValidSnapshot(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::ArchiveUnavailable), 1u);
    }

    TEST(AssetCatalogProviderTest, EmptyConfigResolvesTheDefaultArchiveUnderTheAssetRoot)
    {
        // The repository archive may or may not exist in a given checkout, so
        // this asserts the resolved default path is usable either way.
        AssetCatalogSnapshotProvider provider(AssetManager::GetInstance(),
                                              AssetCatalogProviderConfig{});

        // Captured through the boundary interface the Editor will consume.
        IAssetCatalogSnapshotSource &source = provider;
        const AssetCatalogSnapshot snapshot = source.CaptureAssetCatalog();

        ExpectValidSnapshot(snapshot);
        EXPECT_GT(snapshot.revision, 0u);
    }

    TEST(AssetCatalogProviderTest, MetadataProjectionReplacesTheLegacyProductGraph)
    {
        TemporaryArchive archive;
        const std::filesystem::path content_root = archive.Root() / "content";
        const std::filesystem::path content_archive = content_root / ".archive";
        const ContentHash product_hash = kpengine::asset::Sha256("content model");
        const std::filesystem::path product_path =
            content_archive / kpengine::asset::ProductRelativePath(
                                  kpengine::asset::ArchiveProductType::Model, product_hash);
        std::error_code error;
        std::filesystem::create_directories(product_path.parent_path(), error);
        ASSERT_FALSE(error) << error.message();
        {
            std::ofstream product(product_path, std::ios::binary);
            ASSERT_TRUE(product.is_open());
            product << "content model";
        }

        kpengine::asset::ContentMetadata metadata;
        metadata.id = kpengine::asset::ContentID("content-model-id");
        metadata.asset_type = AssetType::KPAT_Model;
        metadata.type_name = "model";
        metadata.name = "Model_Content";
        metadata.content_path = "model/content";
        metadata.products.push_back({ArchiveProductType::Model, product_hash});
        std::string metadata_diagnostic;
        ASSERT_TRUE(kpengine::asset::WriteContentMetadata(
            content_root, metadata, &metadata_diagnostic))
            << metadata_diagnostic;

        AssetCatalogProviderConfig config;
        config.content_root = content_root;
        AssetCatalogSnapshotProvider provider(AssetManager::GetInstance(), config);

        const AssetCatalogSnapshot snapshot = provider.CaptureAssetCatalog();
        ExpectValidSnapshot(snapshot);
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_EQ(snapshot.nodes.front().content_id, "content-model-id");
        EXPECT_EQ(snapshot.nodes.front().stable_key,
                  kpengine::asset::MakeContentCatalogKey("content-model-id"));
        EXPECT_EQ(snapshot.nodes.front().display_name, "Model_Content");
        EXPECT_EQ(snapshot.nodes.front().logical_path, "model/content");
        EXPECT_EQ(snapshot.nodes.front().content_hash, product_hash);
        EXPECT_TRUE(snapshot.edges.empty());
    }

    TEST(AssetCatalogProviderTest, ExistingEmptyContentRootSuppressesLegacyRows)
    {
        ASSERT_TRUE(EnsureProbeType());

        TemporaryArchive archive;
        const std::filesystem::path content_root = archive.Root() / "content";
        std::error_code error;
        std::filesystem::create_directories(content_root, error);
        ASSERT_FALSE(error) << error.message();

        const AssetID probe = AssetManager::GetInstance().LoadSync(ProbeFilePath());
        ASSERT_TRUE(probe.IsValid());

        AssetCatalogProviderConfig config;
        config.database_path = archive.DatabasePath();
        config.content_root = content_root;
        AssetCatalogSnapshotProvider provider(AssetManager::GetInstance(), config);

        const AssetCatalogSnapshot snapshot = provider.CaptureAssetCatalog();
        ExpectValidSnapshot(snapshot);
        EXPECT_TRUE(snapshot.nodes.empty());
        EXPECT_TRUE(snapshot.edges.empty());
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Complete);

        AssetManager::GetInstance().UnRegisterAsset(probe);
    }

    TEST(AssetCatalogProviderTest, LiveAssetsAreCopiedWithoutPayloadDataCrossingTheContract)
    {
        ASSERT_TRUE(EnsureProbeType());

        TemporaryArchive archive;
        const AssetID probe = AssetManager::GetInstance().LoadSync(ProbeFilePath());
        ASSERT_TRUE(probe.IsValid());

        AssetCatalogSnapshotProvider provider(
            AssetManager::GetInstance(),
            AssetCatalogProviderConfig{archive.DatabasePath(), 50, {}});
        const AssetCatalogSnapshot snapshot = provider.CaptureAssetCatalog();

        ExpectValidSnapshot(snapshot);
        const AssetCatalogNode *node = FindNodeByPackedId(snapshot, probe.Pack());
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->availability, AssetCatalogAvailability::RuntimeOnly);
        EXPECT_EQ(node->type, kProbeAssetType);
        EXPECT_EQ(node->type_name, kProbeTypeName);
        EXPECT_EQ(node->display_name, kProbeName);
        EXPECT_EQ(node->logical_path, kProbeFileName);
        // The manager path index still resolves this path, so the key is the
        // AB1.0 path-key spelling rather than a transient identity.
        EXPECT_EQ(node->stable_key,
                  kpengine::asset::MakeRuntimePathCatalogKey(
                      kProbeAssetType,
                      kpengine::asset::CanonicalAssetPathKey(ProbeFilePath())));
        // A runtime identity carries no archived product facts.
        EXPECT_FALSE(node->archive_product_type.has_value());
        EXPECT_FALSE(node->content_hash.has_value());
        EXPECT_TRUE(node->product_path.empty());
        // The key is an AB1.0 spelling, not a pointer or address.
        ASSERT_TRUE(node->stable_key.rfind(kpengine::asset::kAssetCatalogKeyPrefix, 0) == 0);

        // Destroying the live Asset and its payload cannot alter an already
        // published snapshot, because the contract carries copied values only.
        AssetManager::GetInstance().UnRegisterAsset(probe);
        EXPECT_EQ(AssetManager::GetInstance().GetAsset(probe), nullptr);
        EXPECT_EQ(node->display_name, kProbeName);
        EXPECT_EQ(node->logical_path, kProbeFileName);
        EXPECT_EQ(node->availability, AssetCatalogAvailability::RuntimeOnly);
        std::string diagnostic;
        EXPECT_TRUE(kpengine::asset::ValidateAssetCatalogSnapshot(snapshot, diagnostic))
            << diagnostic;
    }

    TEST(AssetCatalogProviderTest, ConsecutiveCapturesAdvanceRevisionAndKeepNodeKeys)
    {
        TemporaryArchive archive;
        AssetCatalogSnapshotProvider provider(
            AssetManager::GetInstance(),
            AssetCatalogProviderConfig{archive.DatabasePath(), 50, {}});

        const AssetCatalogSnapshot first = provider.CaptureAssetCatalog();
        const AssetCatalogSnapshot second = provider.CaptureAssetCatalog();
        const AssetCatalogSnapshot third = provider.CaptureAssetCatalog();

        ExpectValidSnapshot(first);
        ExpectValidSnapshot(second);
        ExpectValidSnapshot(third);
        EXPECT_GT(first.revision, 0u);
        EXPECT_LT(first.revision, second.revision);
        EXPECT_LT(second.revision, third.revision);

        // Nothing changed between captures, so the graph identity must not.
        ASSERT_EQ(first.nodes.size(), third.nodes.size());
        for (std::size_t index = 0; index < first.nodes.size(); ++index)
        {
            EXPECT_EQ(first.nodes[index].stable_key, third.nodes[index].stable_key);
            EXPECT_EQ(first.nodes[index].id.value, third.nodes[index].id.value);
        }
    }

    TEST(AssetCatalogProviderTest, CaptureDoesNotMutateManagerState)
    {
        TemporaryArchive archive;
        AssetManager &manager = AssetManager::GetInstance();

        const std::size_t meshes_before = manager.GetLiveAssetCount(AssetType::KPAT_Mesh);
        const std::size_t total_before = manager.GetTotalLiveAssetCount();

        AssetRegisterInfo info = MakeMeshInfo("catalog_provider_state_probe.mesh",
                                              "CatalogProviderStateProbe");
        const AssetID id = manager.RegisterAsset(info);
        ASSERT_TRUE(id.IsValid());

        AssetCatalogSnapshotProvider provider(
            manager, AssetCatalogProviderConfig{archive.DatabasePath(), 50, {}});
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            ExpectValidSnapshot(provider.CaptureAssetCatalog());
        }

        EXPECT_EQ(manager.GetLiveAssetCount(AssetType::KPAT_Mesh), meshes_before + 1);
        EXPECT_EQ(manager.GetTotalLiveAssetCount(), total_before + 1);
        const kpengine::asset::Asset *const asset = manager.GetAsset(id);
        ASSERT_NE(asset, nullptr);
        EXPECT_EQ(asset->GetName(), "CatalogProviderStateProbe");
        EXPECT_EQ(asset->GetPath(), "catalog_provider_state_probe.mesh");
        EXPECT_TRUE(asset->GetDependencies().empty());

        manager.UnRegisterAsset(id);
        EXPECT_EQ(manager.GetLiveAssetCount(AssetType::KPAT_Mesh), meshes_before);
        EXPECT_EQ(manager.GetTotalLiveAssetCount(), total_before);
    }

    TEST(AssetCatalogProviderTest, ConcurrentMutationAndLookupNeverYieldAnInvalidSnapshot)
    {
        TemporaryArchive archive;
        AssetManager &manager = AssetManager::GetInstance();

        const std::size_t total_before = manager.GetTotalLiveAssetCount();
        std::atomic<bool> stop{false};
        std::atomic<std::uint32_t> failures{0};

        std::thread worker(
            [&]
            {
                std::uint32_t sequence = 0;
                while (!stop.load(std::memory_order_relaxed))
                {
                    AssetRegisterInfo info = MakeMeshInfo(
                        "catalog_provider_concurrent_" + std::to_string(sequence) + ".mesh",
                        "CatalogProviderConcurrent" + std::to_string(sequence));
                    const AssetID id = manager.RegisterAsset(info);
                    if (!id.IsValid())
                    {
                        failures.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    // A live-state lookup and an allowed unload both take the
                    // same lock the capture takes.
                    const kpengine::asset::Asset *const asset = manager.GetAsset(id);
                    if (asset == nullptr || !(asset->GetID() == id))
                    {
                        failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    manager.UnRegisterAsset(id);
                    ++sequence;
                }
            });

        AssetCatalogSnapshotProvider provider(
            manager, AssetCatalogProviderConfig{archive.DatabasePath(), 50, {}});
        std::uint64_t previous_revision = 0;
        for (int attempt = 0; attempt < 40; ++attempt)
        {
            const AssetCatalogSnapshot snapshot = provider.CaptureAssetCatalog();
            ExpectValidSnapshot(snapshot);
            EXPECT_GT(snapshot.revision, previous_revision);
            previous_revision = snapshot.revision;
        }
        stop.store(true, std::memory_order_relaxed);
        worker.join();

        EXPECT_EQ(failures.load(std::memory_order_relaxed), 0u);
        // Every worker asset was unloaded, so the capture published copies only.
        EXPECT_EQ(manager.GetTotalLiveAssetCount(), total_before);
    }

    TEST(AssetCatalogProviderTest, DestroyingTheProviderLeavesTheManagerUsable)
    {
        TemporaryArchive archive;
        AssetManager &manager = AssetManager::GetInstance();
        const std::size_t total_before = manager.GetTotalLiveAssetCount();

        {
            AssetCatalogSnapshotProvider provider(
                manager, AssetCatalogProviderConfig{archive.DatabasePath(), 50, {}});
            ExpectValidSnapshot(provider.CaptureAssetCatalog());
        }

        AssetRegisterInfo info = MakeMeshInfo("catalog_provider_after_destroy.mesh",
                                              "CatalogProviderAfterDestroy");
        const AssetID id = manager.RegisterAsset(info);
        ASSERT_TRUE(id.IsValid());
        ASSERT_NE(manager.GetAsset(id), nullptr);

        // A fresh provider still sees the manager it borrows.
        AssetCatalogSnapshotProvider fresh(
            manager, AssetCatalogProviderConfig{archive.DatabasePath(), 50, {}});
        const AssetCatalogSnapshot snapshot = fresh.CaptureAssetCatalog();
        ExpectValidSnapshot(snapshot);
        EXPECT_NE(FindNodeByPackedId(snapshot, id.Pack()), nullptr);

        manager.UnRegisterAsset(id);
        EXPECT_EQ(manager.GetTotalLiveAssetCount(), total_before);
    }
}
