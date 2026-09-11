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

#include "asset/asset_manager.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "data/texture.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "log/log_system.h"
#include "resource/resource_pipeline.h"
#include "render/deferred_renderer.h"
#include "render/render_resource_resolver.h"
#include "render/render_system.h"
#include "render/render_submission_executor.h"
#include "render/prepared_render_asset_catalog.h"

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
        bool missing_command_recorder = false;
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
        int descriptor_set_create_count = 0;
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
        std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> target_extents_;
        std::unordered_map<uint32_t, std::vector<uint8_t>> uniform_buffers_;
    };

    bool FakeCommandRecorder::BeginRenderTarget(graphics::RenderTargetHandle target)
    {
        backend_.RecordBeginTarget(target);
        return true;
    }

    void FakeCommandRecorder::EndRenderTarget() { backend_.RecordEndTarget(); }

    TEST(RenderSubmissionExecutorTest, PreparesAndRecordsGenericWorkInOrder)
    {
        const auto probe = std::make_shared<BackendProbe>();
        FakeBackend backend(probe);
        backend.Initialize({});

        render::FrameContext frame;
        frame.Initialize(backend, 256u);
        frame.Begin(0u, {1u, 0.0f, 1.0f / 60.0f}, {64u, 64u});

        graphics::BufferDesc buffer_desc{};
        buffer_desc.role = graphics::BufferRole::Vertex;
        buffer_desc.update_mode = graphics::BufferUpdateMode::PerFrame;
        buffer_desc.capacity_bytes = 64u;
        const graphics::BufferHandle position_buffer =
            backend.CreateBuffer(buffer_desc, nullptr, 0u);
        const graphics::RenderTargetDesc target_desc{
            64u, 64u, 1u, {graphics::RenderTargetColorAttachment{}}, std::nullopt};
        const graphics::RenderTargetHandle target = backend.CreateRenderTarget(target_desc);

        render::RenderSubmission submission{};
        submission.buffer_writes.push_back({position_buffer, 0u, {std::byte{7}}});
        render::SubmissionPass pass{};
        pass.target = target;
        render::SubmissionDraw draw{};
        draw.pipeline = {20u, 0u};
        draw.geometry.vertices = {{0u, position_buffer, 0u}};
        draw.geometry.indices = {{21u, 0u}, 0u, graphics::IndexElementType::UInt16};
        draw.viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
        draw.index_count = 3u;
        draw.uniforms.push_back({0u, 0u, {std::byte{1}, std::byte{2}}});
        draw.textures.push_back({0u, 1u, {22u, 0u}, {23u, 0u}});
        pass.draws.push_back(std::move(draw));
        submission.passes.push_back(std::move(pass));

        const render::RenderSubmissionExecutionResult result =
            render::RenderSubmissionExecutor::Execute(
                submission, frame, *backend.GetCommandRecorder());
        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        EXPECT_EQ(result.pass_count, 1u);
        EXPECT_EQ(result.draw_count, 1u);
        EXPECT_EQ(result.upload_bytes, 1u);
        EXPECT_EQ(result.uniform_bytes, 2u);
        EXPECT_EQ(result.binding_count, 1u);
        EXPECT_EQ(probe->events.back(), "end_target");

        frame.End();
        frame.Cleanup();
        backend.DestroyRenderTarget(target);
        backend.Cleanup();
    }

    std::shared_ptr<const render::PreparedRenderAssetCatalog> BuildPreparedCatalog(
        const std::vector<asset::AssetID> &extra_textures = {})
    {
        render::PreparedRenderAssetCatalogBuild build;
        build.graphics_api = GraphicsAPIType::GRAPHICS_API_OPENGL;
        const asset::AssetID vertex_id{1, 1, asset::AssetType::KPAT_Shader};
        const asset::AssetID fragment_id{2, 1, asset::AssetType::KPAT_Shader};
        const asset::AssetID program_id{3, 1, asset::AssetType::KPAT_ShaderProgram};

        auto make_shader = [](ShaderStage stage)
        {
            auto shader = std::make_shared<asset::ShaderResource>();
            shader->status = asset::ShaderStatus::Ready;
            shader->data = std::make_shared<data::ShaderData>();
            shader->data->stage = stage;
            shader->data->api = GraphicsAPIType::GRAPHICS_API_OPENGL;
            shader->data->source = "void main() {}";
            shader->desc.stage = stage;
            shader->format = ShaderFormat::SHADER_FORMAT_GLSL;
            return shader;
        };
        build.records.push_back({vertex_id, make_shader(ShaderStage::SHADER_STAGE_VERTEX), {}});
        build.records.push_back({fragment_id, make_shader(ShaderStage::SHADER_STAGE_FRAGMENT), {}});
        auto program = std::make_shared<asset::ShaderProgramResource>();
        program->BindData(ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL,
                          vertex_id);
        program->BindData(ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL,
                          fragment_id);
        build.records.push_back({program_id, program, {vertex_id, fragment_id}});

        auto make_texture = [](uint32_t value)
        {
            auto texture = std::make_shared<asset::TextureResource>();
            texture->data->width = 1;
            texture->data->height = 1;
            texture->data->pixels.resize(4, static_cast<uint8_t>(value));
            return texture;
        };
        const asset::AssetID white_id{4, 1, asset::AssetType::KPAT_Texture};
        const asset::AssetID normal_id{5, 1, asset::AssetType::KPAT_Texture};
        build.records.push_back({white_id, make_texture(255), {}});
        build.records.push_back({normal_id, make_texture(128), {}});
        for (const asset::AssetID extra : extra_textures)
        {
            const auto texture = asset::AssetManager::GetInstance().GetResource<asset::TextureResource>(extra);
            if (texture)
            {
                build.records.push_back({extra, texture, {}});
                if (texture->data && texture->data->format == TextureFormat::TEXTURE_FORMAT_RGBA16F &&
                    texture->data->pixels.size() >= 64)
                {
                    resource::EnvironmentIblData ibl{};
                    ibl.irradiance.width = 1;
                    ibl.irradiance.height = 1;
                    ibl.irradiance.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
                    ibl.irradiance.pixels.resize(8, 0);
                    ibl.prefiltered_radiance.width = 1;
                    ibl.prefiltered_radiance.height = 1;
                    ibl.prefiltered_radiance.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
                    ibl.prefiltered_radiance.pixels.resize(8, 0);
                    ibl.brdf_lut.width = 1;
                    ibl.brdf_lut.height = 1;
                    ibl.brdf_lut.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
                    ibl.brdf_lut.pixels.resize(8, 0);
                    ibl.prefilter_level_count = 1;
                    build.environment_ibl.push_back({extra, std::move(ibl)});
                }
            }
        }
        for (const auto &requirement : render::GetBuiltInRenderAssetRequirements())
        {
            build.built_ins[static_cast<size_t>(requirement.role)] =
                requirement.expected_type == asset::AssetType::KPAT_Texture
                    ? (requirement.role == render::BuiltInRenderAsset::DefaultWhiteTexture
                           ? white_id
                           : normal_id)
                    : program_id;
        }
        std::string diagnostic;
        auto catalog = render::PreparedRenderAssetCatalog::Create(std::move(build), diagnostic);
        return catalog ? std::make_shared<const render::PreparedRenderAssetCatalog>(std::move(*catalog))
                       : nullptr;
    }

    struct InitFixtures
    {
        EventDispatcher<ResizeEvent> resize_dispatcher;
        int native_window_token = 0;
        std::vector<asset::AssetID> extra_textures;

        render::RenderSystemInitInfo Info(const render::RenderBackendFactory &factory)
        {
            render::RenderSystemInitInfo info{};
            info.api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
            info.native_window = &native_window_token;
            info.resize_dispatcher = &resize_dispatcher;
            info.prepared_assets = BuildPreparedCatalog(extra_textures);
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

    render::RenderProfileSnapshot RecordDirectionalShadowCacheFrame(
        render::DeferredRenderer &renderer, graphics::RenderBackend &backend,
        render::FrameContext &frame_context, const render::RenderWorld &render_world,
        uint64_t frame_number, const Vector3f &camera_position,
        const Vector3f &light_direction)
    {
        render::RenderSceneFrameInput input{render_world};
        input.camera.SetPosition(camera_position);
        render::Light light{};
        light.handle = {42, 1};
        light.desc.type = render::LightType::Directional;
        light.desc.shadow = render::ShadowHandle{7, 1};
        light.desc.type_data = render::DirectionalLightData{light_direction};
        input.lights.push_back(light);
        input.is_shadow_handle_valid = [](render::ShadowHandle) { return true; };

        backend.BeginFrame();
        frame_context.Begin(backend.GetCurrentFrameIndex(),
                            {frame_number, 0.0f, 1.0f / 60.0f},
                            backend.GetRenderExtent());
        renderer.RecordFrame(frame_context, input);
        EXPECT_TRUE(renderer.ExecuteEditorCompositePass([] {}));
        EXPECT_TRUE(renderer.FinalizeFrame());
        frame_context.End();
        backend.EndFrame();
        return renderer.GetProfileSnapshot();
    }
}

TEST(FrameContextTest, ReusesStableBindingSetAndPublishesDynamicOffsets)
{
    auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::FrameContext frame;
    frame.Initialize(backend, 1024);
    frame.Begin(0, {1, 0.0f, 1.0f / 60.0f}, {320, 200});

    const graphics::PipelineHandle pipeline{1, 0};
    const render::UniformAllocation per_pass =
        frame.UpdateStableUniform(1, uint32_t{11});
    const render::UniformAllocation per_object =
        frame.UpdateStableUniform(2, uint32_t{22});
    ASSERT_TRUE(per_pass.IsValid());
    ASSERT_TRUE(per_object.IsValid());
    const std::vector<graphics::ResourceBinding> bindings{
        graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
        graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range}};

    const render::FrameResourceBinding first =
        frame.CreateOrGetStableBindingSet(3, pipeline, bindings);
    const render::FrameResourceBinding second =
        frame.CreateOrGetStableBindingSet(3, pipeline, bindings);
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());
    EXPECT_EQ(first.descriptor_set, second.descriptor_set);
    EXPECT_EQ(first.dynamic_offsets, second.dynamic_offsets);
    EXPECT_EQ(first.dynamic_offsets.size(), 2u);
    EXPECT_EQ(probe->descriptor_set_create_count, 1);

    frame.Cleanup();
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

