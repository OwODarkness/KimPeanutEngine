#include "render_graph.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <queue>
#include <set>
#include <utility>

namespace kpengine::render
{
    namespace
    {
        std::atomic<uint64_t> next_graph_id{1};

        std::size_t ToIndex(GraphPassId pass) noexcept
        {
            return static_cast<std::size_t>(pass.index);
        }

        struct LifetimeKey
        {
            bool is_texture = false;
            uint32_t resource = 0;
            uint32_t version = 0;

            friend bool operator<(const LifetimeKey &left, const LifetimeKey &right) noexcept
            {
                if (left.is_texture != right.is_texture)
                {
                    return left.is_texture < right.is_texture;
                }
                if (left.resource != right.resource)
                {
                    return left.resource < right.resource;
                }
                return left.version < right.version;
            }
        };
    }

    bool CompiledRenderGraph::ContainsPass(GraphPassId id) const noexcept
    {
        return FindPass(id) != nullptr;
    }

    const CompiledRenderGraph::Pass *CompiledRenderGraph::FindPass(GraphPassId id) const noexcept
    {
        if (id.graph_id != graph_id_)
        {
            return nullptr;
        }
        for (const Pass &pass : passes_)
        {
            if (pass.id == id)
            {
                return &pass;
            }
        }
        return nullptr;
    }

    uint64_t RenderGraphBuilder::AllocateGraphId() noexcept
    {
        return next_graph_id.fetch_add(1, std::memory_order_relaxed);
    }

    RenderGraphBuilder::RenderGraphBuilder() : graph_id_(AllocateGraphId())
    {
    }

    bool RenderGraphBuilder::HasResourceName(const std::string &name) const noexcept
    {
        return std::any_of(textures_.begin(), textures_.end(), [&](const TextureRecord &record) {
                   return record.name == name;
               }) ||
               std::any_of(buffers_.begin(), buffers_.end(), [&](const BufferRecord &record) {
                   return record.name == name;
               });
    }

    bool RenderGraphBuilder::IsValidPass(GraphPassId pass) const noexcept
    {
        return pass.graph_id == graph_id_ && pass.index < passes_.size();
    }

    bool RenderGraphBuilder::IsValidTexture(GraphTextureHandle texture) const noexcept
    {
        return texture.graph_id == graph_id_ && texture.resource < textures_.size() &&
               texture.version < textures_[texture.resource].versions.size();
    }

    bool RenderGraphBuilder::IsValidBuffer(GraphBufferHandle buffer) const noexcept
    {
        return buffer.graph_id == graph_id_ && buffer.resource < buffers_.size() &&
               buffer.version < buffers_[buffer.resource].versions.size();
    }

    void RenderGraphBuilder::RecordDeclarationError(std::string message)
    {
        declaration_errors_.push_back(std::move(message));
    }

    GraphTextureHandle RenderGraphBuilder::CreateTexture(std::string name)
    {
        const GraphTextureHandle invalid{graph_id_, GraphTextureHandle::InvalidIndex,
                                         GraphTextureHandle::InvalidIndex};
        if (name.empty() || HasResourceName(name))
        {
            RecordDeclarationError("A render graph texture requires a unique non-empty name.");
            return invalid;
        }
        const uint32_t resource = static_cast<uint32_t>(textures_.size());
        textures_.push_back(TextureRecord{std::move(name), RenderGraphResourceLifetime::Transient,
                                          std::vector<TextureVersion>{{std::nullopt, false}}, 0});
        return GraphTextureHandle{graph_id_, resource, 0};
    }

    GraphTextureHandle RenderGraphBuilder::ImportTexture(std::string name)
    {
        const GraphTextureHandle invalid{graph_id_, GraphTextureHandle::InvalidIndex,
                                         GraphTextureHandle::InvalidIndex};
        if (name.empty() || HasResourceName(name))
        {
            RecordDeclarationError("An imported render graph texture requires a unique non-empty name.");
            return invalid;
        }
        const uint32_t resource = static_cast<uint32_t>(textures_.size());
        textures_.push_back(TextureRecord{std::move(name), RenderGraphResourceLifetime::Imported,
                                          std::vector<TextureVersion>{{std::nullopt, true}}, 0});
        return GraphTextureHandle{graph_id_, resource, 0};
    }

