#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "async/async_queue.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
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
        std::vector<std::string> events;
        std::vector<TargetRecord> targets;
        int wait_idle_count = 0;
        int cleanup_count = 0;
        int readback_count = 0;
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
            return MakeHandle<graphics::PipelineHandle>();
        }

        bool DestroyPipelineResource(graphics::PipelineHandle) override { return true; }

        graphics::MeshHandle CreateMesh(const data::MeshData &) override
        {
            return MakeHandle<graphics::MeshHandle>();
        }

        bool DestroyMesh(graphics::MeshHandle) override { return true; }

        graphics::TextureHandle CreateTexture(const data::TextureData &,
                                              const graphics::TextureSettings &) override
        {
            return MakeHandle<graphics::TextureHandle>();
        }

        bool DestroyTexture(graphics::TextureHandle) override { return true; }

        graphics::SamplerHandle CreateSampler(const graphics::SamplerSettings &) override
        {
            return MakeHandle<graphics::SamplerHandle>();
        }

        bool DestroySampler(graphics::SamplerHandle) override { return true; }

        graphics::RenderTargetHandle CreateRenderTarget(
            const graphics::RenderTargetDesc &desc) override
        {
            if (probe_->fail_render_target)
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

        bool DestroyRenderTarget(graphics::RenderTargetHandle) override { return true; }

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
            graphics::PipelineHandle, const graphics::ResourceBindingSetDesc &) override
        {
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
}
