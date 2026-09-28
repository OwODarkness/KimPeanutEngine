#include "render_graph_executor.h"

#include <algorithm>

namespace kpengine::render
{
    bool RenderGraphPassContext::Declares(
        const RenderFrameGraphResourceHandle &handle, RenderGraphAccess access) const noexcept
    {
        return std::any_of(pass_.uses.begin(), pass_.uses.end(), [&handle, access](const auto &use) {
            return use.handle == handle && use.access == access;
        });
    }

    RenderTarget *RenderGraphPassContext::ResolveTexture(
        RenderFrameResourceRole role, RenderGraphAccess access) const
    {
        for (const RenderGraphPhysicalResource &resource : bindings_.Resources())
        {
            if (resource.role != role)
                continue;
            if (!Declares(resource.logical, access))
                continue;
            if (const auto *target = std::get_if<RenderTarget *>(&resource.physical))
                return *target;
        }
        violation_ = "Pass requested texture role " +
            std::to_string(static_cast<unsigned>(role)) + " with access " +
            std::to_string(static_cast<unsigned>(access)) +
            " that is not declared or physically bound";
        return nullptr;
    }

    std::span<const graphics::BufferHandle> RenderGraphPassContext::ResolveBuffers(
        RenderFrameResourceRole role, RenderGraphAccess access) const
    {
        for (const RenderGraphPhysicalResource &resource : bindings_.Resources())
        {
            if (resource.role != role)
                continue;
            if (!Declares(resource.logical, access))
                continue;
            if (const auto *buffers =
                    std::get_if<std::vector<graphics::BufferHandle>>(&resource.physical))
                return *buffers;
        }
        violation_ = "Pass requested buffer role " +
            std::to_string(static_cast<unsigned>(role)) + " with access " +
            std::to_string(static_cast<unsigned>(access)) +
            " that is not declared or physically bound";
        return {};
    }

    std::span<const graphics::AccelerationStructureHandle>
    RenderGraphPassContext::ResolveAccelerationStructures(
        RenderFrameResourceRole role, RenderGraphAccess access) const
    {
        for (const RenderGraphPhysicalResource &resource : bindings_.Resources())
        {
            if (resource.role != role)
                continue;
            if (!Declares(resource.logical, access))
                continue;
            if (const auto *structures = std::get_if<
                    std::vector<graphics::AccelerationStructureHandle>>(&resource.physical))
                return *structures;
        }
        violation_ = "Pass requested acceleration-structure role " +
            std::to_string(static_cast<unsigned>(role)) + " with access " +
            std::to_string(static_cast<unsigned>(access)) +
            " that is not declared or physically bound";
        return {};
    }

    RenderTarget *RenderGraphPassContext::ResolveTexture(
        GraphTextureHandle handle, RenderGraphAccess access) const
    {
        if (Declares(handle, access))
            return bindings_.ResolveTexture(handle);
        violation_ = "Pass requested an undeclared texture handle or access intent";
        return nullptr;
    }

    std::span<const graphics::BufferHandle> RenderGraphPassContext::ResolveBuffers(
        GraphBufferHandle handle, RenderGraphAccess access) const
    {
        if (Declares(handle, access))
            return bindings_.ResolveBuffers(handle);
        violation_ = "Pass requested an undeclared buffer handle or access intent";
        return {};
    }

    std::span<const graphics::AccelerationStructureHandle>
    RenderGraphPassContext::ResolveAccelerationStructures(
        GraphAccelerationStructureHandle handle, RenderGraphAccess access) const
    {
        if (Declares(handle, access))
            return bindings_.ResolveAccelerationStructures(handle);
        violation_ = "Pass requested an undeclared acceleration-structure handle or access intent";
        return {};
    }

    bool RenderGraphExecutor::BeginFrame(const CompiledRenderGraph &plan)
    {
        Abort();
        frame_.emplace(plan);
        plan_ = &plan;
        return true;
    }

    bool RenderGraphExecutor::BeginFrame(const CompiledRenderGraph &plan,
                                         const RenderGraphBindings &bindings,
                                         graphics::CommandRecorder &recorder)
    {
        if (!frame_.has_value() || plan_ != &plan)
            BeginFrame(plan);
        bindings_ = &bindings;
        recorder_ = &recorder;
        return true;
    }

