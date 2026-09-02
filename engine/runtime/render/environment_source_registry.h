#ifndef KPENGINE_RUNTIME_RENDER_ENVIRONMENT_SOURCE_REGISTRY_H
#define KPENGINE_RUNTIME_RENDER_ENVIRONMENT_SOURCE_REGISTRY_H

#include <mutex>
#include <optional>
#include <variant>
#include <vector>

#include "render/environment_source.h"

namespace kpengine::render
{
    // Render-owned single-source inbox. Runtime may submit commands from the
    // game thread; Render drains them before resolving frame environment state.
    class EnvironmentSourceRegistry final : public IEnvironmentSourceSink
    {
    public:
        EnvironmentSourceHandle EnqueueCreate(const EnvironmentSourceDesc &source) override;
        bool EnqueueDestroy(EnvironmentSourceHandle handle) override;

        void Drain();
        void Clear();

        std::optional<EnvironmentSourceDesc> GetActiveSource() const { return active_source_; }
        std::optional<EnvironmentSourceHandle> GetActiveHandle() const { return active_handle_; }

    private:
        struct CreateCommand
        {
            EnvironmentSourceHandle handle;
            EnvironmentSourceDesc source;
        };
        struct DestroyCommand
        {
            EnvironmentSourceHandle handle;
        };
        using Command = std::variant<CreateCommand, DestroyCommand>;

        void ApplyCommands(std::vector<Command> commands);

        mutable std::mutex command_mutex_;
        HandleSystem<EnvironmentSourceHandle> handles_;
        std::vector<Command> pending_commands_;
        std::optional<EnvironmentSourceDesc> active_source_;
        std::optional<EnvironmentSourceHandle> active_handle_;
        std::optional<EnvironmentSourceHandle> live_handle_;
    };
}

#endif
