#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

        void WriteBinary(const std::string &relative,
                         const std::vector<unsigned char> &contents) const
        {
            const std::filesystem::path path = root_ / relative;
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(file.is_open());
            if (!contents.empty())
            {
                file.write(reinterpret_cast<const char *>(contents.data()),
                           static_cast<std::streamsize>(contents.size()));
            }
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

    std::vector<unsigned char> OneByOnePng()
    {
        return {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
                0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
                0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
                0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
                0x54, 0x78, 0x9C, 0x63, 0x68, 0x68, 0xF8, 0xFF,
                0x1F, 0x00, 0x06, 0x82, 0x02, 0xFF, 0xB3, 0xBE,
                0x51, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
                0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    }

    void WriteTexturedMaterialClosure(ImportFixture &fixture, std::size_t material_count,
                                       const std::vector<std::string> &texture_names,
                                       int variant)
    {
        ASSERT_FALSE(texture_names.empty());
        std::ostringstream obj;
        obj << "mtllib triangle.mtl\n"
            << "o TexturedClosure\n";
        for (std::size_t index = 0; index < material_count; ++index)
        {
            const std::size_t vertex = index * 3 + 1;
            const float x = static_cast<float>(index % 16) * 2.0f;
            const float y = static_cast<float>(index / 16) * 2.0f;
            obj << "v " << x << ' ' << y << " 0\n"
                << "v " << x + 1.0f << ' ' << y << " 0\n"
                << "v " << x << ' ' << y + 1.0f << " 0\n"
                << "vt 0 0\nvt 1 0\nvt 0 1\n"
                << "vn 0 0 1\n"
                << "usemtl Material" << index << "\n"
                << "f " << vertex << '/' << vertex << '/' << vertex << ' '
                << vertex + 1 << '/' << vertex + 1 << '/' << vertex + 1 << ' '
                << vertex + 2 << '/' << vertex + 2 << '/' << vertex + 2 << "\n";
        }
        fixture.Write("models/triangle.obj", obj.str());

        std::ostringstream mtl;
        mtl << std::fixed << std::setprecision(3);
        for (std::size_t index = 0; index < material_count; ++index)
        {
            const float base = 0.1f + static_cast<float>(index % 40) * 0.01f;
            mtl << "newmtl Material" << index << "\n"
                << "Kd " << base << ' ' << base + 0.1f << ' ' << base + 0.2f << "\n"
                << "Ns " << static_cast<float>(index + 1 + variant) << "\n"
                << "map_Kd " << texture_names[index % texture_names.size()] << "\n";
        }
        fixture.Write("models/triangle.mtl", mtl.str());
    }

    void ExpectStagingDirectoryEmpty(const std::filesystem::path &archive_root)
    {
        const std::filesystem::path staging = archive_root / "staging";
        if (std::filesystem::exists(staging))
        {
            ASSERT_TRUE(std::filesystem::is_directory(staging));
            EXPECT_EQ(std::distance(std::filesystem::directory_iterator(staging),
                                    std::filesystem::directory_iterator{}),
                      0);
        }
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
    for (const auto &source_product : snapshot->source_products)
    {
        EXPECT_NE(source_product.asset_type, ArchiveProductType::Texture);
    }
}

TEST(ModelImportServiceTest, ProgressIsMonotonicAndCallbackIsCoordinatorOwned)
{
    ImportFixture fixture;
    std::vector<kpengine::asset::ModelImportProgress> progress;
    std::vector<std::thread::id> callback_threads;
    ModelImportRequest request = fixture.Request();
    request.progress_callback = [&progress, &callback_threads](
                                    const kpengine::asset::ModelImportProgress &update)
    {
        progress.push_back(update);
        callback_threads.push_back(std::this_thread::get_id());
    };

    ModelImportService service;
    ASSERT_NO_THROW((void)service.Import(request));
    ASSERT_FALSE(progress.empty());
    ASSERT_EQ(callback_threads.size(), progress.size());
    for (std::size_t index = 1; index < progress.size(); ++index)
    {
        const unsigned previous_stage = static_cast<unsigned>(progress[index - 1].stage);
        const unsigned current_stage = static_cast<unsigned>(progress[index].stage);
        EXPECT_LE(previous_stage, current_stage);
        if (previous_stage == current_stage)
        {
            EXPECT_LE(progress[index - 1].completed, progress[index].completed);
        }
        EXPECT_LE(progress[index].completed, progress[index].total);
        EXPECT_EQ(callback_threads[index - 1], callback_threads[index]);
    }
}

TEST(ModelImportServiceTest, ExecutionPolicyChangesDoNotChangeCookedProducts)
{
    ImportFixture fixture;
    fixture.WriteBinary("models/albedo.png",
                        {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
                         0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                         0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
                         0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
                         0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
                         0x54, 0x78, 0x9C, 0x63, 0x68, 0x68, 0xF8, 0xFF,
                         0x1F, 0x00, 0x06, 0x82, 0x02, 0xFF, 0xB3, 0xBE,
                         0x51, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
                         0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82});
    fixture.Write("models/triangle.mtl",
                  "newmtl TriangleMaterial\n"
                  "Kd 0.8 0.7 0.6\n"
                  "map_Kd albedo.png\n");

    ModelImportService service;
    ModelImportRequest serial_request = fixture.Request();
    serial_request.archive_root = fixture.Root() / ".archive_serial";
    serial_request.execution.texture_worker_count = 1;
    serial_request.execution.texture_memory_budget_bytes = 5ull * 1024ull * 1024ull;
    serial_request.execution.completion_queue_capacity = 1;
    const auto serial = service.Import(serial_request);

    ModelImportRequest parallel_request = fixture.Request();
    parallel_request.archive_root = fixture.Root() / ".archive_parallel";
    parallel_request.execution.texture_worker_count = 2;
    parallel_request.execution.texture_memory_budget_bytes = 5ull * 1024ull * 1024ull;
    parallel_request.execution.completion_queue_capacity = 1;
    const auto parallel = service.Import(parallel_request);

    EXPECT_EQ(parallel.model_hash, serial.model_hash);
    EXPECT_EQ(parallel.material_hashes, serial.material_hashes);
    EXPECT_EQ(parallel.texture_hashes, serial.texture_hashes);
    EXPECT_GT(parallel.metrics.total_texture_jobs, 0u);
    EXPECT_EQ(parallel.metrics.completed_texture_jobs, parallel.metrics.total_texture_jobs);
    EXPECT_EQ(parallel.metrics.texture_worker_count, 2u);
    EXPECT_EQ(parallel.metrics.completion_queue_capacity, 1u);
    EXPECT_LE(parallel.metrics.peak_completion_queue_size, 1u);
    EXPECT_EQ(parallel.metrics.current_reserved_bytes, 0u);

    fixture.Write("models/triangle.mtl",
                  "newmtl TriangleMaterial\n"
                  "Kd 0.2 0.3 0.4\n"
                  "map_Kd albedo.png\n");
    ModelImportRequest cancelled_request = parallel_request;
    cancelled_request.execution.cancellation_requested = [] { return true; };
    EXPECT_EQ(CatchImportError([&] { (void)service.Import(cancelled_request); }),
              ModelImportErrorCode::Cancelled);

    ModelArchiveDatabase archive{parallel_request.archive_root / "archive.sqlite3"};
    const auto snapshot = archive.FindSource("models/triangle.obj");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->source.package_hash, parallel.source_package_hash);
    const std::filesystem::path staging = parallel_request.archive_root / "staging";
    if (std::filesystem::exists(staging))
    {
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(staging),
                                std::filesystem::directory_iterator{}), 0);
    }
}

