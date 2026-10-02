#ifndef KPENGINE_EDITOR_IMGUI_RHI_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_RHI_RENDERER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "data/shader.h"
#include "editor/settings/editor_settings.h"
#include "editor/ui/editor_ui_frame_packet.h"
#include "graphics/backend/common/editor_presentation_bridge.h"
#include "graphics/backend/common/render_target.h"
#include "graphics/backend/common/resource_binding.h"
#include "graphics/backend/common/sampler.h"
#include "graphics/backend/common/texture.h"

namespace kpengine::graphics
{
    class RenderBackend;
}

namespace kpengine::editor
{
    class EditorImguiRhiRenderer final
    {
    public:
        bool Initialize(graphics::IEditorPresentationBridge *bridge);
        void Shutdown();
        void SetBloomShaders(const data::ShaderData *vertex,
                            const data::ShaderData *fragment);
        void SetBackgroundColor(const LogColor &color);
        void RegisterTexture(ImTextureID id, const graphics::RenderTargetView &view);
        bool Render(ImDrawData *draw_data, const std::array<float, 4> &clear_color,
                    std::string *diagnostic = nullptr);

    private:
        void ClearBindingCache(graphics::RenderBackend &backend);
        bool RenderFrame(ImDrawData *draw_data, EditorUiFramePacket &packet,
                         const std::array<float, 4> &clear_color,
                         graphics::RenderBackend &backend,
                         graphics::CommandRecorder &recorder,
                         std::string *diagnostic);

        struct TextureRegistration
        {
            graphics::TextureHandle texture;
            graphics::SamplerHandle sampler;
            graphics::TextureOrigin origin = graphics::TextureOrigin::TopLeft;
        };

        struct BindingCacheEntry
        {
            graphics::PipelineHandle pipeline;
            graphics::TextureHandle texture;
            graphics::BufferHandle options_buffer;
            size_t options_offset = 0;
            graphics::DescriptorSetHandle bindings;
        };

        graphics::IEditorPresentationBridge *bridge_ = nullptr;
        graphics::RenderBackend *backend_ = nullptr;
        std::unique_ptr<data::ShaderData> vertex_shader_;
        std::unique_ptr<data::ShaderData> fragment_shader_;
        const data::ShaderData *vertex_shader_identity_ = nullptr;
        const data::ShaderData *fragment_shader_identity_ = nullptr;
        std::unordered_map<uint64_t, TextureRegistration> textures_;
        std::vector<BindingCacheEntry> binding_cache_;
        graphics::TextureHandle font_texture_;
        graphics::SamplerHandle sampler_;
        graphics::PipelineHandle ui_pipeline_;
        graphics::PipelineHandle emission_pipeline_;
        graphics::PipelineHandle blur_pipeline_;
        graphics::PipelineHandle copy_pipeline_;
        graphics::PipelineHandle additive_pipeline_;
        graphics::PipelineHandle presentation_pipeline_;
        TextureFormat presentation_format_ = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        graphics::BufferHandle vertex_buffer_;
        graphics::BufferHandle index_buffer_;
        graphics::BufferHandle options_buffer_;
        void *mapped_options_ = nullptr;
        size_t options_capacity_ = 0;
        size_t options_cursor_ = 0;
        uint32_t options_frame_index_ = 0xffffffffu;
        std::vector<size_t> options_frame_cursors_;
        graphics::RenderTargetHandle color_canvas_;
        graphics::RenderTargetHandle emission_source_;
        graphics::RenderTargetHandle blur_horizontal_;
        graphics::RenderTargetHandle blur_vertical_;
        uint32_t target_width_ = 0;
        uint32_t target_height_ = 0;
        uint32_t vertex_capacity_ = 0;
        uint32_t index_capacity_ = 0;
        std::array<float, 4> background_color_{};
        std::string last_diagnostic_;
        bool ready_ = false;
    };
}

#endif
