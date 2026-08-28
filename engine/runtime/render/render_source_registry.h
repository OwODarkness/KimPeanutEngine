#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SOURCE_REGISTRY_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SOURCE_REGISTRY_H

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "render/render_source.h"
#include "render/render_world/render_world.h"

namespace kpengine::render
{
    enum class RenderableSourceState : uint8_t
    {
        Pending,
        Ready,
        Failed,
    };

    struct RenderableSourceResolution
    {
        RenderableSourceState state = RenderableSourceState::Pending;
        std::string diagnostic;
        std::optional<MeshProxyDesc> proxy_desc;
    };

    // Render-owned inbox and source-record registry. Producers may call its
    // sink interface from the game thread; Drain runs on the render thread.
    class RenderableSourceRegistry final : public IRenderableSourceSink
    {
    public:
        using ResolveFunction = std::function<RenderableSourceResolution(
            const PrimitiveRenderableSourceDesc &source)>;

        RenderableSourceHandle EnqueueCreate(
            const PrimitiveRenderableSourceDesc &source) override;
        bool EnqueueUpdate(RenderableSourceHandle handle,
                           const PrimitiveRenderableSourceDesc &source) override;
        bool EnqueueDestroy(RenderableSourceHandle handle) override;

        void Drain(RenderWorld &render_world, const ResolveFunction &resolve);
        void Clear(RenderWorld &render_world);

    private:
        struct CreateCommand
        {
            RenderableSourceHandle handle;
            PrimitiveRenderableSourceDesc source;
        };
        struct UpdateCommand
        {
            RenderableSourceHandle handle;
            PrimitiveRenderableSourceDesc source;
        };
        struct DestroyCommand
        {
            RenderableSourceHandle handle;
        };
        using Command = std::variant<CreateCommand, UpdateCommand, DestroyCommand>;

        struct SourceRecord
        {
            RenderableSourceHandle handle;
            PrimitiveRenderableSourceDesc source;
            RenderableHandle proxy_handle;
            RenderableSourceState state = RenderableSourceState::Pending;
            bool needs_resolution = true;
            std::string diagnostic;
        };

        void ApplyCommands(RenderWorld &render_world, std::vector<Command> commands);
        void ResolveRecords(RenderWorld &render_world, const ResolveFunction &resolve);
        void RetireProxy(RenderWorld &render_world, SourceRecord &record);

        std::mutex command_mutex_;
        HandleSystem<RenderableSourceHandle> handles_;
        std::vector<Command> pending_commands_;
        std::unordered_map<uint32_t, SourceRecord> records_;
    };
}

#endif