TEST(ModelImportServiceTest, EncoderQualityParticipatesInImportIdentity)
{
    ImportFixture fixture;
    fixture.WriteBinary("models/albedo.png",
                        {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
                         0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                         0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
                         0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
                         0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
                         0x54, 0x78, 0x9C, 0x63, 0x68, 0x68, 0xF8, 0xFF,
                         0x1F, 0x00, 0x06, 0x82, 0x02, 0xFF, 0xB3, 0xBE,
                         0x51, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
                         0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82});
    fixture.Write("models/triangle.mtl",
                  "newmtl TriangleMaterial\n"
                  "Kd 0.8 0.7 0.6\n"
                  "map_Kd albedo.png\n");

    ModelImportService service;
    const ModelImportRequest balanced_request = fixture.Request();
    const auto balanced = service.Import(balanced_request);
    ASSERT_EQ(balanced.status, ModelImportStatus::Imported);

    ModelImportRequest fast_request = balanced_request;
    fast_request.settings.texture_settings.bc_quality =
        kpengine::asset::TextureBcQuality::Fast;
    const auto fast = service.Import(fast_request);
    EXPECT_EQ(fast.status, ModelImportStatus::Imported);
    EXPECT_FALSE(fast.metrics.cache_hit);
    EXPECT_EQ(fast.model_hash, balanced.model_hash);

    const auto repeated_fast = service.Import(fast_request);
    EXPECT_EQ(repeated_fast.status, ModelImportStatus::UpToDate);
    EXPECT_TRUE(repeated_fast.metrics.cache_hit);
}

