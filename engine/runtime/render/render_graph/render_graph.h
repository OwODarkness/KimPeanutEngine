#ifndef KPENGINE_RUNTIME_RENDER_RENDER_GRAPH_RENDER_GRAPH_H
#define KPENGINE_RUNTIME_RENDER_RENDER_GRAPH_RENDER_GRAPH_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <utility>

namespace kpengine::render
{
    struct GraphTextureHandle
    {
        static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

        uint64_t graph_id = 0;
        uint32_t resource = InvalidIndex;
        uint32_t version = InvalidIndex;

        bool IsValid() const noexcept
        {
            return graph_id != 0 && resource != InvalidIndex && version != InvalidIndex;
        }

        friend bool operator==(const GraphTextureHandle &, const GraphTextureHandle &) = default;
    };

    struct GraphBufferHandle
    {
        static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

        uint64_t graph_id = 0;
        uint32_t resource = InvalidIndex;
        uint32_t version = InvalidIndex;

        bool IsValid() const noexcept
        {
            return graph_id != 0 && resource != InvalidIndex && version != InvalidIndex;
        }

        friend bool operator==(const GraphBufferHandle &, const GraphBufferHandle &) = default;
    };

    struct GraphPassId
    {
        static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

        uint64_t graph_id = 0;
        uint32_t index = InvalidIndex;

        bool IsValid() const noexcept
        {
            return graph_id != 0 && index != InvalidIndex;
        }

        friend bool operator==(const GraphPassId &, const GraphPassId &) = default;
    };

    enum class RenderGraphResourceLifetime : uint8_t
    {
        Imported,
        Transient,
    };

    enum class RenderGraphAccess : uint8_t
    {
        Read,
        Write,
    };

    enum class RenderGraphPassCondition : uint8_t
    {
        Always,
        Optional,
    };

    enum class RenderGraphPassOwner : uint8_t
    {
        Renderer,
        External,
    };

    enum class RenderGraphDiagnosticCode : uint8_t
    {
        InvalidDeclaration,
        InvalidHandle,
        MissingProducer,
        DuplicateName,
        DuplicateWrite,
        InvalidDependency,
        ConditionalDependency,
        Cycle,
        DuplicateExport,
    };

    struct RenderGraphPassDesc
    {
        std::string name;
        RenderGraphPassCondition condition = RenderGraphPassCondition::Always;
        bool enabled = true;
        bool side_effect = false;
        RenderGraphPassOwner owner = RenderGraphPassOwner::Renderer;
        bool terminal = false;
    };

    struct RenderGraphDiagnostic
    {
        RenderGraphDiagnosticCode code = RenderGraphDiagnosticCode::InvalidDeclaration;
        std::string message;
    };

    using RenderGraphResourceHandle = std::variant<GraphTextureHandle, GraphBufferHandle>;

    struct RenderGraphResourceUse
    {
        RenderGraphResourceHandle handle;
        RenderGraphAccess access = RenderGraphAccess::Read;
    };

    struct RenderGraphLifetimeInterval
    {
        RenderGraphResourceHandle handle;
        std::string resource_name;
        std::size_t first_use = 0;
        std::size_t last_use = 0;
    };

    struct RenderGraphCompileResult;

    class CompiledRenderGraph final
    {
    public:
        struct Pass
        {
            GraphPassId id;
            std::string name;
            std::size_t declaration_index = 0;
            std::vector<RenderGraphResourceUse> uses;
            RenderGraphPassCondition condition = RenderGraphPassCondition::Always;
            RenderGraphPassOwner owner = RenderGraphPassOwner::Renderer;
            bool terminal = false;
        };

        const std::vector<Pass> &Passes() const noexcept { return passes_; }
        const std::vector<RenderGraphLifetimeInterval> &Lifetimes() const noexcept
        {
            return lifetimes_;
        }
        uint64_t GraphId() const noexcept { return graph_id_; }

