#ifndef KPENGINE_RUNTIME_RENDER_RENDER_GRAPH_EXECUTOR_H
#define KPENGINE_RUNTIME_RENDER_RENDER_GRAPH_EXECUTOR_H

#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "render_graph_bindings.h"
#include "render_graph_frame.h"
#include "graphics/backend/common/render_backend.h"

namespace kpengine::render
{
    enum class RenderGraphPassDisposition : uint8_t
    {
        Record,
        ReusePreviousOutput,
        Fail
    };

    struct RenderGraphExecutionResult
    {
        bool finalized = false;
        bool required_pass_failed = false;
        bool frame_succeeded = false;
        std::string diagnostic;
        std::vector<std::pair<uint64_t, RenderGraphPassOutcome>> pass_outcomes;

        bool Succeeded() const noexcept
        {
            return frame_succeeded;
        }
    };

    class RenderGraphPassContext
    {
    public:
        RenderGraphPassContext(const CompiledRenderGraph::Pass &pass,
                               const RenderGraphBindings &bindings,
                               graphics::CommandRecorder &recorder) noexcept
            : pass_(pass), bindings_(bindings), recorder_(recorder) {}

        const CompiledRenderGraph::Pass &GetPass() const noexcept { return pass_; }
        graphics::CommandRecorder &GetRecorder() const noexcept { return recorder_; }
        RenderTarget *ResolveTexture(
            RenderFrameResourceRole role,
            RenderGraphAccess access = RenderGraphAccess::Read) const;
        std::span<const graphics::BufferHandle> ResolveBuffers(
            RenderFrameResourceRole role,
            RenderGraphAccess access = RenderGraphAccess::Read) const;
        std::span<const graphics::AccelerationStructureHandle> ResolveAccelerationStructures(
            RenderFrameResourceRole role,
            RenderGraphAccess access = RenderGraphAccess::Read) const;
        RenderTarget *ResolveTexture(
            GraphTextureHandle handle,
            RenderGraphAccess access = RenderGraphAccess::Read) const;
        std::span<const graphics::BufferHandle> ResolveBuffers(
            GraphBufferHandle handle,
            RenderGraphAccess access = RenderGraphAccess::Read) const;
        std::span<const graphics::AccelerationStructureHandle> ResolveAccelerationStructures(
            GraphAccelerationStructureHandle handle,
            RenderGraphAccess access = RenderGraphAccess::Read) const;
        bool IsClean() const noexcept { return violation_.empty(); }
        const std::string &GetViolation() const noexcept { return violation_; }

    private:
        bool Declares(const RenderFrameGraphResourceHandle &handle,
                      RenderGraphAccess access) const noexcept;
        const CompiledRenderGraph::Pass &pass_;
        const RenderGraphBindings &bindings_;
        graphics::CommandRecorder &recorder_;
        mutable std::string violation_;
    };

    // Owns the active graph cursor for exactly one frame. The renderer supplies
    // pass recording callbacks; the compiled graph remains the sole scheduler.
    class RenderGraphExecutor
    {
    public:
        bool BeginFrame(const CompiledRenderGraph &plan);
        bool BeginFrame(const CompiledRenderGraph &plan,
                        const RenderGraphBindings &bindings,
                        graphics::CommandRecorder &recorder);
        bool AcquireTransients(
            const CompiledRenderGraph &plan, graphics::RenderBackend &backend,
            graphics::Extent2D extent,
            const std::function<std::optional<graphics::RenderTargetDesc>(
                uint64_t, graphics::Extent2D)> &describe,
            std::string &error);
        RenderTarget *ResolveTransient(uint64_t key) const noexcept;
        bool IsActive() const noexcept { return frame_.has_value(); }
        bool ExecuteRenderer(
            const std::function<bool(const CompiledRenderGraph::Pass &)> &record_pass);
        bool ExecuteRenderer(
            const std::function<RenderGraphPassDisposition(
                const CompiledRenderGraph::Pass &)> &disposition,
            const std::function<bool(const RenderGraphPassContext &)> &record_pass,
            const std::function<void(uint64_t)> &begin_profile = {},
            const std::function<void(uint64_t)> &end_profile = {});
        bool CanExecuteExternal() const noexcept;
        const std::string &GetDiagnostic() const noexcept { return diagnostic_; }
        bool ExecuteExternal(const std::function<void()> &record_pass);
        bool ExecuteExternal(const std::function<void()> &record_pass,
                             const std::function<void()> &begin_profile,
                             const std::function<void()> &end_profile);
        RenderGraphExecutionResult Finalize();
        bool Finalize(std::string &error);
        bool HasRequiredFailure() const noexcept;
        RenderGraphPassOutcome GetOutcome(uint64_t pass_key) const noexcept;
        void Abort() noexcept;

    private:
        std::optional<RenderGraphFrame> frame_;
        const CompiledRenderGraph *plan_ = nullptr;
        const RenderGraphBindings *bindings_ = nullptr;
        graphics::CommandRecorder *recorder_ = nullptr;
        bool external_transition_failed_ = false;
        std::string diagnostic_;
        struct TransientLease
        {
            uint64_t key = 0;
            graphics::RenderTargetHandle handle;
            std::unique_ptr<RenderTarget> target;
        };
        graphics::RenderBackend *transient_backend_ = nullptr;
        std::vector<TransientLease> transient_leases_;
        void ReleaseTransients() noexcept;
    };
}

#endif
