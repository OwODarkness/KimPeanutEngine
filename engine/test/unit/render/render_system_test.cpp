#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "async/async_queue.h"
#include "asset/asset_manager.h"
#include "asset/texture.h"
#include "data/texture.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "log/log_system.h"
#include "resource/resource_pipeline.h"
#include "render/deferred_renderer.h"
#include "render/render_resource_resolver.h"
#include "render/render_system.h"

namespace
{
    using namespace kpengine;

    struct TargetRecord
    {
        std::string name;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct BackendProbe
    {
        bool fail_initialize = false;
        bool fail_uniform_mapping = false;
        bool fail_render_target = false;
        int fail_render_target_after = -1;
        bool fail_texture_creation = false;
        bool fail_mesh_creation = false;
        bool fail_sampler_creation = false;
        std::vector<std::string> events;
        std::vector<TargetRecord> targets;
        int wait_idle_count = 0;
        int cleanup_count = 0;
        int readback_count = 0;
        int texture_create_count = 0;
        int pipeline_create_count = 0;
        int pipeline_destroy_count = 0;
        int mesh_create_count = 0;
        int mesh_destroy_count = 0;
        int sampler_create_count = 0;
        int sampler_destroy_count = 0;
        int render_target_destroy_count = 0;
        std::vector<std::array<uint32_t, 4>> environment_binding_snapshots;
    };

    class FakeReadback final : public graphics::IRenderTargetReadback
    {
    public:
        explicit FakeReadback(BackendProbe &probe) : probe_(probe) {}

        bool EnqueueRenderTargetReadback(graphics::RenderTargetReadbackRequest request,
                                         graphics::RenderTargetReadbackCallback on_completed) override
        {
            ++probe_.readback_count;
            probe_.events.push_back("readback");
            if (!request.IsValid() || !on_completed)
            {
                return false;
            }
            graphics::CapturedImage image{};
            image.width = 1;
            image.height = 1;
            image.frame_number = request.frame_number;
            image.rgba8_pixels = {0, 0, 0, 255};
            on_completed({graphics::RenderTargetReadbackStatus::Captured,
                          std::move(image), {}});
            return true;
        }

        void CollectCompletedReadbacks() override {}

        void DrainPendingReadbacks(std::string diagnostic) override
        {
            (void)diagnostic;
            probe_.events.push_back("drain_readbacks");
        }

    private:
        BackendProbe &probe_;
    };

    class FakeBackend;

    class FakeCommandRecorder final : public graphics::CommandRecorder
    {
    public:
        explicit FakeCommandRecorder(FakeBackend &backend) : backend_(backend) {}

        bool BeginRenderTarget(graphics::RenderTargetHandle target) override;
        void EndRenderTarget() override;
        void BindPipeline(graphics::PipelineHandle) override {}
        void BindMesh(graphics::MeshHandle) override {}
        void BindResourceBindings(graphics::PipelineHandle,
                                  graphics::DescriptorSetHandle) override
        {
        }
        void SetViewport(const graphics::Viewport &) override {}
        void SetScissor(const graphics::Scissor &) override {}
        void DrawIndexed(uint32_t = 0, uint32_t = 1, uint32_t = 0, int32_t = 0,
                         uint32_t = 0) override {}

    private:
        FakeBackend &backend_;
    };

    class FakeBackend final : public graphics::RenderBackend
    {
    public:
        explicit FakeBackend(std::shared_ptr<BackendProbe> probe)
            : probe_(std::move(probe)), readback_(*probe_), recorder_(*this)
        {
        }

        ~FakeBackend() override { probe_->events.push_back("backend_destruct"); }

        void Initialize(WindowHandle) override
        {
            probe_->events.push_back("backend_initialize");
            if (probe_->fail_initialize)
            {
                throw std::runtime_error("fake backend initialization failure");
            }
        }