        static CompiledRenderGraph Create(
            uint64_t graph_id, std::vector<Pass> passes,
            std::vector<RenderGraphLifetimeInterval> lifetimes)
        {
            return CompiledRenderGraph(graph_id, std::move(passes), std::move(lifetimes));
        }

        bool ContainsPass(GraphPassId id) const noexcept;
        const Pass *FindPass(GraphPassId id) const noexcept;

    private:
        friend class RenderGraphBuilder;

        CompiledRenderGraph(uint64_t graph_id, std::vector<Pass> passes,
                            std::vector<RenderGraphLifetimeInterval> lifetimes)
            : graph_id_(graph_id), passes_(std::move(passes)), lifetimes_(std::move(lifetimes))
        {
        }

        uint64_t graph_id_ = 0;
        std::vector<Pass> passes_;
        std::vector<RenderGraphLifetimeInterval> lifetimes_;
    };

    struct RenderGraphCompileResult
    {
        std::optional<CompiledRenderGraph> graph;
        std::vector<RenderGraphDiagnostic> diagnostics;

        bool Succeeded() const noexcept { return graph.has_value() && diagnostics.empty(); }
    };

    class RenderGraphBuilder final
    {
    public:
        RenderGraphBuilder();

        GraphTextureHandle CreateTexture(std::string name);
        GraphTextureHandle ImportTexture(std::string name);
        GraphBufferHandle CreateBuffer(std::string name);
        GraphBufferHandle ImportBuffer(std::string name);

        GraphPassId AddPass(RenderGraphPassDesc desc);
        bool ReadTexture(GraphPassId pass, GraphTextureHandle texture);
        bool ReadBuffer(GraphPassId pass, GraphBufferHandle buffer);
        std::optional<GraphTextureHandle> WriteTexture(GraphPassId pass,
                                                        GraphTextureHandle previous_version);
        std::optional<GraphBufferHandle> WriteBuffer(GraphPassId pass,
                                                      GraphBufferHandle previous_version);
        bool AddDependency(GraphPassId pass, GraphPassId dependency);
        bool ExportTexture(GraphTextureHandle texture, std::string export_name);
        bool ExportBuffer(GraphBufferHandle buffer, std::string export_name);

        RenderGraphCompileResult Compile() const;
        uint64_t GraphId() const noexcept { return graph_id_; }

    private:
        struct TextureVersion
        {
            std::optional<GraphPassId> producer;
            bool imported = false;
        };

        struct BufferVersion
        {
            std::optional<GraphPassId> producer;
            bool imported = false;
        };

        struct TextureRecord
        {
            std::string name;
            RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
            std::vector<TextureVersion> versions;
            uint32_t latest_version = 0;
        };

        struct BufferRecord
        {
            std::string name;
            RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
            std::vector<BufferVersion> versions;
            uint32_t latest_version = 0;
        };

        struct ExportRecord
        {
            RenderGraphResourceHandle handle;
            std::string name;
        };

        struct PassRecord
        {
            RenderGraphPassDesc desc;
            std::vector<RenderGraphResourceUse> uses;
            std::vector<GraphPassId> explicit_dependencies;
            std::vector<uint32_t> written_textures;
            std::vector<uint32_t> written_buffers;
        };

        static uint64_t AllocateGraphId() noexcept;
        bool HasResourceName(const std::string &name) const noexcept;
        bool IsValidPass(GraphPassId pass) const noexcept;
        bool IsValidTexture(GraphTextureHandle texture) const noexcept;
        bool IsValidBuffer(GraphBufferHandle buffer) const noexcept;
        void RecordDeclarationError(std::string message);

        uint64_t graph_id_ = 0;
        std::vector<TextureRecord> textures_;
        std::vector<BufferRecord> buffers_;
        std::vector<PassRecord> passes_;
        std::vector<ExportRecord> exports_;
        std::vector<std::string> declaration_errors_;
    };
}

#endif
