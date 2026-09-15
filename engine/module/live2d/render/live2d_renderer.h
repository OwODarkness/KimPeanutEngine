#ifndef KPENGINE_LIVE2D_RENDERER_H
#define KPENGINE_LIVE2D_RENDERER_H

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "asset/asset.h"
#include "graphics/backend/common/texture.h"
#include "graphics/backend/common/enum.h"
#include "live2d_render_planner.h"
#include "runtime/live2d_model_instance.h"
#include "runtime/live2d_model_resource.h"

namespace kpengine::asset
{
    class AssetManager;
}

namespace kpengine::graphics
{
    class CommandRecorder;
    class RenderBackend;
}

namespace kpengine::render
{
    class FrameContext;
}

namespace kpengine::live2d
{
    class Live2DModelInstance;
    class Live2DSystem;
    struct Live2DFrameInput;

    // First concrete Live2D renderer. It owns only Live2D GPU resources and
    // records a separate preview target through the generic submission API.
    class Live2DRenderer final
    {
    public:
        Live2DRenderer(Live2DSystem &system, asset::AssetID model_asset);
        ~Live2DRenderer();

        Live2DRenderer(const Live2DRenderer &) = delete;
        Live2DRenderer &operator=(const Live2DRenderer &) = delete;

        const char *GetName() const noexcept { return "Live2D"; }
        bool Initialize(graphics::RenderBackend &backend,
                        uint32_t width, uint32_t height,
                        std::string &diagnostic);
        // Viewer policy selects a motion and value-only frame input; the renderer
        // forwards both to the instance during frame recording.
        bool StartPreviewMotion(std::string_view group, uint32_t index,
                                int32_t priority, std::string &diagnostic);
        bool Record(render::FrameContext &frame_context,
                    graphics::CommandRecorder &recorder,
                    float delta_time,
                    std::string &diagnostic);
        bool Record(render::FrameContext &frame_context,
                    graphics::CommandRecorder &recorder,
                    float delta_time,
                    const Live2DFrameInput &frame_input,
                    bool advance_frame,
                    bool reset_parameters,
                    std::string &diagnostic);
        // Extra draws appended to the model's pass, after the model's own so that
        // they paint over it. They join that pass rather than getting one of
        // their own because both backends re-apply the target's clear on every
        // pass begin, so a second pass into the same target would erase the
        // model. The caller supplies generic draws, so this renderer learns
        // nothing about what they are.
        bool Record(render::FrameContext &frame_context,
                    graphics::CommandRecorder &recorder,
                    float delta_time,
                    const Live2DFrameInput &frame_input,
                    bool advance_frame,
                    bool reset_parameters,
                    const std::vector<render::SubmissionDraw> &extra_draws,
                    std::string &diagnostic);
        bool ResetParameters() noexcept;
        // Recreates only the viewer-owned color target. The backend's
        // presentation/swapchain resize remains its own responsibility. The
        // replacement is transactional: on failure the previously valid target
        // is left in place and the renderer stays usable.
        bool ResizeOutput(uint32_t width, uint32_t height,
                          std::string &diagnostic);
        void SetPresentationTarget(bool enabled) noexcept
        {
            presentation_target_requested_ = enabled;
            proxy_.output_to_presentation = enabled;
        }
        void SetBackgroundColor(const std::array<float, 4> &color) noexcept
        {
            background_color_ = color;
        }
        void SetOutputClearColor(const std::array<float, 4> &color) noexcept
        {
            output_clear_color_ = color;
        }
        graphics::RenderTargetHandle GetOutputTarget() const
        {
            return proxy_.output_target;
        }
        // Where the fitted model sits in the output, normalized so that the whole
        // output is one by one with y downward. It exists so a caller can aim
        // something at the model -- a speech bubble's tail, for one -- without
        // knowing anything about Live2D: nothing crosses this boundary but
        // numbers. Updated on every successful Record, and invalid until then.
        struct ModelBounds final
        {
            float min_x = 0.0f;
            float min_y = 0.0f;
            float max_x = 1.0f;
            float max_y = 1.0f;
            bool valid = false;
        };
        ModelBounds GetModelBounds() const noexcept { return model_bounds_; }
        graphics::RenderTargetView GetOutputView() const;
        // The format of the target this renderer draws into. A pipeline baking
        // its attachment format needs it, and asking is better than a caller
        // assuming the viewer's choice and being wrong when it changes.
        TextureFormat GetOutputColorFormat() const noexcept { return output_color_format_; }
        // Counts the GPU handles this renderer currently owns. Zero is the
        // shutdown contract; it is computed from the live handle table so it
        // cannot drift from the actual ownership set.
        std::uint32_t GetLiveGpuHandleCount() const noexcept;
        const Live2DRenderCounters &GetLastCounters() const noexcept
        {
            return last_counters_;
        }
        // The loaded model's authored feature counts. Blend mode is authored in
        // the .moc3 and cannot be recovered from a capture, so a blend-coverage
        // claim has to be read from here rather than inferred from an image.
        // Written once during Initialize and only read afterwards.
        Live2DBehaviorCapabilities GetBehaviorCapabilities() const noexcept;
        std::uint32_t GetLastBehaviorMask() const noexcept
        {
            return last_behavior_mask_;
        }
        const Live2DRenderFeatureReport &GetFeatureReport() const noexcept
        {
            return static_data_.feature_report;
        }
        std::uint64_t GetLastFrameSequence() const noexcept
        {
            return last_frame_sequence_;
        }
        std::uint64_t GetLastUpdateSequence() const noexcept
        {
            return last_update_sequence_;
        }
        std::size_t GetParameterCount() const noexcept
        {
            return instance_ != nullptr ? instance_->ParameterCount() : 0u;
        }
        void Cleanup() noexcept;

    private:
        bool CreateShadersAndPipelines(std::string &diagnostic);
        bool CreateGeometryAndTextures(std::string &diagnostic);
        graphics::PipelineHandle CreatePipeline(
            graphics::PipelineHandle vertex_fragment_source,
            bool masked, bool mask_source, bool culling,
            Live2DBlendMode blend_mode, std::string &diagnostic);
        void DestroyPipelines() noexcept;
        void DestroyGeometryAndTextures() noexcept;
        void DestroyRenderTargets() noexcept;

        Live2DSystem *system_ = nullptr;
        asset::AssetID model_asset_{};
        graphics::RenderBackend *backend_ = nullptr;
        std::unique_ptr<Live2DModelInstance> instance_;
        Live2DRenderResourceSet resources_;
        Live2DRenderProxy proxy_;
        Live2DStaticModelData static_data_;
        graphics::RenderTargetView output_view_{};
        TextureFormat output_color_format_ =
            TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        std::vector<graphics::PipelineHandle> pipelines_;
        std::vector<graphics::TextureHandle> textures_;
        Live2DRenderCounters last_counters_{};
        ModelBounds model_bounds_{};
        std::uint64_t last_frame_sequence_ = 0u;
        std::uint64_t last_update_sequence_ = 0u;
        bool has_last_frame_sequence_ = false;
        std::uint32_t last_behavior_mask_ = 0u;
        bool initialized_ = false;
        bool preview_motion_enabled_ = false;
        std::string preview_motion_group_;
        uint32_t preview_motion_index_ = 0u;
        int32_t preview_motion_priority_ = 1;

        bool presentation_target_requested_ = false;
        std::array<float, 4> background_color_{0.1f, 0.1f, 0.1f, 1.0f};
        std::array<float, 4> output_clear_color_{0.1f, 0.1f, 0.1f, 1.0f};
    };
}

#endif