    GraphBufferHandle RenderGraphBuilder::CreateBuffer(std::string name)
    {
        const GraphBufferHandle invalid{graph_id_, GraphBufferHandle::InvalidIndex,
                                        GraphBufferHandle::InvalidIndex};
        if (name.empty() || HasResourceName(name))
        {
            RecordDeclarationError("A render graph buffer requires a unique non-empty name.");
            return invalid;
        }
        const uint32_t resource = static_cast<uint32_t>(buffers_.size());
        buffers_.push_back(BufferRecord{std::move(name), RenderGraphResourceLifetime::Transient,
                                        std::vector<BufferVersion>{{std::nullopt, false}}, 0});
        return GraphBufferHandle{graph_id_, resource, 0};
    }

    GraphBufferHandle RenderGraphBuilder::ImportBuffer(std::string name)
    {
        const GraphBufferHandle invalid{graph_id_, GraphBufferHandle::InvalidIndex,
                                        GraphBufferHandle::InvalidIndex};
        if (name.empty() || HasResourceName(name))
        {
            RecordDeclarationError("An imported render graph buffer requires a unique non-empty name.");
            return invalid;
        }
        const uint32_t resource = static_cast<uint32_t>(buffers_.size());
        buffers_.push_back(BufferRecord{std::move(name), RenderGraphResourceLifetime::Imported,
                                        std::vector<BufferVersion>{{std::nullopt, true}}, 0});
        return GraphBufferHandle{graph_id_, resource, 0};
    }

    GraphPassId RenderGraphBuilder::AddPass(RenderGraphPassDesc desc)
    {
        const GraphPassId pass{graph_id_, static_cast<uint32_t>(passes_.size())};
        passes_.push_back(PassRecord{std::move(desc), {}, {}, {}, {}});
        return pass;
    }

    bool RenderGraphBuilder::ReadTexture(GraphPassId pass, GraphTextureHandle texture)
    {
        if (!IsValidPass(pass) || !IsValidTexture(texture))
        {
            RecordDeclarationError("A render graph texture read references an invalid handle.");
            return false;
        }
        passes_[ToIndex(pass)].uses.push_back({texture, RenderGraphAccess::Read});
        return true;
    }

    bool RenderGraphBuilder::ReadBuffer(GraphPassId pass, GraphBufferHandle buffer)
    {
        if (!IsValidPass(pass) || !IsValidBuffer(buffer))
        {
            RecordDeclarationError("A render graph buffer read references an invalid handle.");
            return false;
        }
        passes_[ToIndex(pass)].uses.push_back({buffer, RenderGraphAccess::Read});
        return true;
    }

    std::optional<GraphTextureHandle> RenderGraphBuilder::WriteTexture(
        GraphPassId pass, GraphTextureHandle previous_version)
    {
        if (!IsValidPass(pass) || !IsValidTexture(previous_version))
        {
            RecordDeclarationError("A render graph texture write references an invalid handle.");
            return std::nullopt;
        }
        TextureRecord &record = textures_[previous_version.resource];
        if (previous_version.version != record.latest_version)
        {
            RecordDeclarationError("A render graph texture write must extend the latest resource version.");
            return std::nullopt;
        }
        PassRecord &pass_record = passes_[ToIndex(pass)];
        if (std::find(pass_record.written_textures.begin(), pass_record.written_textures.end(),
                      previous_version.resource) != pass_record.written_textures.end())
        {
            RecordDeclarationError("A render graph pass writes one texture resource more than once.");
            return std::nullopt;
        }
        const uint32_t version = static_cast<uint32_t>(record.versions.size());
        record.versions.push_back(TextureVersion{pass, false});
        record.latest_version = version;
        pass_record.written_textures.push_back(previous_version.resource);
        const GraphTextureHandle output{graph_id_, previous_version.resource, version};
        pass_record.uses.push_back({output, RenderGraphAccess::Write});
        return output;
    }

