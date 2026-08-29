#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_SOURCE_REGISTRY_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_SOURCE_REGISTRY_H

#include <mutex>
#include <unordered_map>
#include <variant>
#include <vector>

#include "render/light/light_source.h"
#include "render/light/light_world.h"

namespace kpengine::render
{
    // Render-owned game-thread inbox. It maps the Gameplay registration token
    // to a private LightWorld handle only while draining on the render thread.
    class LightSourceRegistry final : public ILightSourceSink
    {
    public:
        LightSourceHandle EnqueueCreate(const LightSourceDesc &source) override;
        bool EnqueueUpdate(LightSourceHandle handle, const LightSourceDesc &source) override;
        bool EnqueueDestroy(LightSourceHandle handle) override;

        void Drain(LightWorld &light_world);
        void Clear(LightWorld &light_world);

    private:
        struct CreateCommand
        {
            LightSourceHandle handle;
            LightSourceDesc source;
        };
        struct UpdateCommand
        {
            LightSourceHandle handle;
            LightSourceDesc source;
        };
        struct DestroyCommand
        {
            LightSourceHandle handle;
        };
        using Command = std::variant<CreateCommand, UpdateCommand, DestroyCommand>;

        struct SourceRecord
        {
            LightSourceHandle source_handle;
            LightHandle light_handle;
        };

        void ApplyCommands(LightWorld &light_world, std::vector<Command> commands);

        std::mutex command_mutex_;
        HandleSystem<LightSourceHandle> handles_;
        std::vector<Command> pending_commands_;
        std::unordered_map<uint32_t, SourceRecord> records_;
    };
}

#endif
