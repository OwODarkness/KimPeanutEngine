#ifndef KPENGINE_RUNTIME_RENDER_CAMERA_SOURCE_REGISTRY_H
#define KPENGINE_RUNTIME_RENDER_CAMERA_SOURCE_REGISTRY_H

#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "render/camera_source.h"

namespace kpengine::render
{
    // Render-owned inbox and active-camera selector. Gameplay may enqueue
    // copied source values from the game thread; Drain runs on the render
    // thread before any view-dependent work.
    class CameraSourceRegistry final : public ICameraSourceSink
    {
    public:
        CameraSourceHandle EnqueueCreate(const CameraSourceDesc &source) override;
        bool EnqueueUpdate(CameraSourceHandle handle,
                           const CameraSourceDesc &source) override;
        bool EnqueueDestroy(CameraSourceHandle handle) override;

        void Drain();
        void Clear();

        // Returns a copied descriptor selected during the last Drain. The
        // caller must be on the render side after Drain has completed.
        std::optional<CameraSourceDesc> GetActiveSource() const { return active_source_; }

    private:
        struct CreateCommand
        {
            CameraSourceHandle handle;
            CameraSourceDesc source;
        };
        struct UpdateCommand
        {
            CameraSourceHandle handle;
            CameraSourceDesc source;
        };
        struct DestroyCommand
        {
            CameraSourceHandle handle;
        };
        using Command = std::variant<CreateCommand, UpdateCommand, DestroyCommand>;

        struct SourceRecord
        {
            CameraSourceHandle handle;
            CameraSourceDesc source;
        };

        void ApplyCommands(std::vector<Command> commands);
        void SelectActiveSource();

        mutable std::mutex command_mutex_;
        HandleSystem<CameraSourceHandle> handles_;
        std::vector<Command> pending_commands_;
        std::unordered_map<uint32_t, SourceRecord> records_;
        std::optional<CameraSourceDesc> active_source_;
    };
}

#endif
