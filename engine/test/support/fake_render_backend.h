#ifndef KPENGINE_TEST_SUPPORT_FAKE_RENDER_BACKEND_H
#define KPENGINE_TEST_SUPPORT_FAKE_RENDER_BACKEND_H

// Shared graphics backend test double.
//
// Extracted from render_system_test.cpp once the Live2D renderer needed to
// drive Live2DRenderer::Initialize -- and therefore ResizeOutput and the
// shutdown handle release -- without a GPU. It is shared deliberately rather
// than copied: the probe counters are the assertion surface for both callers,
// and two copies would drift. The double mints valid handles for every
// resource a renderer creates and records what happened in BackendProbe, so a
// test asserts on the fake's ledger instead of on a rendered image.

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/texture.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"

namespace kpengine::test
{
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
        bool missing_command_recorder = false;
        std::vector<std::string> events;
        std::vector<TargetRecord> targets;
        // Extent this double reports. Defaults to the value the render tests
        // were written against; a caller that drives extent-sensitive code sets
        // it explicitly instead of relying on the default.
        graphics::Extent2D render_extent{320u, 200u};
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
        int descriptor_set_create_count = 0;
        std::vector<std::array<uint32_t, 4>> environment_binding_snapshots;
        // Viewports the recorder was given, in submission order. A caller that
        // resizes its output asserts the last entry to prove the frame it
        // recorded actually used the new extent, rather than only that the
        // extent was changed somewhere.
        std::vector<graphics::Viewport> viewports;
        int draw_count = 0;
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
        bool BeginPresentation(const std::array<float, 4> *) override { return true; }
        void EndRenderTarget() override;
        bool BindPipeline(graphics::PipelineHandle) override { return true; }
        void BindMesh(graphics::MeshHandle) override {}
        bool BindGeometry(const graphics::GeometryView &) override { return true; }
        bool BindResourceBindings(graphics::PipelineHandle,
                                  graphics::DescriptorSetHandle,
                                  const graphics::DynamicUniformOffsets &) override
        {
            return true;
        }
        void SetViewport(const graphics::Viewport &viewport) override;
        void SetScissor(const graphics::Scissor &) override {}
        // Defined out of line with the other methods that touch FakeBackend,
        // which is still incomplete here.
        void DrawIndexed(uint32_t = 0, uint32_t = 1, uint32_t = 0, int32_t = 0,
                         uint32_t = 0) override;

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
            target_extents_[handle.id] = {desc.width, desc.height};
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
            const auto extent = target_extents_.at(target.id);
            return {extent.first, extent.second, static_cast<uintptr_t>(target.id),
                    static_cast<uintptr_t>(target.id)};
        }

        graphics::IRenderTargetReadback *GetRenderTargetReadback() override
        {
            return &readback_;
        }

        graphics::DescriptorSetHandle CreateResourceBindingSet(
            graphics::PipelineHandle, const graphics::ResourceBindingSetDesc &desc) override
        {
            ++probe_->descriptor_set_create_count;
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

        graphics::CommandRecorder *GetCommandRecorder() override
        {
            return probe_->missing_command_recorder ? nullptr : &recorder_;
        }

        void EndFrame() override { probe_->events.push_back("end_frame"); }

        GraphicsAPIType GetGraphicsAPI() const override
        {
            return GraphicsAPIType::GRAPHICS_API_OPENGL;
        }

        graphics::IEditorPresentationBridge *GetEditorPresentationBridge() override
        {
            return nullptr;
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

        graphics::BufferHandle CreateBuffer(const graphics::BufferDesc &, const void *,
                                            size_t) override
        {
            return MakeHandle<graphics::BufferHandle>();
        }

        bool WriteFrameBuffer(graphics::BufferHandle, size_t, const void *, size_t) override
        {
            return true;
        }

        uint32_t GetCurrentFrameIndex() const override { return 0; }
        uint32_t GetFramesInFlight() const override { return 1; }
        size_t GetUniformBufferAlignment() const override { return 16; }
        graphics::Extent2D GetRenderExtent() const override { return probe_->render_extent; }

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

        void RecordViewport(const graphics::Viewport &viewport)
        {
            probe_->viewports.push_back(viewport);
        }

        void RecordDraw() { ++probe_->draw_count; }

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
        std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> target_extents_;
        std::unordered_map<uint32_t, std::vector<uint8_t>> uniform_buffers_;
    };

    inline bool FakeCommandRecorder::BeginRenderTarget(graphics::RenderTargetHandle target)
    {
        backend_.RecordBeginTarget(target);
        return true;
    }

    inline void FakeCommandRecorder::EndRenderTarget() { backend_.RecordEndTarget(); }

    inline void FakeCommandRecorder::SetViewport(const graphics::Viewport &viewport)
    {
        backend_.RecordViewport(viewport);
    }

    inline void FakeCommandRecorder::DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t,
                                                 uint32_t)
    {
        backend_.RecordDraw();
    }
}

#endif // KPENGINE_TEST_SUPPORT_FAKE_RENDER_BACKEND_H