    std::optional<GraphBufferHandle> RenderGraphBuilder::WriteBuffer(
        GraphPassId pass, GraphBufferHandle previous_version)
    {
        if (!IsValidPass(pass) || !IsValidBuffer(previous_version))
        {
            RecordDeclarationError("A render graph buffer write references an invalid handle.");
            return std::nullopt;
        }
        BufferRecord &record = buffers_[previous_version.resource];
        if (previous_version.version != record.latest_version)
        {
            RecordDeclarationError("A render graph buffer write must extend the latest resource version.");
            return std::nullopt;
        }
        PassRecord &pass_record = passes_[ToIndex(pass)];
        if (std::find(pass_record.written_buffers.begin(), pass_record.written_buffers.end(),
                      previous_version.resource) != pass_record.written_buffers.end())
        {
            RecordDeclarationError("A render graph pass writes one buffer resource more than once.");
            return std::nullopt;
        }
        const uint32_t version = static_cast<uint32_t>(record.versions.size());
        record.versions.push_back(BufferVersion{pass, false});
        record.latest_version = version;
        pass_record.written_buffers.push_back(previous_version.resource);
        const GraphBufferHandle output{graph_id_, previous_version.resource, version};
        pass_record.uses.push_back({output, RenderGraphAccess::Write});
        return output;
    }

    bool RenderGraphBuilder::AddDependency(GraphPassId pass, GraphPassId dependency)
    {
        if (!IsValidPass(pass) || !IsValidPass(dependency) || pass == dependency)
        {
            RecordDeclarationError("A render graph pass dependency references an invalid or self pass.");
            return false;
        }
        passes_[ToIndex(pass)].explicit_dependencies.push_back(dependency);
        return true;
    }

    bool RenderGraphBuilder::ExportTexture(GraphTextureHandle texture, std::string export_name)
    {
        if (!IsValidTexture(texture) || export_name.empty())
        {
            RecordDeclarationError("A render graph texture export references an invalid handle or name.");
            return false;
        }
        exports_.push_back({texture, std::move(export_name)});
        return true;
    }

    bool RenderGraphBuilder::ExportBuffer(GraphBufferHandle buffer, std::string export_name)
    {
        if (!IsValidBuffer(buffer) || export_name.empty())
        {
            RecordDeclarationError("A render graph buffer export references an invalid handle or name.");
            return false;
        }
        exports_.push_back({buffer, std::move(export_name)});
        return true;
    }