        graphics::PipelineHandle CreatePipelineResource(const graphics::PipelineDesc &) override
        {
            ++probe_->pipeline_create_count;
            return MakeHandle<graphics::PipelineHandle>();
        }

        bool DestroyPipelineResource(graphics::PipelineHandle) override
        {
            ++probe_->pipeline_destroy_count;
            probe_->events.push_back("destroy_pipeline");
            return true;
        }

        graphics::MeshHandle CreateMesh(const data::MeshData &) override
        {
            ++probe_->mesh_create_count;
            if (probe_->fail_mesh_creation)
            {
                return {};
            }
            return MakeHandle<graphics::MeshHandle>();
        }

        bool DestroyMesh(graphics::MeshHandle) override
        {
            ++probe_->mesh_destroy_count;
            probe_->events.push_back("destroy_mesh");
            return true;
        }

        graphics::TextureHandle CreateTexture(const data::TextureData &,
                                              const graphics::TextureSettings &) override
        {
            ++probe_->texture_create_count;
            if (probe_->fail_texture_creation)
            {
                return {};
            }
            return MakeHandle<graphics::TextureHandle>();
        }

        bool DestroyTexture(graphics::TextureHandle) override { return true; }

        graphics::SamplerHandle CreateSampler(const graphics::SamplerSettings &) override
        {
            ++probe_->sampler_create_count;
            if (probe_->fail_sampler_creation)
            {
                return {};
            }
            return MakeHandle<graphics::SamplerHandle>();
        }

        bool DestroySampler(graphics::SamplerHandle) override
        {
            ++probe_->sampler_destroy_count;
            probe_->events.push_back("destroy_sampler");
            return true;
        }

        graphics::RenderTargetHandle CreateRenderTarget(
            const graphics::RenderTargetDesc &desc) override
        {
            if (probe_->fail_render_target ||
                (probe_->fail_render_target_after >= 0 &&
                 static_cast<int>(probe_->targets.size()) >= probe_->fail_render_target_after))
            {
                return {};
            }

            static constexpr const char *names[] = {
                "SceneColor",       "GBuffer",       "DirectionalShadow", "SpotShadow",
                "PointShadow",      "SceneHdr",      "CaptureOutput",
            };
            const std::size_t name_index = probe_->targets.size() % 7;
            probe_->targets.push_back({names[name_index], desc.width, desc.height});
            const graphics::RenderTargetHandle handle = MakeHandle<graphics::RenderTargetHandle>();
            target_names_[handle.id] = names[name_index];
            return handle;
        }

        bool DestroyRenderTarget(graphics::RenderTargetHandle) override
        {
            ++probe_->render_target_destroy_count;
            probe_->events.push_back("destroy_target");
            return true;
        }

        graphics::TextureHandle GetRenderTargetColor(graphics::RenderTargetHandle target) override
        {
            return {target.id, 0};
        }

        graphics::TextureHandle GetRenderTargetColorAttachment(
            graphics::RenderTargetHandle target, uint32_t) override
        {
            return {target.id, 0};
        }

        graphics::TextureHandle GetRenderTargetDepthAttachment(
            graphics::RenderTargetHandle target) override
        {
            return {target.id, 0};
        }

        graphics::TextureHandle GetRenderTargetSampledDepthAttachment(
            graphics::RenderTargetHandle target) override
        {
            return {target.id, 0};
        }

        graphics::RenderTargetView GetRenderTargetView(
            graphics::RenderTargetHandle target) override
        {
            return {320, 200, static_cast<uintptr_t>(target.id), 1};
        }

        graphics::IRenderTargetReadback *GetRenderTargetReadback() override
        {
            return &readback_;
        }