    bool RenderGraphExecutor::AcquireTransients(
        const CompiledRenderGraph &plan, graphics::RenderBackend &backend,
        graphics::Extent2D extent,
        const std::function<std::optional<graphics::RenderTargetDesc>(
            uint64_t, graphics::Extent2D)> &describe,
        std::string &error)
    {
        ReleaseTransients();
        transient_backend_ = &backend;
        for (const CompiledRenderGraph::TransientResource &transient : plan.Transients())
        {
            const auto desc = describe ? describe(transient.key, extent) : std::nullopt;
            if (!desc.has_value())
            {
                error = "No description exists for a declared transient resource";
                ReleaseTransients();
                return false;
            }
            const graphics::RenderTargetHandle handle = backend.AcquireTransientRenderTarget(*desc);
            if (!handle.IsValid())
            {
                error = "Graphics could not acquire a declared transient resource";
                ReleaseTransients();
                return false;
            }
            TransientLease lease{};
            lease.key = transient.key;
            lease.handle = handle;
            lease.target = std::make_unique<RenderTarget>();
            lease.target->Adopt(backend, handle, *desc);
            if (!lease.target->IsValid())
            {
                backend.ReleaseTransientRenderTarget(handle);
                error = "A transient render target wrapper could not adopt its pool handle";
                ReleaseTransients();
                return false;
            }
            transient_leases_.push_back(std::move(lease));
        }
        return true;
    }

    RenderTarget *RenderGraphExecutor::ResolveTransient(uint64_t key) const noexcept
    {
        const auto it = std::find_if(transient_leases_.begin(), transient_leases_.end(),
                                     [key](const auto &lease) { return lease.key == key; });
        return it != transient_leases_.end() ? it->target.get() : nullptr;
    }

    bool RenderGraphExecutor::ExecuteRenderer(
        const std::function<bool(const CompiledRenderGraph::Pass &)> &record_pass)
    {
        return frame_.has_value() && frame_->ExecuteRenderer(record_pass);
    }

    bool RenderGraphExecutor::ExecuteRenderer(
        const std::function<RenderGraphPassDisposition(
            const CompiledRenderGraph::Pass &)> &disposition,
        const std::function<bool(const RenderGraphPassContext &)> &record_pass,
        const std::function<void(uint64_t)> &begin_profile,
        const std::function<void(uint64_t)> &end_profile)
    {
        if (!frame_.has_value() || plan_ == nullptr || bindings_ == nullptr || recorder_ == nullptr)
            return false;
        return frame_->ExecuteRenderer([&](const CompiledRenderGraph::Pass &pass) {
            std::string error;
            if (!bindings_->ApplyPassTransitions(*plan_, pass, *recorder_, error))
                return false;
            const RenderGraphPassDisposition pass_disposition = disposition
                ? disposition(pass)
                : RenderGraphPassDisposition::Record;
            if (pass_disposition == RenderGraphPassDisposition::Fail)
                return false;
            if (pass_disposition == RenderGraphPassDisposition::ReusePreviousOutput)
                return true;

            RenderTarget *attachment = bindings_->ResolveWriteAttachment(pass);
            const bool has_attachment_write = std::any_of(
                pass.uses.begin(), pass.uses.end(), [](const RenderGraphResourceUse &use) {
                    return use.access == RenderGraphAccess::Write &&
                           std::holds_alternative<GraphTextureHandle>(use.handle) &&
                           (use.usage == RenderGraphUsage::ColorAttachment ||
                            use.usage == RenderGraphUsage::DepthAttachment);
                });
            if (has_attachment_write && attachment == nullptr)
                return false;

            const uint64_t profile_key = pass.user_key.value_or(0);
            if (begin_profile)
                begin_profile(profile_key);
            bool attachment_open = false;
            bool succeeded = true;
            if (attachment != nullptr)
            {
                attachment_open = attachment->BeginRecording(*recorder_);
                succeeded = attachment_open;
            }
            if (succeeded)
            {
                const RenderGraphPassContext context(pass, *bindings_, *recorder_);
                succeeded = record_pass(context);
                if (!context.IsClean())
                {
                    diagnostic_ = "Pass '" + pass.name + "' (key " +
                        std::to_string(profile_key) + ") violated graph bindings: " +
                        context.GetViolation();
                    succeeded = false;
                }
            }
            if (attachment_open)
                attachment->EndRecording(*recorder_);
            if (end_profile)
                end_profile(profile_key);
            return succeeded;
        });
    }

