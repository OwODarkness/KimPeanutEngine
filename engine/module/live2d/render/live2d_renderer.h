#ifndef KPENGINE_LIVE2D_RENDERER_H
#define KPENGINE_LIVE2D_RENDERER_H

#include <memory>
#include <string>
#include <vector>

#include "asset/asset.h"
#include "graphics/backend/common/texture.h"
#include "graphics/backend/common/enum.h"
#include "render/render_extension.h"
#include "live2d_render_planner.h"

namespace kpengine::asset
{
    class AssetManager;
}

namespace kpengine::live2d
{
    class Live2DModelInstance;
    class Live2DSystem;

    // First concrete Live2D renderer. It owns only Live2D GPU resources and
    // records a separate preview target through the generic submission API.
    class Live2DRenderer final : public render::IRenderExtension
    {
    public:
        Live2DRenderer(Live2DSystem &system, asset::AssetID model_asset);
        ~Live2DRenderer() override;

        Live2DRenderer(const Live2DRenderer &) = delete;
        Live2DRenderer &operator=(const Live2DRenderer &) = delete;

        const char *GetName() const noexcept override { return "Live2D"; }
        bool Initialize(graphics::RenderBackend &backend,
                        uint32_t width, uint32_t height,
                        std::string &diagnostic) override;
        bool Record(render::FrameContext &frame_context,
                    graphics::CommandRecorder &recorder,
                    float delta_time,
                    std::string &diagnostic) override;
        void SetPresentationTarget(bool enabled) noexcept
        {
            presentation_target_requested_ = enabled;
            proxy_.output_to_presentation = enabled;
        }
        graphics::RenderTargetHandle GetOutputTarget() const override
        {
            return proxy_.output_target;
        }
        graphics::RenderTargetView GetOutputView() const override;
        void Cleanup() noexcept override;

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
    };
}

#endif