        graphics::DescriptorSetHandle CreateResourceBindingSet(
            graphics::PipelineHandle, const graphics::ResourceBindingSetDesc &desc) override
        {
            std::array<uint32_t, 4> environment_texture_ids{
                KPENGINE_NULL_HANDLE, KPENGINE_NULL_HANDLE,
                KPENGINE_NULL_HANDLE, KPENGINE_NULL_HANDLE};
            for (const graphics::ResourceBinding &binding : desc.bindings)
            {
                const auto *const sampled = std::get_if<graphics::SampledTextureBinding>(&binding);
                if (sampled == nullptr || sampled->binding < 7 || sampled->binding > 10)
                {
                    continue;
                }
                environment_texture_ids[sampled->binding - 7] = sampled->texture.id;
            }
            if (std::all_of(environment_texture_ids.begin(), environment_texture_ids.end(),
                            [](uint32_t id) { return id != KPENGINE_NULL_HANDLE; }))
            {
                probe_->environment_binding_snapshots.push_back(environment_texture_ids);
            }
            return MakeHandle<graphics::DescriptorSetHandle>();
        }

        bool DestroyResourceBindingSet(graphics::DescriptorSetHandle) override { return true; }

        void BindResourceBindingSet(graphics::PipelineHandle,
                                    graphics::DescriptorSetHandle) override
        {
        }

        void BeginFrame() override { probe_->events.push_back("begin_frame"); }

        graphics::CommandRecorder *GetCommandRecorder() override { return &recorder_; }

        void EndFrame() override { probe_->events.push_back("end_frame"); }

        GraphicsContext GetGraphicsContext() override
        {
            return {GraphicsAPIType::GRAPHICS_API_OPENGL, nullptr};
        }

        graphics::BufferHandle CreateUniformBuffer(uint32_t) override
        {
            const graphics::BufferHandle handle = MakeHandle<graphics::BufferHandle>();
            uniform_buffers_[handle.id] = {};
            return handle;
        }

        void *MapUniformBuffer(graphics::BufferHandle handle, size_t size) override
        {
            if (probe_->fail_uniform_mapping)
            {
                return nullptr;
            }
            auto &storage = uniform_buffers_[handle.id];
            storage.resize(size);
            return storage.data();
        }

        uint32_t GetCurrentFrameIndex() const override { return 0; }
        uint32_t GetFramesInFlight() const override { return 1; }
        size_t GetUniformBufferAlignment() const override { return 16; }
        graphics::Extent2D GetRenderExtent() const override { return {320, 200}; }

        void WaitIdle() override
        {
            ++probe_->wait_idle_count;
            probe_->events.push_back("wait_idle");
        }

        void Cleanup() override
        {
            ++probe_->cleanup_count;
            probe_->events.push_back("backend_cleanup");
        }

        graphics::BufferHandle CreateVertexBuffer(const void *, size_t) override
        {
            return MakeHandle<graphics::BufferHandle>();
        }

        graphics::BufferHandle CreateIndexBuffer(const void *, size_t) override
        {
            return MakeHandle<graphics::BufferHandle>();
        }

        bool DestroyBufferResource(graphics::BufferHandle) override { return true; }

        void RecordBeginTarget(graphics::RenderTargetHandle target)
        {
            const auto it = target_names_.find(target.id);
            probe_->events.push_back(it == target_names_.end() ? "unknown_target"
                                                               : "target:" + it->second);
        }

        void RecordEndTarget() { probe_->events.push_back("end_target"); }

    private:
        template <typename HandleT>
        HandleT MakeHandle()
        {
            return {next_handle_++, 0};
        }

        std::shared_ptr<BackendProbe> probe_;
        FakeReadback readback_;
        FakeCommandRecorder recorder_;
        uint32_t next_handle_ = 1;
        std::unordered_map<uint32_t, std::string> target_names_;
        std::unordered_map<uint32_t, std::vector<uint8_t>> uniform_buffers_;
    };

    bool FakeCommandRecorder::BeginRenderTarget(graphics::RenderTargetHandle target)
    {
        backend_.RecordBeginTarget(target);
        return true;
    }

    void FakeCommandRecorder::EndRenderTarget() { backend_.RecordEndTarget(); }