    bool RenderGraphExecutor::CanExecuteExternal() const noexcept
    {
        return frame_.has_value() && frame_->CanExecuteExternal();
    }

    bool RenderGraphExecutor::ExecuteExternal(
        const std::function<void()> &record_pass)
    {
        return frame_.has_value() && frame_->ExecuteExternal([&record_pass]() {
            record_pass();
            return true;
        });
    }

    bool RenderGraphExecutor::ExecuteExternal(
        const std::function<void()> &record_pass,
        const std::function<void()> &begin_profile,
        const std::function<void()> &end_profile)
    {
        if (!frame_.has_value() || plan_ == nullptr || bindings_ == nullptr || recorder_ == nullptr)
        {
            if (diagnostic_.empty())
                diagnostic_ = "External terminal has no active frame binding context";
            return false;
        }
        bool callback_failed = false;
        const bool executed = frame_->ExecuteExternal([&]() -> bool {
            const auto terminal = std::find_if(
                plan_->Passes().begin(), plan_->Passes().end(), [](const auto &pass) {
                    return pass.owner == RenderGraphPassOwner::External && pass.terminal;
                });
            if (terminal == plan_->Passes().end())
            {
                external_transition_failed_ = true;
                if (diagnostic_.empty())
                    diagnostic_ = "Compiled plan has no external terminal pass";
                callback_failed = true;
                return false;
            }
            std::string error;
            if (!bindings_->ApplyPassTransitions(*plan_, *terminal, *recorder_, error))
            {
                external_transition_failed_ = true;
                if (diagnostic_.empty())
                    diagnostic_ = "External terminal transition failed: " + error;
                callback_failed = true;
                return false;
            }
            if (begin_profile)
                begin_profile();
            record_pass();
            if (end_profile)
                end_profile();
            return true;
        });
        if (callback_failed)
            external_transition_failed_ = true;
        return executed;
    }

    RenderGraphExecutionResult RenderGraphExecutor::Finalize()
    {
        RenderGraphExecutionResult result{};
        if (!frame_.has_value())
        {
            result.diagnostic = "No render graph frame is active";
            return result;
        }
        result.finalized = frame_->Finalize(result.diagnostic);
        if (result.diagnostic.empty())
            result.diagnostic = diagnostic_;
        result.required_pass_failed = frame_->HasRequiredFailure();
        result.frame_succeeded = result.finalized && !result.required_pass_failed &&
                                  !external_transition_failed_;
        if (plan_ != nullptr)
        {
            result.pass_outcomes.reserve(plan_->Passes().size());
            for (std::size_t index = 0; index < plan_->Passes().size(); ++index)
            {
                const auto &pass = plan_->Passes()[index];
                result.pass_outcomes.emplace_back(
                    pass.user_key.value_or(static_cast<uint64_t>(index)),
                    frame_->GetOutcome(pass.user_key.value_or(static_cast<uint64_t>(index))));
            }
        }
        return result;
    }

    bool RenderGraphExecutor::Finalize(std::string &error)
    {
        RenderGraphExecutionResult result = Finalize();
        error = std::move(result.diagnostic);
        return result.finalized;
    }

    bool RenderGraphExecutor::HasRequiredFailure() const noexcept
    {
        return frame_.has_value() && frame_->HasRequiredFailure();
    }

    RenderGraphPassOutcome RenderGraphExecutor::GetOutcome(uint64_t pass_key) const noexcept
    {
        return frame_.has_value() ? frame_->GetOutcome(pass_key)
                                  : RenderGraphPassOutcome::NotInPlan;
    }

    void RenderGraphExecutor::Abort() noexcept
    {
        frame_.reset();
        plan_ = nullptr;
        bindings_ = nullptr;
        recorder_ = nullptr;
        external_transition_failed_ = false;
        diagnostic_.clear();
        ReleaseTransients();
    }

    void RenderGraphExecutor::ReleaseTransients() noexcept
    {
        for (TransientLease &lease : transient_leases_)
        {
            if (lease.target)
                lease.target->Cleanup();
            if (transient_backend_ != nullptr && lease.handle.IsValid())
                transient_backend_->ReleaseTransientRenderTarget(lease.handle);
        }
        transient_leases_.clear();
        transient_backend_ = nullptr;
    }
}