TEST(RenderSystemLifecycleTest, SeparatesPresentationInitializationFromScenePromotion)
{
    auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    const render::RenderSystemInitInfo info = fixtures.Info(
        [probe](GraphicsAPIType) { return std::make_unique<FakeBackend>(probe); });

    ASSERT_TRUE(system.InitializePresentation(info));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::PresentationReady);
    EXPECT_FALSE(system.GetSceneRenderTargetView().IsValid());
    EXPECT_EQ(system.GetMetrics().prepared_shader_count, 0U);
    EXPECT_EQ(system.GetMetrics().triangle_count, 0U);
    EXPECT_FALSE(system.GetMetrics().gpu_usage_percent.has_value());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::PresentationReady);

    ASSERT_TRUE(system.PromoteToScene(info.prepared_assets));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Ready);
    EXPECT_TRUE(system.GetSceneRenderTargetView().IsValid());
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, SelectsViewportDebugTargetAtFrameBoundary)
{
    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    const graphics::RenderTargetView scene_view = system.GetDebugRenderTargetView();
    ASSERT_TRUE(scene_view.IsValid());
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::SceneColor);

    system.SetDebugView(render::CaptureView::WorldNormal);
    // The request is intentionally deferred until BeginFrame, so the view
    // borrowed by the current editor composite remains frame-consistent.
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::SceneColor);
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    const graphics::RenderTargetView debug_view = system.GetDebugRenderTargetView();
    ASSERT_TRUE(debug_view.IsValid());
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::WorldNormal);
    EXPECT_EQ(system.GetSceneRenderTargetView().native_image_view, scene_view.native_image_view);
    EXPECT_NE(debug_view.native_image_view, scene_view.native_image_view);
    ASSERT_TRUE(system.EndFrame());

    system.SetDebugView(render::CaptureView::EngineWindow);
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::WorldNormal);
    ASSERT_TRUE(system.EndFrame());
    system.Shutdown();
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

