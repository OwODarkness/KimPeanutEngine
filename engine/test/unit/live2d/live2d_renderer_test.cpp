#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "asset/asset_import_registry.h"
#include "asset/asset_manager.h"
#include "config/path.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "live2d_import.h"
#include "live2d_model_resource.h"
#include "live2d_registration.h"
#include "live2d_renderer.h"
#include "live2d_system.h"
#include "render/frame_context.h"
#include "support/fake_render_backend.h"

namespace
{
    using kpengine::asset::AssetID;
    using kpengine::graphics::CommandRecorder;
    using kpengine::graphics::RenderBackend;
    using kpengine::graphics::RenderTargetHandle;
    using kpengine::graphics::RenderTargetView;
    using kpengine::live2d::Live2DRenderer;
    using kpengine::live2d::Live2DSystem;
    using kpengine::test::BackendProbe;
    using kpengine::test::FakeBackend;

    constexpr const char *kFixtureModelPath =
        "live2d/hiyori_pro/runtime/hiyori_pro_t11.model3.json";
    constexpr uint32_t kInitialWidth = 720u;
    constexpr uint32_t kInitialHeight = 960u;
    constexpr uint32_t kResizedWidth = 1024u;
    constexpr uint32_t kResizedHeight = 768u;
    constexpr size_t kUniformCapacity = 4u * 1024u * 1024u;

    std::filesystem::path MakeFixtureContentRoot()
    {
        // Distinct from the root the asset tests use so a partially written
        // product in one file cannot be observed by the other.
        return std::filesystem::temp_directory_path() /
               "kpengine_live2d_renderer_test_root";
    }

    // Registration is not idempotent and another file in this binary registers
    // the same type, so a duplicate is not a failure here. The load below is
    // the real check that the type resolves.
    void EnsureLive2DAssetTypeRegistered()
    {
        static const bool registered = []
        {
            std::string diagnostic;
            kpengine::live2d::RegisterLive2DAssetTypes(
                kpengine::asset::AssetManager::GetInstance(), diagnostic);
            return true;
        }();
        (void)registered;
    }