    RenderGraphCompileResult RenderGraphBuilder::Compile() const
    {
        RenderGraphCompileResult result;
        for (const std::string &error : declaration_errors_)
        {
            result.diagnostics.push_back({RenderGraphDiagnosticCode::InvalidDeclaration, error});
        }

        std::set<std::string> pass_names;
        std::size_t external_terminal_count = 0;
        for (const PassRecord &pass : passes_)
        {
            if (pass.desc.name.empty() || !pass_names.insert(pass.desc.name).second)
            {
                result.diagnostics.push_back(
                    {RenderGraphDiagnosticCode::DuplicateName,
                     "Render graph pass names must be unique and non-empty."});
            }
            if (pass.desc.condition == RenderGraphPassCondition::Always && !pass.desc.enabled)
            {
                result.diagnostics.push_back(
                    {RenderGraphDiagnosticCode::InvalidDeclaration,
                     "An Always render graph pass cannot be disabled."});
            }
            if (pass.desc.owner == RenderGraphPassOwner::External)
            {
                if (!pass.desc.terminal || !pass.desc.side_effect)
                {
                    result.diagnostics.push_back(
                        {RenderGraphDiagnosticCode::InvalidDeclaration,
                         "An external render graph pass must be a side-effect terminal."});
                }
                ++external_terminal_count;
            }
            else if (pass.desc.terminal)
            {
                result.diagnostics.push_back(
                    {RenderGraphDiagnosticCode::InvalidDeclaration,
                     "Only an external render graph pass may be terminal."});
            }
        }
        if (external_terminal_count > 1)
        {
            result.diagnostics.push_back(
                {RenderGraphDiagnosticCode::InvalidDeclaration,
                 "A render graph may contain only one external terminal pass."});
        }

        std::set<std::string> export_names;
        for (const ExportRecord &export_record : exports_)
        {
            if (!export_names.insert(export_record.name).second)
            {
                result.diagnostics.push_back(
                    {RenderGraphDiagnosticCode::DuplicateExport,
                     "Render graph export names must be unique."});
            }
        }

        const std::size_t pass_count = passes_.size();
        std::vector<std::vector<std::size_t>> edges(pass_count);
        std::vector<std::vector<std::size_t>> reverse_edges(pass_count);
        const auto add_edge = [&](std::size_t producer, std::size_t consumer) {
            if (producer == consumer)
            {
                result.diagnostics.push_back(
                    {RenderGraphDiagnosticCode::Cycle,
                     "A render graph pass depends on itself through resource flow."});
                return;
            }
            edges[producer].push_back(consumer);
            reverse_edges[consumer].push_back(producer);
        };

        const auto add_producer_edge = [&](const std::optional<GraphPassId> &producer,
                                           GraphPassId consumer) {
            if (!producer.has_value())
            {
                return;
            }
            if (!IsValidPass(*producer) || !IsValidPass(consumer))
            {
                result.diagnostics.push_back(
                    {RenderGraphDiagnosticCode::InvalidDependency,
                     "Render graph resource flow references an invalid producer or consumer."});
                return;
            }
            const PassRecord &producer_record = passes_[ToIndex(*producer)];
            const PassRecord &consumer_record = passes_[ToIndex(consumer)];
            if (!producer_record.desc.enabled && consumer_record.desc.enabled)
            {
                result.diagnostics.push_back(
                    {RenderGraphDiagnosticCode::ConditionalDependency,
                     "An enabled render graph pass depends on a disabled conditional producer."});
            }
            add_edge(ToIndex(*producer), ToIndex(consumer));
        };

        for (std::size_t pass_index = 0; pass_index < pass_count; ++pass_index)
        {
            const GraphPassId pass{graph_id_, static_cast<uint32_t>(pass_index)};
            const PassRecord &record = passes_[pass_index];
            for (const RenderGraphResourceUse &use : record.uses)
            {
                if (record.desc.owner == RenderGraphPassOwner::External &&
                    use.access == RenderGraphAccess::Write)
                {
                    result.diagnostics.push_back(
                        {RenderGraphDiagnosticCode::InvalidDeclaration,
                         "An external terminal render graph pass cannot write resources."});
                }
                if (const auto *texture = std::get_if<GraphTextureHandle>(&use.handle))
                {
                    if (!IsValidTexture(*texture))
                    {
                        result.diagnostics.push_back(
                            {RenderGraphDiagnosticCode::InvalidHandle,
                             "A render graph pass contains an invalid texture handle."});
                        continue;
                    }
                    const TextureVersion &version = textures_[texture->resource].versions[texture->version];
                    if (use.access == RenderGraphAccess::Read && !version.imported &&
                        !version.producer.has_value())
                    {
                        result.diagnostics.push_back(
                            {RenderGraphDiagnosticCode::MissingProducer,
                             "A render graph texture is read without an imported or produced version."});
                    }
                    if (use.access == RenderGraphAccess::Read)
                    {
                        add_producer_edge(version.producer, pass);
                    }
                }
                else
                {
                    const GraphBufferHandle buffer = std::get<GraphBufferHandle>(use.handle);
                    if (!IsValidBuffer(buffer))
                    {
                        result.diagnostics.push_back(
                            {RenderGraphDiagnosticCode::InvalidHandle,
                             "A render graph pass contains an invalid buffer handle."});
                        continue;
                    }
                    const BufferVersion &version = buffers_[buffer.resource].versions[buffer.version];
                    if (use.access == RenderGraphAccess::Read && !version.imported &&
                        !version.producer.has_value())
                    {
                        result.diagnostics.push_back(
                            {RenderGraphDiagnosticCode::MissingProducer,
                             "A render graph buffer is read without an imported or produced version."});
                    }
                    if (use.access == RenderGraphAccess::Read)
                    {
                        add_producer_edge(version.producer, pass);
                    }
                }
            }
            for (const GraphPassId dependency : record.explicit_dependencies)
            {
                if (!IsValidPass(dependency))
                {
                    result.diagnostics.push_back(
                        {RenderGraphDiagnosticCode::InvalidDependency,
                         "A render graph pass contains an invalid explicit dependency."});
                    continue;
                }
                if (!passes_[ToIndex(dependency)].desc.enabled && record.desc.enabled)
                {
                    result.diagnostics.push_back(
                        {RenderGraphDiagnosticCode::ConditionalDependency,
                         "An enabled render graph pass depends on a disabled conditional pass."});
                }
                add_edge(ToIndex(dependency), pass_index);
            }
        }

        const auto add_export_dependency = [&](const ExportRecord &export_record) {
            if (const auto *texture = std::get_if<GraphTextureHandle>(&export_record.handle))
            {
                if (!IsValidTexture(*texture))
                {
                    result.diagnostics.push_back(
                        {RenderGraphDiagnosticCode::InvalidHandle,
                         "A render graph export contains an invalid texture handle."});
                    return;
                }
                const TextureVersion &version = textures_[texture->resource].versions[texture->version];
                if (version.producer.has_value() &&
                    !passes_[ToIndex(*version.producer)].desc.enabled)
                {
                    result.diagnostics.push_back(
                        {RenderGraphDiagnosticCode::ConditionalDependency,
                         "A render graph export depends on a disabled conditional producer."});
                }
            }
            else
            {
                const GraphBufferHandle buffer = std::get<GraphBufferHandle>(export_record.handle);
                if (!IsValidBuffer(buffer))
                {
                    result.diagnostics.push_back(
                        {RenderGraphDiagnosticCode::InvalidHandle,
                         "A render graph export contains an invalid buffer handle."});
                    return;
                }
                const BufferVersion &version = buffers_[buffer.resource].versions[buffer.version];
                if (version.producer.has_value() &&
                    !passes_[ToIndex(*version.producer)].desc.enabled)
                {
                    result.diagnostics.push_back(
                        {RenderGraphDiagnosticCode::ConditionalDependency,
                         "A render graph export depends on a disabled conditional producer."});
                }
            }
        };
        for (const ExportRecord &export_record : exports_)
        {
            add_export_dependency(export_record);
        }

        for (std::vector<std::size_t> &outgoing : edges)
        {
            std::sort(outgoing.begin(), outgoing.end());
            outgoing.erase(std::unique(outgoing.begin(), outgoing.end()), outgoing.end());
        }
        for (std::vector<std::size_t> &incoming : reverse_edges)
        {
            std::sort(incoming.begin(), incoming.end());
            incoming.erase(std::unique(incoming.begin(), incoming.end()), incoming.end());
        }

        std::vector<std::size_t> indegree(pass_count, 0);
        for (std::size_t producer = 0; producer < pass_count; ++producer)
        {
            if (!passes_[producer].desc.enabled)
            {
                continue;
            }
            for (const std::size_t consumer : edges[producer])
            {
                if (passes_[consumer].desc.enabled)
                {
                    ++indegree[consumer];
                }
            }
        }

        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t pass_index = 0; pass_index < pass_count; ++pass_index)
        {
            if (passes_[pass_index].desc.enabled && indegree[pass_index] == 0)
            {
                ready.push(pass_index);
            }
        }
        std::vector<std::size_t> topological_order;
        while (!ready.empty())
        {
            const std::size_t pass_index = ready.top();
            ready.pop();
            topological_order.push_back(pass_index);
            for (const std::size_t consumer : edges[pass_index])
            {
                if (passes_[consumer].desc.enabled && --indegree[consumer] == 0)
                {
                    ready.push(consumer);
                }
            }
        }
        std::vector<bool> in_topological_order(pass_count, false);
        for (const std::size_t pass_index : topological_order)
        {
            in_topological_order[pass_index] = true;
        }
        for (std::size_t pass_index = 0; pass_index < pass_count; ++pass_index)
        {
            if (passes_[pass_index].desc.enabled && !in_topological_order[pass_index])
            {
                result.diagnostics.push_back(
                    {RenderGraphDiagnosticCode::Cycle,
                     "Render graph compilation found a cycle involving pass '" +
                         passes_[pass_index].desc.name + "'."});
            }
        }