TEST(RenderSystemLifecycleTest, KeepsSourceSinkAddressesStableAcrossRetry)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_uniform_mapping = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    auto *const renderable_sink = system.GetRenderableSourceSink();
    auto *const light_sink = system.GetLightSourceSink();
    auto *const camera_sink = system.GetCameraSourceSink();
    auto *const environment_sink = system.GetEnvironmentSourceSink();

    EXPECT_FALSE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));
    EXPECT_EQ(system.GetRenderableSourceSink(), renderable_sink);
    EXPECT_EQ(system.GetLightSourceSink(), light_sink);
    EXPECT_EQ(system.GetCameraSourceSink(), camera_sink);
    EXPECT_EQ(system.GetEnvironmentSourceSink(), environment_sink);

    probe->fail_uniform_mapping = false;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));
    EXPECT_EQ(system.GetRenderableSourceSink(), renderable_sink);
    EXPECT_EQ(system.GetLightSourceSink(), light_sink);
    EXPECT_EQ(system.GetCameraSourceSink(), camera_sink);
    EXPECT_EQ(system.GetEnvironmentSourceSink(), environment_sink);
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, UnwindsBackendFrameWhenNoRecorderIsAvailable)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->missing_command_recorder = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    EXPECT_FALSE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Ready);
    EXPECT_FALSE(system.EndFrame());
    EXPECT_EQ(std::count(probe->events.begin(), probe->events.end(), "begin_frame"), 1);
    EXPECT_EQ(std::count(probe->events.begin(), probe->events.end(), "end_frame"), 1);
    system.Shutdown();
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
    render::MaterialSystem materials;
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::DeferredRenderer renderer;

    ASSERT_TRUE(renderer.Initialize({backend, resolver, materials, *prepared_assets}, 320, 200));
    EXPECT_TRUE(renderer.IsPassSequenceValid());
    renderer.Cleanup();
    renderer.Cleanup();

    EXPECT_EQ(probe->render_target_destroy_count,
              static_cast<int>(probe->targets.size()));
    EXPECT_EQ(probe->cleanup_count, 0);
    resolver.Cleanup();
}