    struct InitFixtures
    {
        EventDispatcher<ResizeEvent> resize_dispatcher;
        async::AsyncQueue<asset::AssetLoadRequest> load_queue;
        int native_window_token = 0;

        render::RenderSystemInitInfo Info(const render::RenderBackendFactory &factory)
        {
            render::RenderSystemInitInfo info{};
            info.api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
            info.native_window = &native_window_token;
            info.resize_dispatcher = &resize_dispatcher;
            info.load_queue = &load_queue;
            info.backend_factory = factory;
            return info;
        }
    };

    asset::AssetID RegisterEnvironmentTexture(const char *path, TextureFormat format,
                                              bool malformed = false)
    {
        auto texture_resource = std::make_shared<asset::TextureResource>();
        texture_resource->data->width = 4;
        texture_resource->data->height = 2;
        texture_resource->data->format = format;
        texture_resource->data->pixels.resize(
            malformed ? 1u : 4u * 2u * 4u * sizeof(uint16_t), 0);

        asset::AssetRegisterInfo texture_info{};
        texture_info.resource = texture_resource;
        texture_info.path = path;
        texture_info.name = path;
        texture_info.type = asset::AssetType::KPAT_Texture;
        return asset::AssetManager::GetInstance().RegisterAsset(texture_info);
    }
}

TEST(RenderSystemLifecycleTest, RejectsInvalidStateAndMakesShutdownIdempotent)
{
    render::RenderSystem system;
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Uninitialized);
    EXPECT_FALSE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_FALSE(system.EndFrame());
    EXPECT_FALSE(system.ExecuteEditorCompositePass([] {}));

    system.Shutdown();
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::ShutDown);
    system.Shutdown();
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::ShutDown);
}

TEST(RenderSystemLifecycleTest, RollsBackWhenARequiredCollaboratorFails)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_uniform_mapping = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    const render::RenderSystemInitResult result = system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); }));

    EXPECT_FALSE(result);
    EXPECT_NE(result.diagnostic.find("frame uniform allocator"), std::string::npos);
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Uninitialized);
    EXPECT_EQ(probe->cleanup_count, 1);
    EXPECT_NE(std::find(probe->events.begin(), probe->events.end(), "backend_cleanup"),
              probe->events.end());
}

TEST(RenderSystemLifecycleTest, RendererTargetInitializationRollsBackPartialOwnership)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_render_target_after = 3;
    InitFixtures fixtures;
    render::RenderSystem system;
    const render::RenderSystemInitResult result = system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); }));

    EXPECT_FALSE(result);
    EXPECT_EQ(probe->render_target_destroy_count, 3);
    EXPECT_EQ(probe->cleanup_count, 1);
    const auto destroy_it =
        std::find(probe->events.begin(), probe->events.end(), "destroy_target");
    const auto cleanup_it =
        std::find(probe->events.begin(), probe->events.end(), "backend_cleanup");
    ASSERT_NE(destroy_it, probe->events.end());
    ASSERT_NE(cleanup_it, probe->events.end());
    EXPECT_LT(destroy_it, cleanup_it);
}

TEST(RenderSystemLifecycleTest, FullscreenMeshSurvivesSamplerRetryFailure)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_sampler_creation = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                       { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    system.Shutdown();

    // The normal frame asks for the fullscreen pair from both deferred
    // lighting and tone mapping. A failed sampler must not recreate the mesh.
    EXPECT_EQ(probe->mesh_create_count, 1);
    EXPECT_EQ(probe->mesh_destroy_count, 1);
    EXPECT_EQ(probe->sampler_destroy_count, 0);
}

TEST(RenderSystemLifecycleTest, FullscreenSamplerSurvivesMeshRetryFailure)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_mesh_creation = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                       { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    system.Shutdown();

    // The mesh failure is retried by both consumers, but the successful
    // sampler remains the renderer-owned resource for cleanup.
    EXPECT_EQ(probe->sampler_create_count, 1);
    EXPECT_EQ(probe->sampler_destroy_count, 1);
}