        if (!result.diagnostics.empty())
        {
            return result;
        }

        std::vector<bool> live(pass_count, false);
        std::queue<std::size_t> pending_roots;
        for (std::size_t pass_index = 0; pass_index < pass_count; ++pass_index)
        {
            if (passes_[pass_index].desc.enabled && passes_[pass_index].desc.side_effect)
            {
                pending_roots.push(pass_index);
            }
        }
        for (const ExportRecord &export_record : exports_)
        {
            std::optional<GraphPassId> producer;
            if (const auto *texture = std::get_if<GraphTextureHandle>(&export_record.handle))
            {
                if (IsValidTexture(*texture))
                {
                    producer = textures_[texture->resource].versions[texture->version].producer;
                }
            }
            else
            {
                const GraphBufferHandle buffer = std::get<GraphBufferHandle>(export_record.handle);
                if (IsValidBuffer(buffer))
                {
                    producer = buffers_[buffer.resource].versions[buffer.version].producer;
                }
            }
            if (producer.has_value() && passes_[ToIndex(*producer)].desc.enabled)
            {
                pending_roots.push(ToIndex(*producer));
            }
        }
        while (!pending_roots.empty())
        {
            const std::size_t pass_index = pending_roots.front();
            pending_roots.pop();
            if (live[pass_index])
            {
                continue;
            }
            live[pass_index] = true;
            for (const std::size_t producer : reverse_edges[pass_index])
            {
                if (passes_[producer].desc.enabled)
                {
                    pending_roots.push(producer);
                }
            }
        }

