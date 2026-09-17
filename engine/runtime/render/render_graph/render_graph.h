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

    // Portable state a use requires, never a native layout. The backend resolves
    // the state a resource is currently in and translates the difference.
    enum class RenderGraphUsage : uint8_t
    {
        Undefined,
        Sampled,
        ColorAttachment,
        DepthAttachment,
        TransferSource,
        TransferDestination,
        Present,
    };

    // What an attachment use does to its contents. Load and Clear are the
    // authored intent; the persistent raster targets currently fix their load
    // operation at creation, so no authored entry sets this yet -- it exists for
    // the transient consumer, which will describe attachments it owns.
    enum class RenderGraphAttachmentOp : uint8_t
    {
        None,
        Load,
        Clear,
    };

    // Whole-resource today. The fields are retained so mip and layer tracking can
    // be added later without replacing the handle types.
    struct RenderGraphResourceRange
    {
        static constexpr uint32_t AllRemaining = std::numeric_limits<uint32_t>::max();

        uint32_t base_mip_level = 0;
        uint32_t mip_level_count = AllRemaining;
        uint32_t base_array_layer = 0;
        uint32_t array_layer_count = AllRemaining;

        // True when the range covers the whole resource, which is the only shape
        // the graph currently produces or consumes.
        bool IsWholeResource() const noexcept
        {
            return base_mip_level == 0 && mip_level_count == AllRemaining &&
                   base_array_layer == 0 && array_layer_count == AllRemaining;
        }

        friend bool operator==(const RenderGraphResourceRange &,
                               const RenderGraphResourceRange &) = default;
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
        DuplicatePassKey,
        InvalidUsage,
    };

    struct RenderGraphPassDesc
    {
        std::string name;
        RenderGraphPassCondition condition = RenderGraphPassCondition::Always;
        bool enabled = true;
        bool side_effect = false;
        RenderGraphPassOwner owner = RenderGraphPassOwner::Renderer;
        bool terminal = false;
        // Caller-owned identity, such as the renderer's typed pass id. Authors
        // who need to dispatch or query by pass must set it, and enabled passes
        // must not share one; the graph never interprets the value.
        std::optional<uint64_t> user_key;
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
        RenderGraphUsage usage = RenderGraphUsage::Undefined;
        RenderGraphAttachmentOp attachment_op = RenderGraphAttachmentOp::None;
        RenderGraphResourceRange range{};
    };

    // One requirement that a pass places on a resource's state: at this pass the
    // resource version must be in this usage. It deliberately carries no
    // from-state, because the backend owns what the resource is currently in.
    struct RenderGraphTransitionIntent
    {
        RenderGraphResourceHandle handle;
        std::string resource_name;
        // Index into the compiled execution order, i.e. the pass that requires it.
        std::size_t pass_index = 0;
        RenderGraphUsage usage = RenderGraphUsage::Undefined;
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
            std::optional<uint64_t> user_key;
        };

        const std::vector<Pass> &Passes() const noexcept { return passes_; }
        const std::vector<RenderGraphLifetimeInterval> &Lifetimes() const noexcept
        {
            return lifetimes_;
        }
        // State requirements in compiled execution order. Each entry says a
        // resource version must be in that usage at that pass; the backend owns
        // what it is currently in and translates the difference.
        const std::vector<RenderGraphTransitionIntent> &Transitions() const noexcept
        {
            return transitions_;
        }
        uint64_t GraphId() const noexcept { return graph_id_; }

        static CompiledRenderGraph Create(
            uint64_t graph_id, std::vector<Pass> passes,
            std::vector<RenderGraphLifetimeInterval> lifetimes,
            std::vector<RenderGraphTransitionIntent> transitions)
        {
            return CompiledRenderGraph(graph_id, std::move(passes), std::move(lifetimes),
                                       std::move(transitions));
        }

        bool ContainsPass(GraphPassId id) const noexcept;
        const Pass *FindPass(GraphPassId id) const noexcept;

    private:
        friend class RenderGraphBuilder;

        CompiledRenderGraph(uint64_t graph_id, std::vector<Pass> passes,
                            std::vector<RenderGraphLifetimeInterval> lifetimes,
                            std::vector<RenderGraphTransitionIntent> transitions)
            : graph_id_(graph_id), passes_(std::move(passes)), lifetimes_(std::move(lifetimes)),
              transitions_(std::move(transitions))
        {
        }

        uint64_t graph_id_ = 0;
        std::vector<Pass> passes_;
        std::vector<RenderGraphLifetimeInterval> lifetimes_;
        std::vector<RenderGraphTransitionIntent> transitions_;
    };

    struct RenderGraphCompileResult
    {
        std::optional<CompiledRenderGraph> graph;
        std::vector<RenderGraphDiagnostic> diagnostics;

        bool Succeeded() const noexcept { return graph.has_value() && diagnostics.empty(); }
    };

    class RenderGraphBuilder;
    class RenderGraphPassRef;

    class RenderGraphBuilder final
    {
    public:
        RenderGraphBuilder();

        GraphTextureHandle CreateTexture(std::string name);
        GraphTextureHandle ImportTexture(std::string name);
        GraphBufferHandle CreateBuffer(std::string name);
        GraphBufferHandle ImportBuffer(std::string name);

        // Re-stamps the handle with the resource's current version, which is the
        // version later passes produce. The fluent pass methods use this so a
        // declaration does not have to thread versions by hand.
        GraphTextureHandle CurrentVersion(GraphTextureHandle texture) const noexcept;
        GraphBufferHandle CurrentVersion(GraphBufferHandle buffer) const noexcept;

        RenderGraphPassRef AddPass(RenderGraphPassDesc desc);
        bool ReadTexture(GraphPassId pass, GraphTextureHandle texture,
                         RenderGraphUsage usage = RenderGraphUsage::Undefined);
        bool ReadBuffer(GraphPassId pass, GraphBufferHandle buffer,
                        RenderGraphUsage usage = RenderGraphUsage::Undefined);
        std::optional<GraphTextureHandle> WriteTexture(
            GraphPassId pass, GraphTextureHandle previous_version,
            RenderGraphUsage usage = RenderGraphUsage::Undefined,
            RenderGraphAttachmentOp attachment_op = RenderGraphAttachmentOp::None);
        std::optional<GraphBufferHandle> WriteBuffer(
            GraphPassId pass, GraphBufferHandle previous_version,
            RenderGraphUsage usage = RenderGraphUsage::Undefined,
            RenderGraphAttachmentOp attachment_op = RenderGraphAttachmentOp::None);
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

    // One declared pass, returned by RenderGraphBuilder::AddPass. Reads and
    // writes chain onto it, so a pass and its resource flow are one statement:
    //
    //     graph.AddPass({"ToneMap", ...}).Read(scene_hdr).Write(scene_color);
    //
    // Read and Write act on the resource's *current* version rather than the
    // version stamped in the handle, so a write is visible to every later pass
    // without the caller threading versions. The reference borrows its builder
    // and must not outlive it.
    class RenderGraphPassRef final
    {
    public:
        RenderGraphPassRef() = default;

        RenderGraphPassRef &Read(GraphTextureHandle texture,
                                 RenderGraphUsage usage = RenderGraphUsage::Undefined);
        RenderGraphPassRef &Read(GraphBufferHandle buffer,
                                 RenderGraphUsage usage = RenderGraphUsage::Undefined);
        RenderGraphPassRef &Write(GraphTextureHandle texture,
                                  RenderGraphUsage usage = RenderGraphUsage::Undefined,
                                  RenderGraphAttachmentOp attachment_op =
                                      RenderGraphAttachmentOp::None);
        RenderGraphPassRef &Write(GraphBufferHandle buffer,
                                  RenderGraphUsage usage = RenderGraphUsage::Undefined,
                                  RenderGraphAttachmentOp attachment_op =
                                      RenderGraphAttachmentOp::None);
        RenderGraphPassRef &DependsOn(GraphPassId dependency);

        GraphPassId Id() const noexcept { return pass_; }
        bool IsValid() const noexcept { return pass_.IsValid(); }

        // Lets existing call sites that only need the identity keep taking a
        // GraphPassId.
        operator GraphPassId() const noexcept { return pass_; }

    private:
        friend class RenderGraphBuilder;

        RenderGraphPassRef(RenderGraphBuilder &builder, GraphPassId pass) noexcept
            : builder_(&builder), pass_(pass)
        {
        }

        RenderGraphBuilder *builder_ = nullptr;
        GraphPassId pass_;
    };
}

#endif
