#ifndef KPENGINE_RUNTIME_RENDER_PASSES_TEMPORAL_AA_PASS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_TEMPORAL_AA_PASS_H

#include <array>
#include <cstdint>
#include <memory>

#include "graphics/backend/common/render_backend.h"
#include "render/render_target.h"
#include "render/temporal_aa_settings.h"

namespace kpengine::render
{
    class FrameContext;
    class PreparedRenderAssetCatalog;
    class FullscreenPassResources;

    class TemporalAaPass final
    {
    public:
        bool PrepareResources(graphics::RenderBackend &backend,
                              const PreparedRenderAssetCatalog &assets);
        bool EnsureHistoryTargets(graphics::RenderBackend &backend,
                                  uint32_t width, uint32_t height);
        void Cleanup(graphics::RenderBackend &backend);
        void Invalidate(const char *reason) noexcept;
        void UpdateHistorySignature(uint64_t signature) noexcept;
        void CommitSubmittedFrame(bool submitted) noexcept;
        void SetSettings(TemporalAaSettings settings) noexcept;
        bool Record(FrameContext &frame_context, RenderTarget &scene_hdr,
                    RenderTarget &gbuffer, const FullscreenPassResources &fullscreen,
                    graphics::CommandRecorder &recorder, bool camera_history_valid,
                    Vector2f current_jitter);

        bool HistoryValid() const noexcept { return history_valid_; }
        uint32_t ReadIndex() const noexcept { return read_index_; }
        uint32_t WriteIndex() const noexcept { return 1u - read_index_; }
        RenderTarget *HistoryTarget(uint32_t index) noexcept;
        const TemporalAaSettings &Settings() const noexcept { return settings_; }
        const char *ResetReason() const noexcept { return reset_reason_; }
        uint64_t ResetCount() const noexcept { return reset_count_; }
        TextureFootprint HistoryFootprint() const;

    private:
        graphics::PipelineHandle pipeline_{};
        std::shared_ptr<const data::ShaderData> vertex_artifact_;
        std::shared_ptr<const data::ShaderData> fragment_artifact_;
        std::array<std::unique_ptr<RenderTarget>, 2> history_targets_;
        TemporalAaSettings settings_{};
        uint32_t read_index_ = 0;
        bool history_valid_ = false;
        bool pending_commit_ = false;
        const char *reset_reason_ = "uninitialized";
        uint64_t history_signature_ = 0;
        uint64_t reset_count_ = 0;
    };
}

#endif