    // Imports the checked-in model3 package into a content root and loads it
    // through the AssetManager, so the renderer sees the same product and
    // texture dependencies the viewer does. The licensed model is an
    // environmental prerequisite (asset/ is not distributed), so an absent or
    // unimportable source leaves the AssetID invalid and the tests skip.
    AssetID LoadFixtureModel(std::string &diagnostic)
    {
        EnsureLive2DAssetTypeRegistered();

        kpengine::asset::ImportProviderRegistry registry;
        if (!kpengine::live2d::RegisterLive2DImporters(registry, diagnostic) ||
            !registry.Seal(diagnostic))
        {
            return {};
        }

        const std::filesystem::path content_root = MakeFixtureContentRoot();
        std::error_code cleanup_error;
        std::filesystem::remove_all(content_root, cleanup_error);

        kpengine::asset::ImportProviderRequest request{};
        request.asset_root = kpengine::project_root / "asset";
        request.archive_root = content_root / ".archive";
        request.source_path = kFixtureModelPath;
        const auto imported = registry.Execute(request, {}, diagnostic);
        const auto product = std::dynamic_pointer_cast<
            kpengine::asset::TypedImportProduct<
                kpengine::live2d::Live2DImportProduct,
                kpengine::asset::ImportProviderKind::Custom>>(imported.product);
        if (product == nullptr)
        {
            return {};
        }

        std::filesystem::create_directories(content_root);
        const std::filesystem::path output = content_root / "hiyori.live2d";
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            diagnostic = "could not write the Live2D renderer fixture product";
            return {};
        }
        file.write(reinterpret_cast<const char *>(product->value.product_bytes.data()),
                   static_cast<std::streamsize>(product->value.product_bytes.size()));
        file.close();
        return kpengine::asset::AssetManager::GetInstance().LoadSync(output.generic_string());
    }

    // One initialized renderer and the fake backend it records through.
    struct RendererHarness
    {
        explicit RendererHarness(Live2DSystem &system, const AssetID &asset)
            : renderer(system, asset)
        {
        }

        std::shared_ptr<BackendProbe> probe = std::make_shared<BackendProbe>();
        FakeBackend backend{probe};
        kpengine::render::FrameContext frame;
        Live2DRenderer renderer;
        std::string diagnostic;
        uint64_t frame_number = 0u;

        // Records one frame at whatever extent the renderer currently owns.
        bool RecordFrame()
        {
            CommandRecorder *const recorder = backend.GetCommandRecorder();
            if (recorder == nullptr)
            {
                diagnostic = "fake backend exposed no command recorder";
                return false;
            }
            frame.Begin(0u, {frame_number, 0.0f, 1.0f / 60.0f},
                        backend.GetRenderExtent());
            const bool recorded =
                renderer.Record(frame, *recorder, 1.0f / 60.0f, diagnostic);
            frame.End();
            ++frame_number;
            return recorded;
        }
    };

    // The renderer allocates real Cubism state, so the whole suite shares one
    // framework initialize/shutdown pair: the lifecycle owns process-global
    // Framework state and refuses shutdown while a model instance is live.
    class Live2DRendererTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            if (!system_.Initialize())
            {
                fixture_diagnostic_ = "the Cubism framework could not initialize";
                return;
            }
            system_initialized_ = true;
            model_asset_ = LoadFixtureModel(fixture_diagnostic_);
        }

        static void TearDownTestSuite()
        {
            if (system_initialized_)
            {
                EXPECT_EQ(system_.Cubism().LiveModelInstanceCount(), 0u)
                    << "a model instance lease outlived the test suite";
                // Shutdown reports false while a model instance is still live,
                // so this is a leak check as well as a teardown.
                EXPECT_TRUE(system_.Cubism().Shutdown())
                    << "Cubism refused shutdown with "
                    << system_.Cubism().LiveModelInstanceCount()
                    << " live model instance(s)";
            }
        }

        void SetUp() override
        {
            if (!system_initialized_ || !model_asset_.IsValid())
            {
                GTEST_SKIP() << "The checked-in Live2D product is unavailable in "
                                "this environment: " << fixture_diagnostic_;
            }
        }

        static Live2DSystem system_;
        static bool system_initialized_;
        static AssetID model_asset_;
        static std::string fixture_diagnostic_;
    };

    Live2DSystem Live2DRendererTest::system_;
    bool Live2DRendererTest::system_initialized_ = false;
    AssetID Live2DRendererTest::model_asset_{};
    std::string Live2DRendererTest::fixture_diagnostic_;

    // A different non-zero extent builds a replacement, waits for submitted
    // work before releasing the previous target, and leaves the renderer able
    // to record at the new extent.
    TEST_F(Live2DRendererTest, ResizeOutputSwapsTheTargetAfterWaitingIdle)
    {
        RendererHarness harness(system_, model_asset_);
        harness.backend.Initialize({});
        ASSERT_TRUE(harness.renderer.Initialize(harness.backend, kInitialWidth,
                                                kInitialHeight,
                                                harness.diagnostic))
            << harness.diagnostic;
        harness.frame.Initialize(harness.backend, kUniformCapacity);

        const RenderTargetHandle initial_target = harness.renderer.GetOutputTarget();
        ASSERT_TRUE(initial_target.IsValid());
        ASSERT_TRUE(harness.RecordFrame()) << harness.diagnostic;

        const size_t targets_before = harness.probe->targets.size();
        const int destroys_before = harness.probe->render_target_destroy_count;
        const int waits_before = harness.probe->wait_idle_count;
        const size_t events_before = harness.probe->events.size();

        ASSERT_TRUE(harness.renderer.ResizeOutput(kResizedWidth, kResizedHeight,
                                                  harness.diagnostic))
            << harness.diagnostic;

        // The replacement is created alongside the old target, never in place
        // of it: a failed create must be able to leave the old one valid.
        ASSERT_EQ(harness.probe->targets.size(), targets_before + 1u);
        EXPECT_EQ(harness.probe->targets.back().width, kResizedWidth);
        EXPECT_EQ(harness.probe->targets.back().height, kResizedHeight);

        const RenderTargetHandle resized_target = harness.renderer.GetOutputTarget();
        EXPECT_TRUE(resized_target.IsValid());
        EXPECT_NE(resized_target.id, initial_target.id);
        EXPECT_EQ(harness.renderer.GetOutputView().width, kResizedWidth);
        EXPECT_EQ(harness.renderer.GetOutputView().height, kResizedHeight);

        EXPECT_EQ(harness.probe->wait_idle_count, waits_before + 1);
        ASSERT_EQ(harness.probe->render_target_destroy_count, destroys_before + 1);

        // Ordering, not just occurrence: the previous target must be released
        // only after WaitIdle covers the work that still referenced it.
        bool waited = false;
        bool waited_before_release = false;
        for (size_t index = events_before; index < harness.probe->events.size(); ++index)
        {
            const std::string &event = harness.probe->events[index];
            if (event == "wait_idle")
            {
                waited = true;
            }
            else if (event == "destroy_target")
            {
                waited_before_release = waited;
                break;
            }
        }
        EXPECT_TRUE(waited_before_release)
            << "the previous target was released before WaitIdle";

        // Usable afterwards: the recorded frame's own viewport carries the new
        // extent, so the resize reached the submission and not just a field.
        const size_t viewports_before = harness.probe->viewports.size();
        const int draws_before = harness.probe->draw_count;
        ASSERT_TRUE(harness.RecordFrame()) << harness.diagnostic;
        ASSERT_GT(harness.probe->viewports.size(), viewports_before);
        const kpengine::graphics::Viewport &viewport = harness.probe->viewports.back();
        EXPECT_FLOAT_EQ(viewport.width, static_cast<float>(kResizedWidth));
        EXPECT_FLOAT_EQ(viewport.height, static_cast<float>(kResizedHeight));
        EXPECT_GT(harness.probe->draw_count, draws_before);
    }

    // A backend that cannot create the replacement must leave the last good
    // target, view, and extent in place so the caller keeps its output.
    TEST_F(Live2DRendererTest, FailedResizeKeepsThePreviousTargetAndView)
    {
        RendererHarness harness(system_, model_asset_);
        harness.backend.Initialize({});
        ASSERT_TRUE(harness.renderer.Initialize(harness.backend, kInitialWidth,
                                                kInitialHeight,
                                                harness.diagnostic))
            << harness.diagnostic;
        harness.frame.Initialize(harness.backend, kUniformCapacity);
        ASSERT_TRUE(harness.RecordFrame()) << harness.diagnostic;

        const RenderTargetHandle initial_target = harness.renderer.GetOutputTarget();
        const RenderTargetView initial_view = harness.renderer.GetOutputView();
        ASSERT_TRUE(initial_target.IsValid());
        ASSERT_TRUE(initial_view.IsValid());

        harness.probe->fail_render_target = true;
        EXPECT_FALSE(harness.renderer.ResizeOutput(kResizedWidth, kResizedHeight,
                                                   harness.diagnostic));
        EXPECT_FALSE(harness.diagnostic.empty());
        harness.probe->fail_render_target = false;

        EXPECT_EQ(harness.renderer.GetOutputTarget().id, initial_target.id);
        const RenderTargetView kept_view = harness.renderer.GetOutputView();
        EXPECT_EQ(kept_view.width, initial_view.width);
        EXPECT_EQ(kept_view.height, initial_view.height);
        EXPECT_EQ(harness.probe->render_target_destroy_count, 0);

        // Usable at the extent it never lost: the frame records at 720x960.
        ASSERT_TRUE(harness.RecordFrame()) << harness.diagnostic;
        const kpengine::graphics::Viewport &viewport = harness.probe->viewports.back();
        EXPECT_FLOAT_EQ(viewport.width, static_cast<float>(kInitialWidth));
        EXPECT_FLOAT_EQ(viewport.height, static_cast<float>(kInitialHeight));
    }

    // A zero extent is rejected before any backend work, so the current target
    // and the renderer's extent are untouched.
    TEST_F(Live2DRendererTest, ZeroExtentIsRejectedWithoutTouchingTheTarget)
    {
        RendererHarness harness(system_, model_asset_);
        harness.backend.Initialize({});
        ASSERT_TRUE(harness.renderer.Initialize(harness.backend, kInitialWidth,
                                                kInitialHeight,
                                                harness.diagnostic))
            << harness.diagnostic;

        const RenderTargetHandle initial_target = harness.renderer.GetOutputTarget();
        const size_t targets_before = harness.probe->targets.size();
        const int destroys_before = harness.probe->render_target_destroy_count;
        const int waits_before = harness.probe->wait_idle_count;

        EXPECT_FALSE(harness.renderer.ResizeOutput(0u, kResizedHeight,
                                                   harness.diagnostic));
        EXPECT_FALSE(harness.diagnostic.empty());
        EXPECT_FALSE(harness.renderer.ResizeOutput(kResizedWidth, 0u,
                                                   harness.diagnostic));
        EXPECT_FALSE(harness.diagnostic.empty());

        EXPECT_EQ(harness.renderer.GetOutputTarget().id, initial_target.id);
        EXPECT_EQ(harness.renderer.GetOutputView().width, kInitialWidth);
        EXPECT_EQ(harness.renderer.GetOutputView().height, kInitialHeight);
        EXPECT_EQ(harness.probe->targets.size(), targets_before);
        EXPECT_EQ(harness.probe->render_target_destroy_count, destroys_before);
        EXPECT_EQ(harness.probe->wait_idle_count, waits_before);
    }

    // Asking for the extent the renderer already has must not churn targets.
    TEST_F(Live2DRendererTest, SameExtentResizeIsANoOp)
    {
        RendererHarness harness(system_, model_asset_);
        harness.backend.Initialize({});
        ASSERT_TRUE(harness.renderer.Initialize(harness.backend, kInitialWidth,
                                                kInitialHeight,
                                                harness.diagnostic))
            << harness.diagnostic;

        const RenderTargetHandle initial_target = harness.renderer.GetOutputTarget();
        const size_t targets_before = harness.probe->targets.size();
        const int destroys_before = harness.probe->render_target_destroy_count;
        const int waits_before = harness.probe->wait_idle_count;

        EXPECT_TRUE(harness.renderer.ResizeOutput(kInitialWidth, kInitialHeight,
                                                  harness.diagnostic))
            << harness.diagnostic;
        EXPECT_TRUE(harness.diagnostic.empty());

        EXPECT_EQ(harness.renderer.GetOutputTarget().id, initial_target.id);
        EXPECT_EQ(harness.probe->targets.size(), targets_before);
        EXPECT_EQ(harness.probe->render_target_destroy_count, destroys_before);
        EXPECT_EQ(harness.probe->wait_idle_count, waits_before);
    }

    // The shutdown contract: cleanup releases every handle the renderer created
    // and every Cubism model instance lease, and the framework then accepts
    // shutdown. The backend ledger is compared against its own create counts so
    // a handle the renderer merely forgot to clear cannot pass.
    TEST_F(Live2DRendererTest, CleanupReleasesEveryHandleAndModelInstance)
    {
        RendererHarness harness(system_, model_asset_);
        harness.backend.Initialize({});
        ASSERT_TRUE(harness.renderer.Initialize(harness.backend, kInitialWidth,
                                                kInitialHeight,
                                                harness.diagnostic))
            << harness.diagnostic;
        harness.frame.Initialize(harness.backend, kUniformCapacity);
        ASSERT_TRUE(harness.RecordFrame()) << harness.diagnostic;
        ASSERT_TRUE(harness.renderer.ResizeOutput(kResizedWidth, kResizedHeight,
                                                  harness.diagnostic))
            << harness.diagnostic;

        ASSERT_GT(harness.renderer.GetLiveGpuHandleCount(), 0u);
        ASSERT_EQ(system_.Cubism().LiveModelInstanceCount(), 1u);

        harness.renderer.Cleanup();

        EXPECT_EQ(harness.renderer.GetLiveGpuHandleCount(), 0u);
        EXPECT_EQ(system_.Cubism().LiveModelInstanceCount(), 0u);
        EXPECT_FALSE(harness.renderer.GetOutputTarget().IsValid());
        EXPECT_FALSE(harness.renderer.GetOutputView().IsValid());

        EXPECT_EQ(harness.probe->pipeline_destroy_count,
                  harness.probe->pipeline_create_count);
        EXPECT_EQ(harness.probe->sampler_destroy_count,
                  harness.probe->sampler_create_count);
        EXPECT_GT(harness.probe->render_target_destroy_count, 0);
        // The resized target is released too: mask atlas plus both outputs.
        EXPECT_EQ(harness.probe->targets.size(),
                  static_cast<size_t>(harness.probe->render_target_destroy_count));
    }
}