TEST(DeferredRendererTest, OwnsTargetLifetimeAndCleanupIsIdempotent)
{
    const auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    resource::ResourcePipeline resource_pipeline;
    resource_pipeline.Initialize({GraphicsAPIType::GRAPHICS_API_OPENGL});
    render::MaterialSystem materials;
    render::RenderResourceResolver resolver(backend, resource_pipeline);
    render::DeferredRenderer renderer;

    ASSERT_TRUE(renderer.Initialize({backend, resource_pipeline, resolver, materials}, 320, 200));
    EXPECT_TRUE(renderer.IsPassSequenceValid());
    renderer.Cleanup();
    renderer.Cleanup();

    EXPECT_EQ(probe->render_target_destroy_count,
              static_cast<int>(probe->targets.size()));
    EXPECT_EQ(probe->cleanup_count, 0);
    resolver.Cleanup();
}

TEST(RenderSystemLifecycleTest, CharacterizesFrameCaptureResizeEditorAndTeardownOrder)
{
    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Ready);

    bool capture_completed = false;
    ASSERT_TRUE(system.GetRenderCaptureService()->RequestCapture(
        {render::CaptureView::SceneColor},
        [&capture_completed](render::CaptureResult result)
        { capture_completed = result.IsSuccess(); }));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::FrameActive);
    EXPECT_FALSE(system.BeginFrame(1.0f / 60.0f));

    bool editor_recorded = false;
    EXPECT_TRUE(system.ExecuteEditorCompositePass(
        [&editor_recorded]
        {
            editor_recorded = true;
        }));
    EXPECT_TRUE(editor_recorded);
    EXPECT_FALSE(system.ExecuteEditorCompositePass([] {}));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Ready);
    EXPECT_TRUE(capture_completed);
    EXPECT_EQ(probe->readback_count, 1);

    const std::vector<std::string> expected_targets{
        "target:DirectionalShadow", "target:SpotShadow", "target:PointShadow",
        "target:GBuffer", "target:SceneHdr", "target:SceneColor"};
    std::vector<std::string> actual_targets;
    for (const std::string &event : probe->events)
    {
        if (event.rfind("target:", 0) == 0)
        {
            actual_targets.push_back(event);
        }
    }
    EXPECT_EQ(actual_targets, expected_targets);

    system.RequestSceneRenderTargetExtent(640, 360);
    const int waits_before_resize = probe->wait_idle_count;
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_GT(probe->wait_idle_count, waits_before_resize);
    ASSERT_GE(probe->targets.size(), 14u);
    EXPECT_EQ(probe->targets[7].name, "SceneColor");
    EXPECT_EQ(probe->targets[7].width, 640u);
    EXPECT_EQ(probe->targets[7].height, 360u);

    system.Shutdown();
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::ShutDown);
    EXPECT_EQ(probe->cleanup_count, 1);
    const auto wait_it = std::find(probe->events.begin(), probe->events.end(), "wait_idle");
    const auto cleanup_it =
        std::find(probe->events.begin(), probe->events.end(), "backend_cleanup");
    ASSERT_NE(wait_it, probe->events.end());
    ASSERT_NE(cleanup_it, probe->events.end());
    EXPECT_LT(wait_it, cleanup_it);
    EXPECT_EQ(probe->render_target_destroy_count,
              static_cast<int>(probe->targets.size()));
    const auto last_destroy_it =
        std::find(probe->events.rbegin(), probe->events.rend(), "destroy_target");
    ASSERT_NE(last_destroy_it, probe->events.rend());
    EXPECT_LT(last_destroy_it.base() - 1, cleanup_it);

    EXPECT_EQ(probe->pipeline_create_count, probe->pipeline_destroy_count);
    EXPECT_EQ(probe->mesh_create_count, probe->mesh_destroy_count);
    EXPECT_EQ(probe->sampler_create_count, probe->sampler_destroy_count);
    for (const char *event_name : {"destroy_pipeline", "destroy_mesh", "destroy_sampler"})
    {
        const auto last_destroy =
            std::find(probe->events.rbegin(), probe->events.rend(), event_name);
        ASSERT_NE(last_destroy, probe->events.rend()) << event_name;
        EXPECT_LT(last_destroy.base() - 1, cleanup_it) << event_name;
    }
}