TEST(DeferredRendererTest, ReusesDirectionalShadowWhenCameraStaysInsideEffectiveFit)
{
    const auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::MaterialSystem materials;
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::DeferredRenderer renderer;
    ASSERT_TRUE(renderer.Initialize({backend, resolver, materials, *prepared_assets}, 320, 200));

    render::RenderWorld render_world;
    render::MeshProxyDesc proxy{};
    proxy.world_bounds = {{-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}};
    proxy.flags.visible = true;
    proxy.flags.casts_shadow = true;
    render_world.EnqueueCreate(proxy);
    render_world.ApplyPendingCommands();

    render::FrameContext frame_context;
    frame_context.Initialize(backend, 1024 * 1024);
    const Vector3f light_direction{0.0f, -1.0f, 0.0f};
    const auto first = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 1, {0.0f, 0.0f, 2.0f}, light_direction);
    // The Editor submits the current viewport extent every UI frame. Repeating
    // an unchanged request must not invalidate the shadow cache.
    renderer.RequestExtent(320, 200);
    renderer.ApplyPendingExtent();
    const auto second = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 2, {1.0f, 0.0f, 2.0f}, light_direction);

    EXPECT_EQ(first.shadow_cache_hits, 0U);
    EXPECT_EQ(first.shadow_cache_misses, 1U);
    EXPECT_EQ(second.shadow_cache_hits, 1U);
    EXPECT_EQ(second.shadow_cache_misses, 0U);

    frame_context.Cleanup();
    renderer.Cleanup();
    resolver.Cleanup();
}