TEST(ModelImportServiceTest, QueueDepthOneBackpressuresWorkersWithSlowCoordinator)
{
    ImportFixture fixture;
    const std::vector<unsigned char> png =
        {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
         0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
         0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
         0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
         0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
         0x54, 0x78, 0x9C, 0x63, 0x68, 0x68, 0xF8, 0xFF,
         0x1F, 0x00, 0x06, 0x82, 0x02, 0xFF, 0xB3, 0xBE,
         0x51, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
         0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    std::vector<unsigned char> png_b = png;
    png_b.push_back(0x01);
    std::vector<unsigned char> png_c = png;
    png_c.push_back(0x02);
    fixture.WriteBinary("models/albedo_a.png", png);
    fixture.WriteBinary("models/albedo_b.png", png_b);
    fixture.WriteBinary("models/albedo_c.png", png_c);
    fixture.Write("models/triangle.obj",
                  "mtllib triangle.mtl\n"
                  "o Triangle\n"
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 1 1 0\n"
                  "v 0 1 0\n"
                  "vt 0 0\n"
                  "vt 1 0\n"
                  "vt 1 1\n"
                  "vt 0 1\n"
                  "vn 0 0 1\n"
                  "usemtl MaterialA\n"
                  "f 1/1/1 2/2/1 3/3/1\n"
                  "usemtl MaterialB\n"
                  "f 1/1/1 3/3/1 4/4/1\n"
                  "usemtl MaterialC\n"
                  "f 1/1/1 4/4/1 2/2/1\n");
    fixture.Write("models/triangle.mtl",
                  "newmtl MaterialA\n"
                  "Kd 0.8 0.7 0.6\n"
                  "map_Kd albedo_a.png\n"
                  "newmtl MaterialB\n"
                  "Kd 0.6 0.7 0.8\n"
                  "map_Kd albedo_b.png\n"
                  "newmtl MaterialC\n"
                  "Kd 0.4 0.5 0.6\n"
                  "map_Kd albedo_c.png\n");

    std::atomic_uint32_t consumed{};
    ModelImportRequest request = fixture.Request();
    request.execution.texture_worker_count = 2;
    request.execution.texture_memory_budget_bytes = 64ull * 1024ull * 1024ull;
    request.execution.completion_queue_capacity = 1;
    request.execution.before_completion_consume = [&consumed]
    {
        consumed.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    };

    ModelImportService service;
    const auto result = service.Import(request);

    EXPECT_EQ(result.metrics.total_texture_jobs, 3u);
    EXPECT_EQ(result.metrics.completed_texture_jobs, 3u);
    EXPECT_EQ(result.metrics.peak_completion_queue_size, 1u);
    EXPECT_GT(result.metrics.worker_queue_wait_seconds, 0.0);
    EXPECT_EQ(consumed.load(std::memory_order_relaxed), 3u);
    EXPECT_EQ(result.metrics.current_reserved_bytes, 0u);
}

TEST(ModelImportServiceTest, RepeatedEquivalentTextureClosureDoesNotGrowCookReservation)
{
    ImportFixture fixture;
    fixture.WriteBinary("models/shared.png", OneByOnePng());
    ModelImportService service;

    WriteTexturedMaterialClosure(fixture, 1, {"shared.png"}, 0);
    ModelImportRequest single_request = fixture.Request();
    single_request.archive_root = fixture.Root() / ".archive_single";
    single_request.execution.texture_worker_count = 4;
    single_request.execution.texture_memory_budget_bytes = 64ull * 1024ull * 1024ull;
    single_request.execution.completion_queue_capacity = 2;
    const auto single = service.Import(single_request);

    WriteTexturedMaterialClosure(fixture, 96, {"shared.png"}, 1);
    ModelImportRequest repeated_request = fixture.Request();
    repeated_request.archive_root = fixture.Root() / ".archive_repeated";
    repeated_request.execution = single_request.execution;
    const auto repeated = service.Import(repeated_request);

    ASSERT_EQ(single.metrics.total_texture_jobs, 1u);
    ASSERT_EQ(repeated.metrics.total_texture_jobs, 1u);
    EXPECT_EQ(single.metrics.unique_cook_keys, 1u);
    EXPECT_EQ(repeated.metrics.unique_cook_keys, 1u);
    EXPECT_EQ(single.metrics.estimated_texture_bytes, repeated.metrics.estimated_texture_bytes);
    EXPECT_EQ(single.metrics.peak_reserved_bytes, repeated.metrics.peak_reserved_bytes);
    EXPECT_EQ(repeated.metrics.current_reserved_bytes, 0u);
    EXPECT_GE(repeated.material_hashes.size(), 96u);
    EXPECT_EQ(repeated.metrics.requested_texture_bindings, 96u);
}

TEST(ModelImportServiceTest, FailureMatrixJoinsBlockedWorkersAndPreservesPublication)
{
    ImportFixture fixture;
    const std::vector<std::string> texture_names{"albedo_a.png", "albedo_b.png", "albedo_c.png"};
    for (std::size_t index = 0; index < texture_names.size(); ++index)
    {
        std::vector<unsigned char> png = OneByOnePng();
        png.push_back(static_cast<unsigned char>(index + 1));
        fixture.WriteBinary("models/" + texture_names[index], png);
    }
    WriteTexturedMaterialClosure(fixture, texture_names.size(), texture_names, 0);

    ModelImportService service;
    ModelImportRequest base_request = fixture.Request();
    base_request.execution.texture_worker_count = 2;
    base_request.execution.texture_memory_budget_bytes = 64ull * 1024ull * 1024ull;
    base_request.execution.completion_queue_capacity = 1;
    const auto baseline = service.Import(base_request);
    ASSERT_EQ(baseline.status, ModelImportStatus::Imported);

    const auto expect_previous_publication = [&]
    {
        ModelArchiveDatabase archive{base_request.archive_root / "archive.sqlite3"};
        const auto snapshot = archive.FindSource("models/triangle.obj");
        ASSERT_TRUE(snapshot.has_value());
        EXPECT_EQ(snapshot->source.package_hash, baseline.source_package_hash);
        EXPECT_TRUE(std::filesystem::is_regular_file(baseline.model_path));
        ExpectStagingDirectoryEmpty(base_request.archive_root);
    };

    std::vector<unsigned char> corrupt_png = OneByOnePng();
    corrupt_png[48] ^= 0xFF;
    fixture.WriteBinary("models/albedo_a.png", corrupt_png);
    WriteTexturedMaterialClosure(fixture, texture_names.size(), texture_names, 1);
    EXPECT_EQ(CatchImportError([&] { (void)service.Import(base_request); }),
              ModelImportErrorCode::ConversionFailed);
    expect_previous_publication();
    fixture.WriteBinary("models/albedo_a.png", OneByOnePng());

    std::atomic_uint32_t producer_cancel_checks{};
    ModelImportRequest blocked_producer = base_request;
    blocked_producer.execution.cancellation_requested = [&producer_cancel_checks]
    {
        return producer_cancel_checks.fetch_add(1, std::memory_order_relaxed) >= 4;
    };
    blocked_producer.execution.before_completion_consume = []
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
    };
    WriteTexturedMaterialClosure(fixture, texture_names.size(), texture_names, 2);
    EXPECT_EQ(CatchImportError([&] { (void)service.Import(blocked_producer); }),
              ModelImportErrorCode::Cancelled);
    expect_previous_publication();

    std::atomic_uint32_t memory_cancel_checks{};
    ModelImportRequest blocked_memory = base_request;
    blocked_memory.execution.texture_memory_budget_bytes = 8ull * 1024ull * 1024ull;
    blocked_memory.execution.completion_queue_capacity = 2;
    blocked_memory.execution.cancellation_requested = [&memory_cancel_checks]
    {
        return memory_cancel_checks.fetch_add(1, std::memory_order_relaxed) >= 4;
    };
    blocked_memory.execution.before_completion_consume = []
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
    };
    WriteTexturedMaterialClosure(fixture, texture_names.size(), texture_names, 3);
    EXPECT_EQ(CatchImportError([&] { (void)service.Import(blocked_memory); }),
              ModelImportErrorCode::Cancelled);
    expect_previous_publication();

    ModelImportRequest cancelled = base_request;
    cancelled.execution.cancellation_requested = [] { return true; };
    WriteTexturedMaterialClosure(fixture, texture_names.size(), texture_names, 4);
    EXPECT_EQ(CatchImportError([&] { (void)service.Import(cancelled); }),
              ModelImportErrorCode::Cancelled);
    expect_previous_publication();

    WriteTexturedMaterialClosure(fixture, texture_names.size(), texture_names, 5);
    const std::filesystem::path staging_file = base_request.archive_root / "staging";
    std::error_code staging_cleanup_error;
    std::filesystem::remove_all(staging_file, staging_cleanup_error);
    ASSERT_FALSE(staging_cleanup_error);
    {
        std::ofstream file(staging_file, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << "sentinel";
    }
    EXPECT_EQ(CatchImportError([&] { (void)service.Import(base_request); }),
              ModelImportErrorCode::PublicationFailed);
    EXPECT_EQ(ReadText(staging_file), "sentinel");
    {
        ModelArchiveDatabase archive{base_request.archive_root / "archive.sqlite3"};
        const auto snapshot = archive.FindSource("models/triangle.obj");
        ASSERT_TRUE(snapshot.has_value());
        EXPECT_EQ(snapshot->source.package_hash, baseline.source_package_hash);
        EXPECT_TRUE(std::filesystem::is_regular_file(baseline.model_path));
    }
    std::filesystem::remove(staging_file, staging_cleanup_error);
    ASSERT_FALSE(staging_cleanup_error);
    expect_previous_publication();
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

    ModelArchiveDatabase archive{fixture.Root() / ".archive" / "archive.sqlite3"};
    const auto snapshot = archive.FindSource("models/triangle.obj");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->source.package_hash, first.source_package_hash);
    EXPECT_EQ(snapshot->source_products.front().content_hash, first.model_hash);

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