TEST(RenderSystemEnvironmentTest, ResolvesReadyAssetIDsAndReusesDerivedBindings)
{
    auto texture_resource = std::make_shared<asset::TextureResource>();
    texture_resource->data->width = 4;
    texture_resource->data->height = 2;
    texture_resource->data->format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
    texture_resource->data->pixels.resize(4 * 2 * 4 * sizeof(uint16_t), 0);

    asset::AssetRegisterInfo texture_info{};
    texture_info.resource = texture_resource;
    texture_info.path = "render_system_environment_test.texture";
    texture_info.name = "RenderSystemEnvironmentTest";
    texture_info.type = asset::AssetType::KPAT_Texture;
    asset::AssetManager &asset_manager = asset::AssetManager::GetInstance();
    const asset::AssetID texture_id = asset_manager.RegisterAsset(texture_info);
    ASSERT_TRUE(texture_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    const render::EnvironmentSourceDesc first_source{texture_id, 1.0f};
    const auto first = system.GetEnvironmentSourceSink()->EnqueueCreate(first_source);
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    const int first_frame_texture_creates = probe->texture_create_count;
    EXPECT_GT(first_frame_texture_creates, 0);

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(first));
    const render::EnvironmentSourceDesc second_source{texture_id, 2.0f};
    const auto second = system.GetEnvironmentSourceSink()->EnqueueCreate(second_source);
    ASSERT_TRUE(second.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->texture_create_count, first_frame_texture_creates);

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(second));
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    system.Shutdown();
    asset_manager.UnRegisterAsset(texture_id);
}

TEST(RenderSystemEnvironmentTest, RetainsBaselineAcrossTypedResolutionFailures)
{
    asset::AssetManager &asset_manager = asset::AssetManager::GetInstance();
    const asset::AssetID wrong_format_id = RegisterEnvironmentTexture(
        "render_system_environment_wrong_format.texture",
        TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB);
    const asset::AssetID malformed_id = RegisterEnvironmentTexture(
        "render_system_environment_malformed.texture",
        TextureFormat::TEXTURE_FORMAT_RGBA16F, true);
    const asset::AssetID valid_id = RegisterEnvironmentTexture(
        "render_system_environment_backend_failure.texture",
        TextureFormat::TEXTURE_FORMAT_RGBA16F);
    ASSERT_TRUE(wrong_format_id.IsValid());
    ASSERT_TRUE(malformed_id.IsValid());
    ASSERT_TRUE(valid_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    ASSERT_FALSE(probe->environment_binding_snapshots.empty());
    const auto baseline = probe->environment_binding_snapshots.back();

    const auto wrong_format = system.GetEnvironmentSourceSink()->EnqueueCreate(
        {wrong_format_id, 1.0f});
    ASSERT_TRUE(wrong_format.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);

    const std::vector<kpengine::program::LogEntry> logs_before_repeat =
        kpengine::LogSystem{}.GetLogSnapshot();
    const std::size_t unresolved_diagnostics = std::count_if(
        logs_before_repeat.begin(), logs_before_repeat.end(),
        [](const kpengine::program::LogEntry &entry)
        {
            return entry.name == "RenderLog" &&
                   entry.message ==
                       "Level environment source could not be resolved; retaining baseline";
        });
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    const std::vector<kpengine::program::LogEntry> logs_after_repeat =
        kpengine::LogSystem{}.GetLogSnapshot();
    const std::size_t repeated_unresolved_diagnostics = std::count_if(
        logs_after_repeat.begin(), logs_after_repeat.end(),
        [](const kpengine::program::LogEntry &entry)
        {
            return entry.name == "RenderLog" &&
                   entry.message ==
                       "Level environment source could not be resolved; retaining baseline";
        });
    EXPECT_EQ(repeated_unresolved_diagnostics, unresolved_diagnostics);

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(wrong_format));
    const auto malformed = system.GetEnvironmentSourceSink()->EnqueueCreate(
        {malformed_id, 1.0f});
    ASSERT_TRUE(malformed.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(malformed));
    probe->fail_texture_creation = true;
    const auto backend_failure = system.GetEnvironmentSourceSink()->EnqueueCreate(
        {valid_id, 1.0f});
    ASSERT_TRUE(backend_failure.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);
    EXPECT_GT(probe->texture_create_count, 0);

    system.Shutdown();
    asset_manager.UnRegisterAsset(wrong_format_id);
    asset_manager.UnRegisterAsset(malformed_id);
    asset_manager.UnRegisterAsset(valid_id);
}

