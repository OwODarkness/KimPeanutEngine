#include "editor/platform/editor_imgui_rhi_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <bit>
#include <limits>
#include <span>
#include <unordered_map>

#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/pipeline_types.h"
#include "graphics/backend/common/descriptor_types.h"
#include "editor/settings/editor_settings.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr uint32_t kMaxGlowRegions = 8;
        constexpr size_t kMaxCachedBindings = 512;
        constexpr uint32_t kMaxUiVertices = 1u << 20;
        constexpr uint32_t kMaxUiIndices = 1u << 22;

        struct alignas(16) UiOptions
        {
            float projection[16]{};
            float options[4]{};
            float tint[4]{};
        };

        struct GlowRegion
        {
            uint32_t id = 0;
            float bounds[4]{};
            std::vector<uint32_t> emission_draws;
        };

        float SrgbToLinear(float value)
        {
            return value <= 0.04045f ? value / 12.92f
                                      : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        float LinearToSrgb(float value)
        {
            value = std::max(0.0f, value);
            return value <= 0.0031308f ? value * 12.92f
                                       : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        UiOptions MakeOptions(const ImDrawData &draw_data,
                              const float mode, const float option_y = 0.0f,
                              const float option_z = 0.0f, const float option_w = 0.0f)
        {
            UiOptions result{};
            const float left = draw_data.DisplayPos.x;
            const float top = draw_data.DisplayPos.y;
            const float right = left + draw_data.DisplaySize.x;
            const float bottom = top + draw_data.DisplaySize.y;
            result.projection[0] = 2.0f / (right - left);
            // The RHI recorder already normalizes Vulkan's viewport orientation.
            result.projection[5] = -2.0f / (bottom - top);
            result.projection[10] = 1.0f;
            result.projection[12] = -(right + left) / (right - left);
            result.projection[13] = (bottom + top) / (bottom - top);
            result.projection[15] = 1.0f;
            result.options[0] = mode;
            result.options[1] = option_y;
            result.options[2] = option_z;
            result.options[3] = option_w;
            result.tint[0] = result.tint[1] = result.tint[2] = 1.0f;
            result.tint[3] = 1.0f;
            return result;
        }

        UiOptions MakeFullscreenOptions(const float mode, const float option_y = 0.0f,
                                        const float option_z = 0.0f,
                                        const float option_w = 0.0f)
        {
            UiOptions result{};
            result.projection[0] = 1.0f;
            result.projection[5] = 1.0f;
            result.projection[10] = 1.0f;
            result.projection[15] = 1.0f;
            result.options[0] = mode;
            result.options[1] = option_y;
            result.options[2] = option_z;
            result.options[3] = option_w;
            result.tint[0] = result.tint[1] = result.tint[2] = 1.0f;
            result.tint[3] = 1.0f;
            return result;
        }

        graphics::RenderTargetDesc MakeTargetDesc(
            uint32_t width, uint32_t height, const std::array<float, 4> &clear)
        {
            graphics::RenderTargetDesc desc{};
            desc.width = std::max(1u, width);
            desc.height = std::max(1u, height);
            desc.color_attachments.push_back({TextureFormat::TEXTURE_FORMAT_RGBA16F,
                graphics::RenderTargetLoadOp::Clear,
                graphics::RenderTargetStoreOp::Store, clear});
            return desc;
        }

        void ConfigureBlend(graphics::PipelineDesc &desc, bool enabled,
                            graphics::BlendFactor src_color,
                            graphics::BlendFactor dst_color,
                            graphics::BlendFactor src_alpha,
                            graphics::BlendFactor dst_alpha)
        {
            desc.blend_attachment_state.blend_enabled = enabled;
            desc.blend_attachment_state.src_color_blend_factor = src_color;
            desc.blend_attachment_state.dst_color_blend_factor = dst_color;
            desc.blend_attachment_state.src_alpha_blend_factor = src_alpha;
            desc.blend_attachment_state.dst_alpha_blend_factor = dst_alpha;
        }

        graphics::Scissor MakeScissor(const float clip[4], const float bounds[4],
                                      const ImDrawData &draw_data,
                                      uint32_t width, uint32_t height,
                                      GraphicsAPIType api)
        {
            const float min_x = std::max({(clip[0] - draw_data.DisplayPos.x) *
                                              draw_data.FramebufferScale.x,
                                          (bounds[0] - draw_data.DisplayPos.x) *
                                              draw_data.FramebufferScale.x, 0.0f});
            const float min_y = std::max({(clip[1] - draw_data.DisplayPos.y) *
                                              draw_data.FramebufferScale.y,
                                          (bounds[1] - draw_data.DisplayPos.y) *
                                              draw_data.FramebufferScale.y, 0.0f});
            const float max_x = std::min({(clip[2] - draw_data.DisplayPos.x) *
                                              draw_data.FramebufferScale.x,
                                          (bounds[2] - draw_data.DisplayPos.x) *
                                              draw_data.FramebufferScale.x,
                                          static_cast<float>(width)});
            const float max_y = std::min({(clip[3] - draw_data.DisplayPos.y) *
                                              draw_data.FramebufferScale.y,
                                          (bounds[3] - draw_data.DisplayPos.y) *
                                              draw_data.FramebufferScale.y,
                                          static_cast<float>(height)});
            const int32_t left = static_cast<int32_t>(std::floor(min_x));
            const int32_t top = static_cast<int32_t>(std::floor(min_y));
            const int32_t right = static_cast<int32_t>(std::ceil(max_x));
            const int32_t bottom = static_cast<int32_t>(std::ceil(max_y));
            if (right <= left || bottom <= top)
            {
                return {0, 0, 0, 0};
            }
            const int32_t y = api == GraphicsAPIType::GRAPHICS_API_OPENGL
                                  ? static_cast<int32_t>(height) - bottom : top;
            return {left, y, static_cast<uint32_t>(right - left),
                    static_cast<uint32_t>(bottom - top)};
        }

        bool AppendSpan(std::vector<std::byte> &storage, const void *source, size_t size)
        {
            if (size == 0 || source == nullptr ||
                size > std::numeric_limits<size_t>::max() - storage.size())
            {
                return false;
            }
            const size_t old_size = storage.size();
            storage.resize(old_size + size);
            std::memcpy(storage.data() + old_size, source, size);
            return true;
        }
    }

    bool EditorImguiRhiRenderer::Initialize(graphics::IEditorPresentationBridge *bridge)
    {
        bridge_ = bridge;
        return bridge_ != nullptr;
    }

    void EditorImguiRhiRenderer::ClearBindingCache(graphics::RenderBackend &backend)
    {
        // Call only after GPU retirement and before recording new UI commands.
        for (const BindingCacheEntry &entry : binding_cache_)
        {
            if (entry.bindings.IsValid())
            {
                backend.DestroyResourceBindingSet(entry.bindings);
            }
        }
        binding_cache_.clear();
    }

    void EditorImguiRhiRenderer::Shutdown()
    {
        if (backend_ != nullptr)
        {
            backend_->WaitIdle();
            ClearBindingCache(*backend_);
            for (const graphics::PipelineHandle pipeline :
                 {ui_pipeline_, emission_pipeline_, blur_pipeline_, copy_pipeline_,
                  additive_pipeline_, presentation_pipeline_})
            {
                if (pipeline.IsValid())
                {
                    backend_->DestroyPipelineResource(pipeline);
                }
            }
            if (vertex_buffer_.IsValid()) backend_->DestroyBufferResource(vertex_buffer_);
            if (index_buffer_.IsValid()) backend_->DestroyBufferResource(index_buffer_);
            if (options_buffer_.IsValid()) backend_->DestroyBufferResource(options_buffer_);
            if (font_texture_.IsValid()) backend_->DestroyTexture(font_texture_);
            if (sampler_.IsValid()) backend_->DestroySampler(sampler_);
        }
        textures_.clear();
        vertex_buffer_ = {};
        index_buffer_ = {};
        options_buffer_ = {};
        font_texture_ = {};
        sampler_ = {};
        ui_pipeline_ = {};
        emission_pipeline_ = {};
        blur_pipeline_ = {};
        copy_pipeline_ = {};
        additive_pipeline_ = {};
        presentation_pipeline_ = {};
        presentation_format_ = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        color_canvas_ = {};
        emission_source_ = {};
        blur_horizontal_ = {};
        blur_vertical_ = {};
        mapped_options_ = nullptr;
        backend_ = nullptr;
        vertex_shader_identity_ = nullptr;
        fragment_shader_identity_ = nullptr;
        target_width_ = target_height_ = 0;
        ready_ = false;
        bridge_ = nullptr;
    }

    void EditorImguiRhiRenderer::SetBloomShaders(const data::ShaderData *vertex,
                                                 const data::ShaderData *fragment)
    {
        if (vertex == nullptr || fragment == nullptr ||
            vertex->stage != ShaderStage::SHADER_STAGE_VERTEX ||
            fragment->stage != ShaderStage::SHADER_STAGE_FRAGMENT ||
            vertex->api != fragment->api)
        {
            return;
        }
        if (vertex == vertex_shader_identity_ && fragment == fragment_shader_identity_)
        {
            return;
        }
        vertex_shader_ = std::make_unique<data::ShaderData>(*vertex);
        fragment_shader_ = std::make_unique<data::ShaderData>(*fragment);
        vertex_shader_identity_ = vertex;
        fragment_shader_identity_ = fragment;
    }

    void EditorImguiRhiRenderer::SetBackgroundColor(const LogColor &color)
    {
        background_color_ = {SrgbToLinear(color.r), SrgbToLinear(color.g),
                             SrgbToLinear(color.b), color.a};
    }

    void EditorImguiRhiRenderer::RegisterTexture(
        ImTextureID id, const graphics::RenderTargetView &view)
    {
        if (id == ImTextureID{} || !view.IsValid() || !view.sampled_color.IsValid())
        {
            return;
        }
        textures_[static_cast<uint64_t>(id)] =
            {view.sampled_color, sampler_, view.origin};
    }

    bool EditorImguiRhiRenderer::Render(ImDrawData *draw_data,
                                        const std::array<float, 4> &clear_color,
                                        std::string *diagnostic)
    {
        (void)clear_color;
        if (bridge_ == nullptr || vertex_shader_ == nullptr || fragment_shader_ == nullptr ||
            draw_data == nullptr || draw_data->DisplaySize.x <= 0.0f ||
            draw_data->DisplaySize.y <= 0.0f)
        {
            return false;
        }

        EditorUiFramePacket packet;
        std::string local_diagnostic;
        if (!BuildEditorUiFramePacket(draw_data, packet, &local_diagnostic) ||
            packet.requires_legacy_fallback)
        {
            if (diagnostic != nullptr)
            {
                *diagnostic = local_diagnostic.empty()
                                  ? "ImGui frame requires an unsupported callback"
                                  : std::move(local_diagnostic);
            }
            return false;
        }

        const bool rendered = bridge_->ExecuteRhiFrame(
            [this, draw_data, &packet, &clear_color, diagnostic](
                graphics::RenderBackend &backend, graphics::CommandRecorder &recorder)
            {
                backend_ = &backend;
                return RenderFrame(draw_data, packet, clear_color, backend,
                                   recorder, diagnostic);
            });
        if (rendered)
        {
            return true;
        }
        return false;
    }

    bool EditorImguiRhiRenderer::RenderFrame(
        ImDrawData *draw_data, EditorUiFramePacket &packet,
        const std::array<float, 4> &clear_color, graphics::RenderBackend &backend,
        graphics::CommandRecorder &recorder, std::string *diagnostic)
    {
        const auto fail = [diagnostic](const char *message)
        {
            if (diagnostic != nullptr) *diagnostic = message;
            return false;
        };
        if (draw_data == nullptr || vertex_shader_ == nullptr || fragment_shader_ == nullptr ||
            vertex_shader_->api != backend.GetGraphicsAPI() ||
            fragment_shader_->api != backend.GetGraphicsAPI())
        {
            return fail("Editor bloom shader artifacts do not match the active backend");
        }

        const uint32_t width = static_cast<uint32_t>(std::ceil(
            draw_data->DisplaySize.x * draw_data->FramebufferScale.x));
        const uint32_t height = static_cast<uint32_t>(std::ceil(
            draw_data->DisplaySize.y * draw_data->FramebufferScale.y));
        if (width == 0 || height == 0 || width > 8192 || height > 8192)
        {
            return fail("Editor bloom viewport extent is outside the supported budget");
        }

        if (binding_cache_.size() >= kMaxCachedBindings)
        {
            backend.WaitIdle();
            ClearBindingCache(backend);
        }

        struct EmissionDraw
        {
            uint32_t command_index = 0;
        };
        struct Region
        {
            EditorGlowMarker marker{};
            std::vector<EmissionDraw> draws;
        };
        std::vector<Region> regions;
        std::unordered_map<size_t, size_t> region_end_items;
        size_t active_region = std::numeric_limits<size_t>::max();
        std::vector<EditorGlowMarker> emission_stack;
        for (size_t item_index = 0; item_index < packet.ordered_items.size(); ++item_index)
        {
            const EditorUiFrameItem &item = packet.ordered_items[item_index];
            if (item.kind == EditorUiFrameItem::Kind::GlowMarker)
            {
                const EditorGlowMarker &marker = item.glow_marker;
                if (marker.kind == EditorGlowMarkerKind::BeginRegion)
                {
                    if (regions.size() >= kMaxGlowRegions)
                    {
                        return fail("Editor bloom region budget exceeded");
                    }
                    regions.push_back({marker, {}});
                    active_region = regions.size() - 1;
                }
                else if (marker.kind == EditorGlowMarkerKind::EndRegion)
                {
                    if (active_region >= regions.size())
                    {
                        return fail("Editor bloom region marker is unmatched");
                    }
                    region_end_items.emplace(item_index, active_region);
                    active_region = std::numeric_limits<size_t>::max();
                }
                else if (marker.kind == EditorGlowMarkerKind::PushEmission)
                {
                    emission_stack.push_back(marker);
                }
                else if (marker.kind == EditorGlowMarkerKind::PopEmission)
                {
                    if (emission_stack.empty())
                    {
                        return fail("Editor bloom emission marker is unmatched");
                    }
                    emission_stack.pop_back();
                }
                continue;
            }
            if (item.kind != EditorUiFrameItem::Kind::Draw || emission_stack.empty())
            {
                continue;
            }
            if (active_region >= regions.size() ||
                item.draw_command_index >= packet.draw_commands.size())
            {
                return fail("Editor bloom draw escaped its region");
            }

            const EditorUiDrawCommand &source = packet.draw_commands[item.draw_command_index];
            EditorUiDrawCommand emission = source;
            emission.first_index = static_cast<uint32_t>(packet.indices.size());
            for (uint32_t index = 0; index < source.index_count; ++index)
            {
                const uint32_t source_index = packet.indices[source.first_index + index];
                if (source_index >= packet.vertices.size() ||
                    packet.vertices.size() >= kMaxUiVertices ||
                    packet.indices.size() >= kMaxUiIndices)
                {
                    return fail("Editor bloom geometry exceeds the frame packet budget");
                }
                EditorUiVertex vertex = packet.vertices[source_index];
                const EditorGlowMarker &style = emission_stack.back();
                vertex.color[0] = LinearToSrgb(style.linear_tint[0] * style.strength);
                vertex.color[1] = LinearToSrgb(style.linear_tint[1] * style.strength);
                vertex.color[2] = LinearToSrgb(style.linear_tint[2] * style.strength);
                const uint32_t new_index = static_cast<uint32_t>(packet.vertices.size());
                packet.vertices.push_back(vertex);
                packet.indices.push_back(new_index);
            }
            emission.index_count = source.index_count;
            const uint32_t emission_index = static_cast<uint32_t>(packet.draw_commands.size());
            packet.draw_commands.push_back(emission);
            regions[active_region].draws.push_back({emission_index});
        }
        if (active_region != std::numeric_limits<size_t>::max() || !emission_stack.empty())
        {
            return fail("Editor bloom marker stream ended within a scope");
        }

        const size_t full_pixels = static_cast<size_t>(width) * height;
        const uint32_t half_width = std::max(1u, (width + 1u) / 2u);
        const uint32_t half_height = std::max(1u, (height + 1u) / 2u);
        const size_t half_pixels = static_cast<size_t>(half_width) * half_height;
        constexpr size_t kBytesPerRgba16fPixel = 8;
        constexpr size_t kMaximumBloomBytes = 128u * 1024u * 1024u;
        const size_t estimated_bytes = (full_pixels * (1u + (regions.empty() ? 0u : 1u)) +
            half_pixels * (2u + regions.size())) * kBytesPerRgba16fPixel;
        if (estimated_bytes > kMaximumBloomBytes)
        {
            return fail("Editor bloom render-target memory budget exceeded");
        }

        auto create_pipeline = [&](TextureFormat format, bool blend,
                                   graphics::BlendFactor src_color,
                                   graphics::BlendFactor dst_color,
                                   graphics::BlendFactor src_alpha,
                                   graphics::BlendFactor dst_alpha)
        {
            graphics::PipelineDesc desc{};
            desc.vert_shader = vertex_shader_.get();
            desc.frag_shader = fragment_shader_.get();
            desc.color_attachment_formats = {format};
            desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
            desc.binding_descs = {{0, sizeof(EditorUiVertex), false}};
            desc.attri_descs = {
                {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
                 static_cast<uint32_t>(offsetof(EditorUiVertex, position))},
                {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
                 static_cast<uint32_t>(offsetof(EditorUiVertex, uv))},
                {2, 0, graphics::VertexFormat::VERTEX_FORMAT_FOUR_FLOATS,
                 static_cast<uint32_t>(offsetof(EditorUiVertex, color))},
            };
            desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_NONE;
            desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
            desc.descriptor_binding_descs = {{
                {0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                 ShaderStage::SHADER_STAGE_VERTEX_FRAGMENT},
                {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                 ShaderStage::SHADER_STAGE_FRAGMENT},
            }};
            ConfigureBlend(desc, blend, src_color, dst_color, src_alpha, dst_alpha);
            return backend.CreatePipelineResource(desc);
        };

        if (!ui_pipeline_.IsValid())
        {
            ui_pipeline_ = create_pipeline(TextureFormat::TEXTURE_FORMAT_RGBA16F, true,
                graphics::BlendFactor::BLEND_FACTOR_SRC_ALPHA,
                graphics::BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
        }
        if (!emission_pipeline_.IsValid())
        {
            emission_pipeline_ = create_pipeline(TextureFormat::TEXTURE_FORMAT_RGBA16F, true,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ONE);
        }
        if (!blur_pipeline_.IsValid())
        {
            blur_pipeline_ = create_pipeline(TextureFormat::TEXTURE_FORMAT_RGBA16F, false,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ZERO,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ZERO);
        }
        if (!copy_pipeline_.IsValid())
        {
            copy_pipeline_ = create_pipeline(TextureFormat::TEXTURE_FORMAT_RGBA16F, false,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ZERO,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ZERO);
        }
        if (!additive_pipeline_.IsValid())
        {
            additive_pipeline_ = create_pipeline(TextureFormat::TEXTURE_FORMAT_RGBA16F, true,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ZERO,
                graphics::BlendFactor::BLEND_FACTOR_ONE);
        }
        const TextureFormat active_presentation_format = backend.GetPresentationColorFormat();
        if (presentation_format_ != active_presentation_format && presentation_pipeline_.IsValid())
        {
            backend.WaitIdle();
            ClearBindingCache(backend);
            backend.DestroyPipelineResource(presentation_pipeline_);
            presentation_pipeline_ = {};
        }
        if (!presentation_pipeline_.IsValid())
        {
            presentation_pipeline_ = create_pipeline(active_presentation_format, false,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ZERO,
                graphics::BlendFactor::BLEND_FACTOR_ONE,
                graphics::BlendFactor::BLEND_FACTOR_ZERO);
            presentation_format_ = active_presentation_format;
        }
        if (!ui_pipeline_.IsValid() || !emission_pipeline_.IsValid() ||
            !blur_pipeline_.IsValid() || !copy_pipeline_.IsValid() ||
            !additive_pipeline_.IsValid() || !presentation_pipeline_.IsValid())
        {
            return fail("Editor bloom pipeline creation failed");
        }

        if (!sampler_.IsValid())
        {
            graphics::SamplerSettings settings{};
            settings.mag_filter = graphics::SamplerFilterType::SAMPLER_FILTER_LINEAR;
            settings.min_filter = graphics::SamplerFilterType::SAMPLER_FILTER_LINEAR;
            settings.mipmap_mode = graphics::SamplerMipmapMode::None;
            settings.address_mode_u = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            settings.address_mode_v = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            settings.address_mode_w = graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            settings.enable_anisotropy = false;
            sampler_ = backend.CreateSampler(settings);
            if (!sampler_.IsValid()) return fail("Editor bloom sampler creation failed");
        }
        if (!font_texture_.IsValid())
        {
            unsigned char *pixels = nullptr;
            int font_width = 0;
            int font_height = 0;
            ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &font_width, &font_height);
            if (pixels == nullptr || font_width <= 0 || font_height <= 0)
            {
                return fail("Editor font atlas pixels are unavailable");
            }
            data::TextureData atlas{};
            atlas.width = static_cast<uint32_t>(font_width);
            atlas.height = static_cast<uint32_t>(font_height);
            atlas.format = TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM;
            atlas.semantic = data::TextureSemantic::OpacityMask;
            atlas.pixels.assign(pixels, pixels +
                static_cast<size_t>(font_width) * static_cast<size_t>(font_height) * 4u);
            graphics::TextureSettings settings{};
            settings.format = atlas.format;
            settings.usage = graphics::TextureUsage::TEXTURE_USAGE_SAMPLE;
            settings.mip_levels = 1;
            font_texture_ = backend.CreateTexture(atlas, settings);
            if (!font_texture_.IsValid()) return fail("Editor font atlas upload failed");
        }
        const uint64_t font_key = static_cast<uint64_t>(ImGui::GetIO().Fonts->TexID);
        if (font_key != 0)
        {
            textures_[font_key] = {font_texture_, sampler_, graphics::TextureOrigin::TopLeft};
        }
        for (auto &[id, registration] : textures_)
        {
            (void)id;
            registration.sampler = sampler_;
        }

        for (const EditorUiDrawCommand &command : packet.draw_commands)
        {
            if (textures_.find(command.texture_id) == textures_.end())
            {
                return fail("Editor UI texture ID is not registered with the RHI adapter");
            }
        }
        for (const EditorUiDrawCommand &command : packet.draw_commands)
        {
            const auto texture = textures_.find(command.texture_id);
            if (texture == textures_.end() ||
                texture->second.origin != graphics::TextureOrigin::BottomLeft)
            {
                continue;
            }
            for (uint32_t index = 0; index < command.index_count; ++index)
            {
                const size_t packet_index =
                    static_cast<size_t>(command.first_index) + index;
                const uint32_t source_vertex = packet.indices[packet_index];
                if (source_vertex >= packet.vertices.size() ||
                    packet.vertices.size() >= kMaxUiVertices)
                {
                    return fail("Editor UI texture-origin remap exceeds the frame budget");
                }
                EditorUiVertex remapped = packet.vertices[source_vertex];
                remapped.uv[1] = 1.0f - remapped.uv[1];
                packet.indices[packet_index] =
                    static_cast<uint32_t>(packet.vertices.size());
                packet.vertices.push_back(remapped);
            }
        }

        const uint32_t full_vertex_capacity = static_cast<uint32_t>(
            std::min<size_t>(kMaxUiVertices, packet.vertices.size() + 4u));
        const uint32_t full_index_capacity = static_cast<uint32_t>(
            std::min<size_t>(kMaxUiIndices, packet.indices.size() + 6u));
        if (packet.vertices.size() + 4u > kMaxUiVertices ||
            packet.indices.size() + 6u > kMaxUiIndices)
        {
            return fail("Editor UI geometry exceeds the frame packet budget");
        }

        const uint32_t quad_vertex_base = static_cast<uint32_t>(packet.vertices.size());
        const uint32_t quad_first_index = static_cast<uint32_t>(packet.indices.size());
        const bool top_left_target = backend.GetGraphicsAPI() == GraphicsAPIType::GRAPHICS_API_VULKAN;
        const float bottom_v = top_left_target ? 1.0f : 0.0f;
        const float top_v = top_left_target ? 0.0f : 1.0f;
        const std::array<EditorUiVertex, 4> quad{{
            {{-1.0f, -1.0f}, {0.0f, bottom_v}, {1.0f, 1.0f, 1.0f, 1.0f}},
            {{ 1.0f, -1.0f}, {1.0f, bottom_v}, {1.0f, 1.0f, 1.0f, 1.0f}},
            {{ 1.0f,  1.0f}, {1.0f, top_v}, {1.0f, 1.0f, 1.0f, 1.0f}},
            {{-1.0f,  1.0f}, {0.0f, top_v}, {1.0f, 1.0f, 1.0f, 1.0f}},
        }};
        packet.vertices.insert(packet.vertices.end(), quad.begin(), quad.end());
        packet.indices.insert(packet.indices.end(), {quad_vertex_base, quad_vertex_base + 1,
            quad_vertex_base + 2, quad_vertex_base, quad_vertex_base + 2, quad_vertex_base + 3});

        const size_t vertex_bytes = packet.vertices.size() * sizeof(EditorUiVertex);
        const size_t index_bytes = packet.indices.size() * sizeof(uint32_t);
        const auto ensure_geometry_buffer = [&](graphics::BufferHandle &handle,
                                                uint32_t &capacity, size_t needed,
                                                graphics::BufferRole role)
        {
            if (needed > std::numeric_limits<uint32_t>::max()) return false;
            if (needed > capacity)
            {
                backend.WaitIdle();
                if (handle.IsValid()) backend.DestroyBufferResource(handle);
                const uint32_t new_capacity = std::bit_ceil(
                    std::max<uint32_t>(static_cast<uint32_t>(needed), 4096u));
                graphics::BufferDesc desc{};
                desc.role = role;
                desc.update_mode = graphics::BufferUpdateMode::PerFrame;
                desc.capacity_bytes = new_capacity;
                handle = backend.CreateBuffer(desc, {});
                capacity = handle.IsValid() ? new_capacity : 0u;
            }
            return handle.IsValid();
        };
        if (!ensure_geometry_buffer(vertex_buffer_, vertex_capacity_, vertex_bytes,
                                    graphics::BufferRole::Vertex) ||
            !ensure_geometry_buffer(index_buffer_, index_capacity_, index_bytes,
                                    graphics::BufferRole::Index))
        {
            return fail("Editor UI per-frame geometry buffers are unavailable");
        }
        if (!backend.WriteFrameBuffer(vertex_buffer_, 0,
                std::as_bytes(std::span(packet.vertices))) ||
            !backend.WriteFrameBuffer(index_buffer_, 0,
                std::as_bytes(std::span(packet.indices))))
        {
            return fail("Editor UI per-frame geometry upload failed");
        }

        const size_t alignment = std::max<size_t>(backend.GetUniformBufferAlignment(), 16u);
        const size_t option_stride = (sizeof(UiOptions) + alignment - 1u) / alignment * alignment;
        const size_t kOptionCount = 6u + regions.size() * 2u;
        const size_t frame_slice = option_stride * kOptionCount;
        const size_t uniform_capacity = frame_slice * std::max(1u, backend.GetFramesInFlight());
        if (uniform_capacity > std::numeric_limits<uint32_t>::max())
        {
            return fail("Editor UI uniform buffer exceeds the backend limit");
        }
        if (!options_buffer_.IsValid() || options_capacity_ < uniform_capacity)
        {
            backend.WaitIdle();
            ClearBindingCache(backend);
            if (options_buffer_.IsValid()) backend.DestroyBufferResource(options_buffer_);
            options_buffer_ = backend.CreateUniformBuffer(static_cast<uint32_t>(uniform_capacity));
            options_capacity_ = options_buffer_.IsValid() ? uniform_capacity : 0;
            mapped_options_ = options_buffer_.IsValid()
                                  ? backend.MapUniformBuffer(options_buffer_, options_capacity_)
                                  : nullptr;
            if (!options_buffer_.IsValid() || mapped_options_ == nullptr)
            {
                return fail("Editor UI uniform buffer mapping failed");
            }
        }
        const uint32_t frame_index = backend.GetCurrentFrameIndex();
        if (frame_index >= std::max(1u, backend.GetFramesInFlight()))
        {
            return fail("Editor UI active frame index is invalid");
        }
        const size_t frame_base = static_cast<size_t>(frame_index) * frame_slice;
        const auto write_options = [&](size_t slot, const UiOptions &options)
        {
            const size_t offset = frame_base + slot * option_stride;
            std::memcpy(static_cast<std::byte *>(mapped_options_) + offset,
                        &options, sizeof(options));
            backend.MarkUniformBufferRangeWritten(options_buffer_, offset, sizeof(options));
            return offset;
        };
        const UiOptions base_options = MakeOptions(*draw_data, 0.0f);
        const UiOptions emission_options = MakeOptions(*draw_data, 1.0f);
        const UiOptions copy_options = MakeFullscreenOptions(3.0f);
        const UiOptions additive_options = MakeFullscreenOptions(3.0f);
        const UiOptions present_options = MakeFullscreenOptions(4.0f,
            IsSrgbTextureFormat(active_presentation_format) ? 0.0f : 1.0f);
        UiOptions background_options = MakeFullscreenOptions(5.0f);
        std::copy(background_color_.begin(), background_color_.end(), background_options.tint);
        std::vector<size_t> option_offsets(kOptionCount);
        option_offsets[0] = write_options(0, base_options);
        option_offsets[1] = write_options(1, emission_options);
        option_offsets[2] = write_options(2, copy_options);
        option_offsets[3] = write_options(3, additive_options);
        option_offsets[4] = write_options(4, present_options);
        option_offsets[5] = write_options(5, background_options);
        for (size_t index = 0; index < regions.size(); ++index)
        {
            const UiOptions horizontal_options = MakeFullscreenOptions(2.0f, 0.0f,
                regions[index].marker.radius * draw_data->FramebufferScale.x /
                    (3.230769f * static_cast<float>(width)), 0.0f);
            const UiOptions vertical_options = MakeFullscreenOptions(2.0f, 0.0f, 0.0f,
                regions[index].marker.radius * draw_data->FramebufferScale.y /
                    (3.230769f * static_cast<float>(height)));
            option_offsets[6u + index * 2u] = write_options(6u + index * 2u,
                                                             horizontal_options);
            option_offsets[7u + index * 2u] = write_options(7u + index * 2u,
                                                             vertical_options);
        }

        if (target_width_ != width || target_height_ != height)
        {
            if (target_width_ != 0 || target_height_ != 0)
            {
                backend.WaitIdle();
                ClearBindingCache(backend);
                backend.DiscardTransientRenderTargets();
            }
            target_width_ = width;
            target_height_ = height;
        }

        std::vector<graphics::RenderTargetHandle> leases;
        const std::array<float, 4> transparent_clear{0.0f, 0.0f, 0.0f, 0.0f};
        auto acquire_target = [&](uint32_t target_width, uint32_t target_height,
                                  const std::array<float, 4> &clear)
        {
            graphics::RenderTargetHandle target = backend.AcquireTransientRenderTarget(
                MakeTargetDesc(target_width, target_height, clear));
            if (target.IsValid()) leases.push_back(target);
            return target;
        };
        const graphics::RenderTargetHandle canvas = acquire_target(
            width, height, background_color_);
        graphics::RenderTargetHandle source{};
        graphics::RenderTargetHandle horizontal{};
        graphics::RenderTargetHandle vertical{};
        std::vector<graphics::RenderTargetHandle> filtered_regions;
        if (!regions.empty())
        {
            source = acquire_target(width, height, transparent_clear);
            horizontal = acquire_target(half_width, half_height, transparent_clear);
            vertical = acquire_target(half_width, half_height, transparent_clear);
            for (size_t index = 0; index < regions.size(); ++index)
            {
                filtered_regions.push_back(acquire_target(half_width, half_height,
                                                          transparent_clear));
            }
        }
        const auto release_leases = [&]
        {
            for (const graphics::RenderTargetHandle target : leases)
            {
                backend.ReleaseTransientRenderTarget(target);
            }
            leases.clear();
        };
        if (!canvas.IsValid() || (!regions.empty() &&
            (!source.IsValid() || !horizontal.IsValid() || !vertical.IsValid() ||
             filtered_regions.size() != regions.size())))
        {
            release_leases();
            return fail("Editor bloom transient render-target allocation failed");
        }

        const graphics::GeometryView geometry{{{0, vertex_buffer_, 0}},
            {index_buffer_, 0, graphics::IndexElementType::UInt32}};
        const graphics::Viewport full_viewport{0.0f, 0.0f,
            static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        const graphics::Viewport half_viewport{0.0f, 0.0f,
            static_cast<float>(half_width), static_cast<float>(half_height), 0.0f, 1.0f};
        const float full_bounds[4]{draw_data->DisplayPos.x, draw_data->DisplayPos.y,
            draw_data->DisplayPos.x + draw_data->DisplaySize.x,
            draw_data->DisplayPos.y + draw_data->DisplaySize.y};

        auto make_bindings = [&](graphics::PipelineHandle pipeline,
                                 graphics::TextureHandle texture, size_t options_offset)
        {
            if (!texture.IsValid()) return graphics::DescriptorSetHandle{};
            const auto existing = std::find_if(binding_cache_.begin(), binding_cache_.end(),
                [this, pipeline, texture, options_offset](const BindingCacheEntry &entry)
                {
                    return entry.pipeline == pipeline && entry.texture == texture &&
                           entry.options_buffer == options_buffer_ &&
                           entry.options_offset == options_offset;
                });
            if (existing != binding_cache_.end())
            {
                return existing->bindings;
            }
            if (binding_cache_.size() >= kMaxCachedBindings)
            {
                // Existing sets may already be referenced by this command buffer.
                return graphics::DescriptorSetHandle{};
            }
            // Cross-frame cache entries must survive Vulkan frame-pool recycling.
            const graphics::DescriptorSetHandle bindings = backend.CreateResourceBindingSet(pipeline,
                {0, {graphics::UniformBufferBinding{0, 0, options_buffer_,
                         options_offset, sizeof(UiOptions)},
                     graphics::SampledTextureBinding{0, 1, texture, sampler_}}, true});
            if (bindings.IsValid())
            {
                binding_cache_.push_back({pipeline, texture, options_buffer_, options_offset, bindings});
            }
            return bindings;
        };
        auto draw_indexed = [&](graphics::PipelineHandle pipeline,
                                graphics::DescriptorSetHandle bindings,
                                uint32_t first_index, uint32_t index_count,
                                const graphics::Scissor &scissor)
        {
            if (scissor.width == 0 || scissor.height == 0) return true;
            return bindings.IsValid() && recorder.BindPipeline(pipeline) &&
                   recorder.BindGeometry(geometry) &&
                   recorder.BindResourceBindings(pipeline, bindings) &&
                   (recorder.SetScissor(scissor),
                    recorder.DrawIndexed(index_count, 1, first_index, 0, 0), true);
        };
        auto draw_fullscreen = [&](graphics::PipelineHandle pipeline,
                                   graphics::TextureHandle texture, size_t option_offset,
                                   uint32_t viewport_width, uint32_t viewport_height,
                                   const graphics::Scissor &scissor)
        {
            const auto bindings = make_bindings(pipeline, texture, option_offset);
            return draw_indexed(pipeline, bindings, quad_first_index, 6, scissor);
        };
        auto draw_ui_command = [&](const EditorUiDrawCommand &command,
                                   graphics::PipelineHandle pipeline, size_t option_offset,
                                   const float clip_bounds[4], uint32_t viewport_width,
                                   uint32_t viewport_height)
        {
            const auto texture = textures_.find(command.texture_id);
            if (texture == textures_.end()) return false;
            const graphics::Scissor scissor = MakeScissor(command.clip_rect, clip_bounds,
                *draw_data, viewport_width, viewport_height, backend.GetGraphicsAPI());
            const auto bindings = make_bindings(pipeline, texture->second.texture,
                                                option_offset);
            return draw_indexed(pipeline, bindings, command.first_index,
                                command.index_count, scissor);
        };

        bool render_target_active = false;
        const auto end_target = [&]
        {
            if (render_target_active)
            {
                recorder.EndRenderTarget();
                render_target_active = false;
            }
        };

        bool recorded = true;
        for (size_t region_index = 0; region_index < regions.size() && recorded;
             ++region_index)
        {
            if (!recorder.BeginRenderTarget(source))
            {
                recorded = false;
                break;
            }
            render_target_active = true;
            recorder.SetViewport(full_viewport);
            const Region &region = regions[region_index];
            for (const EmissionDraw &draw : region.draws)
            {
                if (draw.command_index >= packet.draw_commands.size() ||
                    !draw_ui_command(packet.draw_commands[draw.command_index],
                        emission_pipeline_, option_offsets[1], region.marker.bounds,
                        width, height))
                {
                    recorded = false;
                    break;
                }
            }
            end_target();
            if (!recorded || !recorder.RequireRenderTargetUsage(source,
                    graphics::ResourceUsage::Sampled))
            {
                recorded = false;
                break;
            }

            if (!recorder.BeginRenderTarget(horizontal))
            {
                recorded = false;
                break;
            }
            render_target_active = true;
            recorder.SetViewport(half_viewport);
            const graphics::TextureHandle source_texture = backend.GetRenderTargetColor(source);
            recorded = draw_fullscreen(blur_pipeline_, source_texture,
                option_offsets[6u + region_index * 2u],
                half_width, half_height, {0, 0, half_width, half_height});
            end_target();
            if (!recorded || !recorder.RequireRenderTargetUsage(horizontal,
                    graphics::ResourceUsage::Sampled))
            {
                recorded = false;
                break;
            }

            if (!recorder.BeginRenderTarget(vertical))
            {
                recorded = false;
                break;
            }
            render_target_active = true;
            recorder.SetViewport(half_viewport);
            recorded = draw_fullscreen(blur_pipeline_,
                backend.GetRenderTargetColor(horizontal),
                option_offsets[7u + region_index * 2u],
                half_width, half_height, {0, 0, half_width, half_height});
            end_target();
            if (!recorded || !recorder.RequireRenderTargetUsage(vertical,
                    graphics::ResourceUsage::Sampled))
            {
                recorded = false;
                break;
            }

            if (!recorder.BeginRenderTarget(filtered_regions[region_index]))
            {
                recorded = false;
                break;
            }
            render_target_active = true;
            recorder.SetViewport(half_viewport);
            recorded = draw_fullscreen(copy_pipeline_,
                backend.GetRenderTargetColor(vertical), option_offsets[2],
                half_width, half_height, {0, 0, half_width, half_height});
            end_target();
            if (!recorded || !recorder.RequireRenderTargetUsage(
                    filtered_regions[region_index], graphics::ResourceUsage::Sampled))
            {
                recorded = false;
                break;
            }
        }

        if (recorded && recorder.BeginRenderTarget(canvas))
        {
            render_target_active = true;
            recorder.SetViewport(full_viewport);
            const graphics::Scissor full_scissor{0, 0, width, height};
            recorded = draw_fullscreen(ui_pipeline_, font_texture_, option_offsets[5],
                                       width, height, full_scissor);
            for (size_t item_index = 0; recorded &&
                 item_index < packet.ordered_items.size(); ++item_index)
            {
                const EditorUiFrameItem &item = packet.ordered_items[item_index];
                if (item.kind == EditorUiFrameItem::Kind::Draw)
                {
                    if (item.draw_command_index >= packet.draw_commands.size() ||
                        !draw_ui_command(packet.draw_commands[item.draw_command_index],
                            ui_pipeline_, option_offsets[0], full_bounds,
                            width, height))
                    {
                        recorded = false;
                        break;
                    }
                }
                const auto region_end = region_end_items.find(item_index);
                if (region_end != region_end_items.end())
                {
                    const size_t region_index = region_end->second;
                    if (region_index >= filtered_regions.size())
                    {
                        recorded = false;
                        break;
                    }
                    const EditorGlowMarker &marker = regions[region_index].marker;
                    const float halo_bounds[4]{
                        marker.bounds[0] - marker.radius,
                        marker.bounds[1] - marker.radius,
                        marker.bounds[2] + marker.radius,
                        marker.bounds[3] + marker.radius};
                    const graphics::Scissor glow_scissor = MakeScissor(
                        halo_bounds, halo_bounds, *draw_data, width, height,
                        backend.GetGraphicsAPI());
                    recorded = draw_fullscreen(additive_pipeline_,
                        backend.GetRenderTargetColor(filtered_regions[region_index]),
                        option_offsets[3], width, height, glow_scissor);
                    if (!recorded) break;
                }
            }
            end_target();
        }
        else
        {
            recorded = false;
        }

        if (recorded && recorder.RequireRenderTargetUsage(canvas,
                graphics::ResourceUsage::Sampled) && recorder.BeginPresentation(&clear_color))
        {
            render_target_active = true;
            recorder.SetViewport(full_viewport);
            recorded = draw_fullscreen(presentation_pipeline_,
                backend.GetRenderTargetColor(canvas), option_offsets[4],
                width, height, {0, 0, width, height});
            end_target();
        }
        else
        {
            recorded = false;
            end_target();
        }
        end_target();
        release_leases();
        if (!recorded)
        {
            return fail("Editor bloom command recording failed; native UI fallback selected");
        }
        if (diagnostic != nullptr) diagnostic->clear();
        ready_ = true;
        return true;
    }
}