TEST(DeferredRendererTest, InvalidatesDirectionalShadowWhenCameraLeavesEffectiveFit)
{
    const auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::MaterialSystem materials;
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::DeferredRenderer renderer;
    ASSERT_TRUE(renderer.Initialize({backend, resolver, materials, *prepared_assets}, 320, 200));

    render::RenderWorld render_world;
    render::MeshProxyDesc proxy{};
    proxy.world_bounds = {{-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}};
    proxy.flags.visible = true;
    proxy.flags.casts_shadow = true;
    render_world.EnqueueCreate(proxy);
    render_world.ApplyPendingCommands();

    render::FrameContext frame_context;
    frame_context.Initialize(backend, 1024 * 1024);
    const Vector3f light_direction{0.0f, -1.0f, 0.0f};
    RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 1, {0.0f, 0.0f, 2.0f}, light_direction);
    const auto moved_outside = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 2, {40.0f, 0.0f, 2.0f}, light_direction);

    EXPECT_EQ(moved_outside.shadow_cache_hits, 0U);
    EXPECT_EQ(moved_outside.shadow_cache_misses, 1U);

    frame_context.Cleanup();
    renderer.Cleanup();
    resolver.Cleanup();
}

TEST(DeferredRendererTest, InvalidatesDirectionalShadowWhenLightOrCasterChanges)
{
    const auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::MaterialSystem materials;
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::DeferredRenderer renderer;
    ASSERT_TRUE(renderer.Initialize({backend, resolver, materials, *prepared_assets}, 320, 200));

    render::RenderWorld render_world;
    render::MeshProxyDesc proxy{};
    proxy.world_bounds = {{-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}};
    proxy.flags.visible = true;
    proxy.flags.casts_shadow = true;
    const render::RenderableHandle proxy_handle = render_world.EnqueueCreate(proxy);
    render_world.ApplyPendingCommands();

    render::FrameContext frame_context;
    frame_context.Initialize(backend, 1024 * 1024);
    const Vector3f initial_direction{0.0f, -1.0f, 0.0f};
    const auto first = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 1, {0.0f, 0.0f, 2.0f}, initial_direction);
    const auto light_changed = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 2, {0.0f, 0.0f, 2.0f},
        {1.0f, -1.0f, 0.0f});

    proxy.world_bounds = {{-20.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}};
    ASSERT_TRUE(render_world.EnqueueUpdate(proxy_handle, proxy));
    render_world.ApplyPendingCommands();
    const auto caster_changed = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 3, {0.0f, 0.0f, 2.0f},
        {1.0f, -1.0f, 0.0f});

    EXPECT_EQ(first.shadow_cache_misses, 1U);
    EXPECT_EQ(light_changed.shadow_cache_hits, 0U);
    EXPECT_EQ(light_changed.shadow_cache_misses, 1U);
    EXPECT_EQ(caster_changed.shadow_cache_hits, 0U);
    EXPECT_EQ(caster_changed.shadow_cache_misses, 1U);

    frame_context.Cleanup();
    renderer.Cleanup();
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

    const graphics::RenderTargetView view_before_resize = system.GetSceneRenderTargetView();
    ASSERT_TRUE(view_before_resize.IsValid());
    system.RequestSceneRenderTargetExtent(640, 360);
    const int waits_before_resize = probe->wait_idle_count;
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_GT(probe->wait_idle_count, waits_before_resize);
    ASSERT_GE(probe->targets.size(), 14u);
    EXPECT_EQ(probe->targets[7].name, "SceneColor");
    EXPECT_EQ(probe->targets[7].width, 640u);
    EXPECT_EQ(probe->targets[7].height, 360u);
    const graphics::RenderTargetView view_after_resize = system.GetSceneRenderTargetView();
    ASSERT_TRUE(view_after_resize.IsValid());
    EXPECT_EQ(view_after_resize.width, 640u);
    EXPECT_EQ(view_after_resize.height, 360u);
    EXPECT_NE(view_before_resize.native_image_view, view_after_resize.native_image_view);
    // The old value is borrowed and must not be reused after the extent change;
    // the Editor reacquires the replacement view on its next draw.

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
    fixtures.extra_textures.push_back(texture_id);
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
    fixtures.extra_textures = {wrong_format_id, malformed_id, valid_id};
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
    fixtures.extra_textures.push_back(texture_id);
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