TEST(RenderSystemEnvironmentTest, KeepsBaselineWhenLevelSourceFails)
{
    asset::AssetManager &asset_manager = asset::AssetManager::GetInstance();
    const asset::AssetID invalid_id = RegisterEnvironmentTexture(
        "render_system_environment_bootstrap_invalid.texture",
        TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB);
    ASSERT_TRUE(invalid_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType) { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.PostInitialize());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    ASSERT_FALSE(probe->environment_binding_snapshots.empty());
    const auto baseline = probe->environment_binding_snapshots.back();

    const auto invalid = system.GetEnvironmentSourceSink()->EnqueueCreate({invalid_id, 1.0f});
    ASSERT_TRUE(invalid.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);

    system.Shutdown();
    asset_manager.UnRegisterAsset(invalid_id);
}

TEST(RenderSystemEnvironmentTest, PublishesEnvironmentBindingsAtomicallyAndRestoresBaseline)
{
    asset::AssetManager &asset_manager = asset::AssetManager::GetInstance();
    const asset::AssetID texture_id = RegisterEnvironmentTexture(
        "render_system_environment_atomic.texture", TextureFormat::TEXTURE_FORMAT_RGBA16F);
    ASSERT_TRUE(texture_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    ASSERT_FALSE(probe->environment_binding_snapshots.empty());
    const auto baseline = probe->environment_binding_snapshots.back();

    const auto source = system.GetEnvironmentSourceSink()->EnqueueCreate({texture_id, 2.0f});
    ASSERT_TRUE(source.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    ASSERT_FALSE(probe->environment_binding_snapshots.empty());
    const auto resolved = probe->environment_binding_snapshots.back();
    EXPECT_NE(resolved, baseline);
    EXPECT_TRUE(std::all_of(resolved.begin(), resolved.end(),
                            [](uint32_t id) { return id != KPENGINE_NULL_HANDLE; }));

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(source));
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);

    system.Shutdown();
    asset_manager.UnRegisterAsset(texture_id);
}

TEST(RenderSystemEnvironmentTest, ClearsEnvironmentSourceHandlesDuringShutdown)
{
    const asset::AssetID texture_id = RegisterEnvironmentTexture(
        "render_system_environment_shutdown.texture", TextureFormat::TEXTURE_FORMAT_RGBA16F);
    ASSERT_TRUE(texture_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    const auto handle =
        system.GetEnvironmentSourceSink()->EnqueueCreate({texture_id, 1.0f});
    ASSERT_TRUE(handle.IsValid());
    system.Shutdown();

    EXPECT_FALSE(system.GetEnvironmentSourceSink()->EnqueueDestroy(handle));
    const auto replacement =
        system.GetEnvironmentSourceSink()->EnqueueCreate({texture_id, 1.0f});
    ASSERT_TRUE(replacement.IsValid());
    EXPECT_FALSE(replacement == handle);
    EXPECT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(replacement));
    asset::AssetManager::GetInstance().UnRegisterAsset(texture_id);
}
