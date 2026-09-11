#ifndef KPENGINE_LIVE2D_RENDERER_H
#define KPENGINE_LIVE2D_RENDERER_H

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "asset/asset.h"
#include "graphics/backend/common/texture.h"
#include "graphics/backend/common/enum.h"
#include "live2d_render_planner.h"

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
        bool Record(render::FrameContext &frame_context,
                    graphics::CommandRecorder &recorder,
                    float delta_time,
                    std::string &diagnostic);
        // Recreates only the viewer-owned color target. The backend's
        // presentation/swapchain resize remains its own responsibility.
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
        graphics::RenderTargetView GetOutputView() const;
        void Cleanup() noexcept;

    private:
        bool CreateShadersAndPipelines(std::string &diagnostic);
        bool CreateGeometryAndTextures(std::string &diagnostic);
        graphics::PipelineHandle CreatePipeline(
            graphics::PipelineHandle vertex_fragment_source,
            bool masked, bool mask_source, bool culling,
            Live2DBlendMode blend_mode, std::string &diagnostic);
        void DestroyPipelines() noexcept;

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
        bool initialized_ = false;
        bool presentation_target_requested_ = false;
        std::array<float, 4> background_color_{0.1f, 0.1f, 0.1f, 1.0f};
        std::array<float, 4> output_clear_color_{0.1f, 0.1f, 0.1f, 1.0f};
    };
}

#endif