        std::vector<CompiledRenderGraph::Pass> compiled_passes;
        compiled_passes.reserve(topological_order.size());
        std::vector<std::size_t> live_order(pass_count, std::numeric_limits<std::size_t>::max());
        for (const std::size_t pass_index : topological_order)
        {
            if (!live[pass_index])
            {
                continue;
            }
            live_order[pass_index] = compiled_passes.size();
            const PassRecord &record = passes_[pass_index];
            compiled_passes.push_back(
                {GraphPassId{graph_id_, static_cast<uint32_t>(pass_index)}, record.desc.name,
                 pass_index, record.uses, record.desc.condition, record.desc.owner,
                 record.desc.terminal});
        }

        std::vector<RenderGraphLifetimeInterval> lifetimes;
        std::map<LifetimeKey, std::size_t> lifetime_indices;
        const auto resource_name = [&](const RenderGraphResourceHandle &handle) -> const std::string & {
            if (const auto *texture = std::get_if<GraphTextureHandle>(&handle))
            {
                return textures_[texture->resource].name;
            }
            return buffers_[std::get<GraphBufferHandle>(handle).resource].name;
        };
        const auto lifetime_key = [](const RenderGraphResourceHandle &handle) {
            if (const auto *texture = std::get_if<GraphTextureHandle>(&handle))
            {
                return LifetimeKey{true, texture->resource, texture->version};
            }
            const GraphBufferHandle buffer = std::get<GraphBufferHandle>(handle);
            return LifetimeKey{false, buffer.resource, buffer.version};
        };
        const auto record_lifetime = [&](const RenderGraphResourceHandle &handle,
                                         std::size_t pass_index) {
            const LifetimeKey key = lifetime_key(handle);
            const auto [iterator, inserted] = lifetime_indices.emplace(key, lifetimes.size());
            if (inserted)
            {
                lifetimes.push_back({handle, resource_name(handle), pass_index, pass_index});
            }
            else
            {
                RenderGraphLifetimeInterval &lifetime = lifetimes[iterator->second];
                lifetime.first_use = std::min(lifetime.first_use, pass_index);
                lifetime.last_use = std::max(lifetime.last_use, pass_index);
            }
        };
        for (const CompiledRenderGraph::Pass &pass : compiled_passes)
        {
            const std::size_t execution_index = live_order[pass.id.index];
            for (const RenderGraphResourceUse &use : pass.uses)
            {
                record_lifetime(use.handle, execution_index);
            }
        }
        if (!compiled_passes.empty())
        {
            for (const ExportRecord &export_record : exports_)
            {
                const LifetimeKey key = lifetime_key(export_record.handle);
                const auto iterator = lifetime_indices.find(key);
                if (iterator != lifetime_indices.end())
                {
                    lifetimes[iterator->second].last_use = compiled_passes.size() - 1;
                }
            }
        }

        result.graph = CompiledRenderGraph::Create(graph_id_, std::move(compiled_passes),
                                                   std::move(lifetimes));
        return result;
    }
}
