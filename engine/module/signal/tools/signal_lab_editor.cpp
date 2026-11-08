#include "signal_lab_editor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <numbers>
#include <optional>
#include <span>

#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_settings.h"
#include "editor/ui/editor_ui.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"
#include "runtime/window/window_system.h"
#include "imgui.h"

namespace kpengine::signal
{
    namespace
    {
        // Match EditorTheme's cool slate ink and restrained accent.
        constexpr ImVec4 kAccent{0.6353f, 0.7490f, 0.8000f, 1.0f};
        constexpr ImVec4 kMuted{0.6078f, 0.6863f, 0.7294f, 0.82f};
        constexpr ImVec4 kValue{0.8314f, 0.8980f, 0.9255f, 1.0f};
        constexpr std::array<ImU32, 8> kSignalColors{
            IM_COL32(162, 191, 204, 255), IM_COL32(107, 214, 177, 255),
            IM_COL32(205, 151, 235, 255), IM_COL32(239, 178, 111, 255),
            IM_COL32(235, 124, 150, 255), IM_COL32(126, 174, 239, 255),
            IM_COL32(200, 205, 119, 255), IM_COL32(120, 205, 207, 255)};

        const char *SignalKindName(const SignalKind kind) noexcept
        {
            switch (kind)
            {
            case SignalKind::Sine: return "Sine";
            case SignalKind::Cosine: return "Cosine";
            case SignalKind::Square: return "Square";
            case SignalKind::Constant: return "Constant";
            case SignalKind::Impulse: return "Impulse";
            case SignalKind::UniformNoise: return "Uniform noise";
            }
            return "Unknown";
        }

        void RenderPanelHeading(const char *title, const char *detail)
        {
            ImGui::TextColored(kAccent, "%s", title);
            ImGui::TextDisabled("%s", detail);
            ImGui::Spacing();
        }

        void RenderProperty(const char *label, const char *value)
        {
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(92.0f);
            ImGui::TextColored(kValue, "%s", value);
        }

        void DrawVisibilityEye(const ImVec2 min, const ImVec2 max,
                               const ImU32 color, const bool visible)
        {
            ImDrawList *draw = ImGui::GetWindowDrawList();
            const ImVec2 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
            std::array<ImVec2, 14> outline{};
            constexpr float kRadiusX = 7.0f;
            constexpr float kRadiusY = 4.5f;
            for (int point = 0; point < 7; ++point)
            {
                const float x = -kRadiusX + 2.0f * kRadiusX *
                    static_cast<float>(point) / 6.0f;
                const float taper = std::sqrt(std::max(0.0f,
                    1.0f - (x * x) / (kRadiusX * kRadiusX)));
                outline[static_cast<std::size_t>(point)] =
                    {center.x + x, center.y - kRadiusY * taper};
                outline[static_cast<std::size_t>(13 - point)] =
                    {center.x + x, center.y + kRadiusY * taper};
            }
            draw->AddPolyline(outline.data(), static_cast<int>(outline.size()), color,
                              ImDrawFlags_Closed, 1.4f);
            if (visible)
            {
                draw->AddCircleFilled(center, 2.0f, color);
            }
            else
            {
                draw->AddLine({center.x - 6.0f, center.y + 6.0f},
                              {center.x + 6.0f, center.y - 6.0f}, color, 1.6f);
            }
        }

        double NiceAxisStep(const double approximate_step)
        {
            if (!(approximate_step > 0.0) || !std::isfinite(approximate_step))
            {
                return 1.0;
            }

            const double magnitude = std::pow(10.0, std::floor(std::log10(approximate_step)));
            const double fraction = approximate_step / magnitude;
            const double nice_fraction = fraction <= 1.0 ? 1.0
                : fraction <= 2.0 ? 2.0
                : fraction <= 2.5 ? 2.5
                : fraction <= 5.0 ? 5.0 : 10.0;
            return nice_fraction * magnitude;
        }

        void RenderSignalPlot(const std::span<const SignalLabSignal> signals,
                              const bool composite_mode,
                              const std::span<const double> composite_samples,
                              const bool composite_valid)
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 size{std::max(available.x, 240.0f),
                              std::max(available.y - 42.0f, 220.0f)};
            ImGui::InvisibleButton("##signal_plot", size);
            const ImVec2 origin = ImGui::GetItemRectMin();
            const ImVec2 end = ImGui::GetItemRectMax();
            ImDrawList *draw = ImGui::GetWindowDrawList();
            const ImU32 panel = ImGui::GetColorU32(ImGuiCol_FrameBg);
            const ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border, 0.65f);
            const ImU32 axis = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.9f);
            draw->AddRectFilled(origin, end, panel, 8.0f);
            draw->AddRect(origin, end, IM_COL32(145, 170, 178, 48), 8.0f);

            if (signals.empty())
            {
                const char *message = "No samples available";
                const ImVec2 text_size = ImGui::CalcTextSize(message);
                draw->AddText({(origin.x + end.x - text_size.x) * 0.5f,
                               (origin.y + end.y - text_size.y) * 0.5f},
                              ImGui::GetColorU32(kMuted), message);
                return;
            }

            const ImVec2 graph_min{origin.x + 54.0f, origin.y + 28.0f};
            const ImVec2 graph_max{end.x - 16.0f, end.y - 48.0f};
            const float graph_width = std::max(graph_max.x - graph_min.x, 1.0f);
            const float graph_height = std::max(graph_max.y - graph_min.y, 1.0f);
            const float center_y = (graph_min.y + graph_max.y) * 0.5f;
            const ImU32 label_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);

            const SampleBlockView first_block = signals.front().samples.View();
            const std::span<const double> timeline = first_block.ChannelSamples(0);
            if (timeline.empty())
            {
                return;
            }
            double peak = 0.0;
            std::size_t visible_count = 0;
            for (const SignalLabSignal &signal : signals)
            {
                if (!signal.visible)
                {
                    continue;
                }
                ++visible_count;
                for (const double sample : signal.samples.ChannelSamples(0))
                {
                    peak = std::max(peak, std::abs(sample));
                }
            }

            if (visible_count == 0)
            {
                const char *message = "All signals are hidden";
                const ImVec2 text_size = ImGui::CalcTextSize(message);
                draw->AddText({(graph_min.x + graph_max.x - text_size.x) * 0.5f,
                               (graph_min.y + graph_max.y - text_size.y) * 0.5f},
                              ImGui::GetColorU32(kMuted), message);
                return;
            }

            if (composite_mode && !composite_valid)
            {
                const char *message = "Composite exceeds the numeric range";
                const ImVec2 text_size = ImGui::CalcTextSize(message);
                draw->AddText({(graph_min.x + graph_max.x - text_size.x) * 0.5f,
                               (graph_min.y + graph_max.y - text_size.y) * 0.5f},
                              ImGui::GetColorU32(kMuted), message);
                return;
            }
            if (composite_valid)
            {
                for (const double sample : composite_samples)
                {
                    peak = std::max(peak, std::abs(sample));
                }
            }

            const double amplitude_tick = NiceAxisStep(std::max(peak, 1.0e-12) / 3.0 * 1.1);
            const double amplitude_limit = amplitude_tick * 3.0;
            const float amplitude_division_count = 6.0f;
            for (int division = 0; division <= 6; ++division)
            {
                const float fraction = static_cast<float>(division) / amplitude_division_count;
                const float y = graph_min.y + graph_height * fraction;
                const bool zero_axis = division == 3;
                draw->AddLine({graph_min.x, y}, {graph_max.x, y},
                              zero_axis ? axis : grid, zero_axis ? 1.4f : 1.0f);
                draw->AddLine({graph_min.x - 4.0f, y}, {graph_min.x, y}, axis);

                const double value = amplitude_limit - 2.0 * amplitude_limit * fraction;
                char label[32]{};
                std::snprintf(label, sizeof(label), "%.3g", value);
                const ImVec2 label_size = ImGui::CalcTextSize(label);
                draw->AddText({graph_min.x - label_size.x - 8.0f,
                               y - label_size.y * 0.5f}, label_color, label);
            }

            constexpr int kTimeDivisionCount = 5;
            const double duration_ms = static_cast<double>(timeline.size()) * 1000.0 /
                                       first_block.SampleRateHz();
            for (int division = 0; division <= kTimeDivisionCount; ++division)
            {
                const float fraction = static_cast<float>(division) /
                                       static_cast<float>(kTimeDivisionCount);
                const float x = graph_min.x + graph_width * fraction;
                draw->AddLine({x, graph_min.y}, {x, graph_max.y}, grid);
                draw->AddLine({x, graph_max.y}, {x, graph_max.y + 4.0f}, axis);

                char label[32]{};
                std::snprintf(label, sizeof(label), "%.3g",
                              duration_ms * static_cast<double>(fraction));
                const ImVec2 label_size = ImGui::CalcTextSize(label);
                draw->AddText({x - label_size.x * 0.5f, graph_max.y + 8.0f},
                              label_color, label);
            }
            draw->AddLine({graph_min.x, graph_min.y}, {graph_min.x, graph_max.y}, axis, 1.4f);
            draw->AddText({graph_min.x, origin.y + 7.0f}, label_color, "AMPLITUDE");
            const char *time_label = "TIME (ms)";
            const ImVec2 time_label_size = ImGui::CalcTextSize(time_label);
            draw->AddText({graph_max.x - time_label_size.x,
                           graph_max.y + 25.0f}, label_color, time_label);

            constexpr std::size_t kMaximumDisplayPoints = 2'048;
            std::array<ImVec2, kMaximumDisplayPoints> points{};
            draw->PushClipRect(origin, end, true);
            const auto draw_trace = [&](const std::span<const double> samples,
                                        const ImU32 color)
            {
                const std::size_t point_count = std::min(samples.size(), kMaximumDisplayPoints);
                for (std::size_t point = 0; point < point_count; ++point)
                {
                    const std::size_t sample_index = point_count == 1
                        ? 0
                        : point * (samples.size() - 1) / (point_count - 1);
                    const float x = point_count == 1
                        ? (graph_min.x + graph_max.x) * 0.5f
                        : graph_min.x + graph_width * static_cast<float>(point) /
                              static_cast<float>(point_count - 1);
                    const float y = center_y - static_cast<float>(samples[sample_index] /
                        amplitude_limit) * graph_height * 0.5f;
                    points[point] = {x, y};
                }
                const ImU32 glow = (color & 0x00FFFFFFU) | 0x28000000U;
                if (point_count == 1)
                {
                    draw->AddCircleFilled(points[0], 3.0f, color);
                }
                else
                {
                    draw->AddPolyline(points.data(), static_cast<int>(point_count),
                                      glow, ImDrawFlags_None, 5.0f);
                    draw->AddPolyline(points.data(), static_cast<int>(point_count),
                                      color, ImDrawFlags_None, 2.0f);
                }
            };
            if (composite_mode)
            {
                draw_trace(composite_samples, IM_COL32(239, 178, 111, 255));
            }
            else
            {
                for (const SignalLabSignal &signal : signals)
                {
                    if (signal.visible)
                    {
                        draw_trace(signal.samples.ChannelSamples(0),
                                   static_cast<ImU32>(signal.color));
                    }
                }
            }
            draw->PopClipRect();

            if (ImGui::IsItemHovered() && !signals.empty())
            {
                const float cursor = std::clamp(ImGui::GetIO().MousePos.x - graph_min.x,
                                                0.0f, graph_width);
                const std::size_t sample_index = static_cast<std::size_t>(
                    cursor / graph_width * static_cast<float>(timeline.size() - 1));
                const ImVec2 probe{graph_min.x + cursor, center_y};
                draw->AddLine({probe.x, graph_min.y}, {probe.x, graph_max.y},
                              IM_COL32(210, 229, 229, 110));
                const std::uint64_t absolute_index = first_block.FirstSampleIndex() + sample_index;
                const double time_seconds = static_cast<double>(absolute_index) /
                                            first_block.SampleRateHz();
                ImGui::BeginTooltip();
                ImGui::Text("Sample %llu", static_cast<unsigned long long>(absolute_index));
                ImGui::Text("Time %.6f s", time_seconds);
                if (composite_mode)
                {
                    if (sample_index < composite_samples.size())
                    {
                        ImGui::TextColored(ImVec4{0.94f, 0.70f, 0.44f, 1.0f},
                            "Composite: %.6f", composite_samples[sample_index]);
                    }
                }
                else for (const SignalLabSignal &signal : signals)
                {
                    if (!signal.visible)
                    {
                        continue;
                    }
                    const std::span<const double> samples = signal.samples.ChannelSamples(0);
                    if (sample_index < samples.size())
                    {
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                            static_cast<ImU32>(signal.color)), "%s: %.6f",
                            signal.name.c_str(), samples[sample_index]);
                    }
                }
                ImGui::EndTooltip();
            }
        }

        void RenderMeasurement(const char *label, const double value)
        {
            ImGui::TextDisabled("%s", label);
            ImGui::TextColored(kValue, "%.4f", value);
            ImGui::Spacing();
        }
    }

    SignalLabEditor::SignalLabEditor() = default;

    SignalLabEditor::~SignalLabEditor()
    {
        Shutdown();
    }

    bool SignalLabEditor::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        if (!GenerateSignal(diagnostic))
        {
            return false;
        }

        runtime::RuntimeContext &context = runtime::global_runtime_context;
        if (context.window_system_ == nullptr || context.render_system_ == nullptr ||
            context.log_system_ == nullptr)
        {
            diagnostic = "Signal Lab presentation services are unavailable";
            return false;
        }

        editor::EditorUIInitInfo init{};
        init.window = context.window_system_->GetNativeHandle();
        init.editor_presentation_bridge =
            context.render_system_->GetEditorPresentationBridge();
        init.log_system = context.log_system_.get();
        init.engine = &engine;
        init.render_system = context.render_system_.get();
        init.window_system = context.window_system_.get();
        try
        {
            editor::EditorWindowConfig log_config;
            log_config.pos_y_ratio = 0.72f;
            log_config.width_ratio = 1.0f;
            log_config.height_ratio = 0.28f;
            log_config.extra_flags = ImGuiWindowFlags_NoCollapse;
            log_component_ = std::make_unique<editor::EditorLogComponent>(
                context.log_system_.get(), editor::DefaultLogColors(), log_config);
            ui_ = std::make_unique<editor::EditorUI>();
            ui_->InitializeViewer(init, [this]
            {
                RenderWorkspace();
                if (log_component_ != nullptr)
                {
                    log_component_->Render();
                }
            });
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Signal Lab UI initialization failed: ") + error.what();
            Shutdown();
            return false;
        }
        diagnostic.clear();
        return true;
    }

    bool SignalLabEditor::GenerateSignal(std::string &diagnostic)
    {
        constexpr std::size_t kMaximumSignals = 8;
        if (signals_.size() >= kMaximumSignals)
        {
            diagnostic = "Signal Lab supports up to eight displayed signals";
            return false;
        }

        SignalGeneratorConfig config;
        config.kind = generator_kind_;
        config.sample_rate_hz = 48'000.0;
        config.frequency_hz = generator_frequency_hz_;
        config.amplitude = generator_amplitude_;
        config.offset = generator_offset_;
        config.phase_radians = generator_phase_degrees_ *
                               (std::numbers::pi_v<double> / 180.0);
        config.unit = SampleUnit::Dimensionless;

        std::optional<SignalGenerator> generator = SignalGenerator::Create(config, diagnostic);
        if (!generator.has_value())
        {
            return false;
        }
        std::optional<SampleBlock> block = generator->Generate(480, diagnostic);
        if (!block.has_value())
        {
            return false;
        }
        std::optional<SignalStatistics> statistics = Measure(
            block->ChannelSamples(0), diagnostic);
        if (!statistics.has_value())
        {
            return false;
        }

        const std::uint64_t serial = ++signal_serial_;
        char name[48]{};
        std::snprintf(name, sizeof(name), "%s %02llu", SignalKindName(config.kind),
                      static_cast<unsigned long long>(serial));
        const std::uint32_t color = static_cast<std::uint32_t>(
            kSignalColors[(serial - 1) % kSignalColors.size()]);
        signals_.emplace_back(name, config, std::move(*block), *statistics, color);
        selected_signal_ = signals_.size() - 1;
        RebuildComposite();
        diagnostic.clear();
        return true;
    }

    void SignalLabEditor::RebuildComposite()
    {
        composite_samples_.clear();
        composite_valid_ = true;
        if (signals_.empty())
        {
            composite_statistics_ = {};
            return;
        }

        const std::size_t sample_count = signals_.front().samples.SampleCount();
        composite_samples_.assign(sample_count, 0.0);
        bool has_visible_signal = false;
        for (const SignalLabSignal &signal : signals_)
        {
            if (!signal.visible)
            {
                continue;
            }
            has_visible_signal = true;
            const std::span<const double> samples = signal.samples.ChannelSamples(0);
            if (samples.size() != sample_count)
            {
                composite_valid_ = false;
                composite_samples_.clear();
                return;
            }
            for (std::size_t sample = 0; sample < sample_count; ++sample)
            {
                const double sum = composite_samples_[sample] + samples[sample];
                if (!std::isfinite(sum))
                {
                    composite_valid_ = false;
                    composite_samples_.clear();
                    return;
                }
                composite_samples_[sample] = sum;
            }
        }

        if (!has_visible_signal)
        {
            composite_samples_.clear();
            composite_statistics_ = {};
            return;
        }

        std::string diagnostic;
        const std::optional<SignalStatistics> statistics = Measure(
            composite_samples_, diagnostic);
        if (statistics.has_value())
        {
            composite_statistics_ = *statistics;
        }
        else
        {
            composite_valid_ = false;
            composite_samples_.clear();
        }
    }

    bool SignalLabEditor::Render(std::string &diagnostic)
    {
        diagnostic.clear();
        if (ui_ == nullptr || !ui_->Render())
        {
            diagnostic = "Signal Lab ImGui presentation failed";
            return false;
        }
        return true;
    }

    void SignalLabEditor::Shutdown() noexcept
    {
        log_component_.reset();
        if (ui_ != nullptr)
        {
            ui_->Close();
            ui_.reset();
        }
    }

    void SignalLabEditor::RenderWorkspace()
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize({viewport->WorkSize.x, viewport->WorkSize.y * 0.72f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::Begin("Signal Lab", nullptr, flags))
        {
            ImGui::TextColored(kAccent, "SIGNAL LAB");
            ImGui::SameLine();
            ImGui::TextDisabled("/  SIGNAL GENERATOR");
            ImGui::TextDisabled("Build a multi-signal sample view.");
            ImGui::Separator();
            const char *waveforms[]{"Sine", "Cosine", "Square"};
            int waveform = generator_kind_ == SignalKind::Cosine ? 1
                : generator_kind_ == SignalKind::Square ? 2 : 0;
            ImGui::SetNextItemWidth(112.0f);
            if (ImGui::Combo("Waveform", &waveform, waveforms, 3))
            {
                generator_kind_ = waveform == 1 ? SignalKind::Cosine
                    : waveform == 2 ? SignalKind::Square : SignalKind::Sine;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputDouble("Freq (Hz)", &generator_frequency_hz_, 0.0, 0.0, "%.1f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputDouble("Amplitude", &generator_amplitude_, 0.0, 0.0, "%.3f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputDouble("Phase (deg)", &generator_phase_degrees_, 0.0, 0.0, "%.1f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputDouble("Offset", &generator_offset_, 0.0, 0.0, "%.3f");
            ImGui::SameLine();
            ImGui::BeginDisabled(signals_.size() >= kSignalColors.size());
            if (ImGui::Button("Generate", {92.0f, 0.0f}))
            {
                if (GenerateSignal(generator_message_))
                {
                    generator_message_.clear();
                }
            }
            ImGui::EndDisabled();
            if (!generator_message_.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4{0.92f, 0.57f, 0.52f, 1.0f}, "%s",
                                   generator_message_.c_str());
            }

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.0f, 8.0f});
            const float content_height = ImGui::GetContentRegionAvail().y;
            const float panel_height = std::max(content_height - 4.0f, 160.0f);
            if (ImGui::BeginTable("##signal_workspace", 3,
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                    {0.0f, panel_height}))
            {
                ImGui::TableSetupColumn("Signals", ImGuiTableColumnFlags_WidthFixed, 205.0f);
                ImGui::TableSetupColumn("Waveform", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 245.0f);
                ImGui::TableNextColumn();
                ImGui::BeginChild("##signal_list_panel", {0.0f, 0.0f}, true,
                                  ImGuiWindowFlags_NoScrollbar);
                RenderPanelHeading("SIGNALS", "GENERATED");
                for (std::size_t index = 0; index < signals_.size(); ++index)
                {
                    SignalLabSignal &signal = signals_[index];
                    const float row_width = std::max(ImGui::GetContentRegionAvail().x -
                        55.0f, 58.0f);
                    if (ImGui::Selectable(signal.name.c_str(), selected_signal_ == index,
                                          ImGuiSelectableFlags_None, {row_width, 27.0f}))
                    {
                        selected_signal_ = index;
                    }
                    ImGui::SameLine();
                    const std::string color_popup_id =
                        "##signal_color_picker_" + std::to_string(index);
                    if (ImGui::ColorButton(("##signal_color_" + std::to_string(index)).c_str(),
                        ImGui::ColorConvertU32ToFloat4(static_cast<ImU32>(signal.color)),
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                        {18.0f, 18.0f}))
                    {
                        selected_signal_ = index;
                        ImGui::OpenPopup(color_popup_id.c_str());
                    }
                    if (ImGui::BeginPopup(color_popup_id.c_str()))
                    {
                        ImGui::Text("%s color", signal.name.c_str());
                        const ImVec4 current_color = ImGui::ColorConvertU32ToFloat4(
                            static_cast<ImU32>(signal.color));
                        float editable_color[4]{current_color.x, current_color.y,
                                                 current_color.z, current_color.w};
                        if (ImGui::ColorPicker4("##signal_color_picker", editable_color,
                                ImGuiColorEditFlags_NoAlpha |
                                ImGuiColorEditFlags_PickerHueWheel |
                                ImGuiColorEditFlags_DisplayRGB))
                        {
                            signal.color = static_cast<std::uint32_t>(
                                ImGui::ColorConvertFloat4ToU32({editable_color[0],
                                    editable_color[1], editable_color[2], editable_color[3]}));
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::SameLine();
                    const std::string visibility_id =
                        "##signal_visibility_" + std::to_string(index);
                    ImGui::InvisibleButton(visibility_id.c_str(), {20.0f, 20.0f});
                    DrawVisibilityEye(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                        ImGui::GetColorU32(signal.visible ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                        signal.visible);
                    if (ImGui::IsItemClicked())
                    {
                        signal.visible = !signal.visible;
                        RebuildComposite();
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(signal.visible ? "Hide signal" : "Show signal");
                    }
                }
                if (signals_.empty())
                {
                    ImGui::TextDisabled("Generate a waveform to begin.");
                }
                ImGui::EndChild();

                ImGui::TableNextColumn();
                ImGui::BeginChild("##signal_waveform_panel", {0.0f, 0.0f}, true,
                                  ImGuiWindowFlags_NoScrollbar);
                const bool composite_mode = display_mode_ == DisplayMode::Composite;
                RenderPanelHeading("DISPLAY", composite_mode
                    ? "TIME DOMAIN  /  COMPOSITE SUM"
                    : "TIME DOMAIN  /  ALL SIGNALS");
                const char *display_modes[]{"Overlay", "Composite"};
                int display_mode = composite_mode ? 1 : 0;
                ImGui::SetNextItemWidth(156.0f);
                if (ImGui::Combo("View mode", &display_mode, display_modes, 2))
                {
                    display_mode_ = display_mode == 1
                        ? DisplayMode::Composite : DisplayMode::Overlay;
                }
                RenderSignalPlot(signals_, composite_mode, composite_samples_,
                                 composite_valid_);
                if (composite_mode && composite_valid_ && !composite_samples_.empty())
                {
                    ImGui::TextColored(ImVec4{0.94f, 0.70f, 0.44f, 1.0f},
                        "Composite RMS %.4f  |  Peak-to-peak %.4f",
                        composite_statistics_.rms, composite_statistics_.peak_to_peak);
                }
                ImGui::TextDisabled("Hover the plot to compare sample values.");
                ImGui::EndChild();

                ImGui::TableNextColumn();
                ImGui::BeginChild("##signal_readout_panel", {0.0f, 0.0f}, true,
                                  ImGuiWindowFlags_NoScrollbar);
                if (signals_.empty())
                {
                    RenderPanelHeading("DETAILS", "SELECTED SIGNAL");
                    ImGui::TextDisabled("No signal selected.");
                }
                else
                {
                    selected_signal_ = std::min(selected_signal_, signals_.size() - 1);
                    const SignalLabSignal &signal = signals_[selected_signal_];
                    const SampleBlockView block = signal.samples.View();
                    char value[48]{};
                    RenderPanelHeading("DETAILS", "SELECTED SIGNAL");
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                        static_cast<ImU32>(signal.color)), "%s", signal.name.c_str());
                    ImGui::Spacing();
                    RenderProperty("Waveform", SignalKindName(signal.config.kind));
                    std::snprintf(value, sizeof(value), "%.3f Hz", signal.config.frequency_hz);
                    RenderProperty("Frequency", value);
                    std::snprintf(value, sizeof(value), "%.4f", signal.config.amplitude);
                    RenderProperty("Amplitude", value);
                    std::snprintf(value, sizeof(value), "%.2f deg",
                        signal.config.phase_radians * 180.0 / std::numbers::pi_v<double>);
                    RenderProperty("Phase", value);
                    std::snprintf(value, sizeof(value), "%.4f", signal.config.offset);
                    RenderProperty("Offset", value);
                    std::snprintf(value, sizeof(value), "%.0f Hz", block.SampleRateHz());
                    RenderProperty("Sample rate", value);
                    std::snprintf(value, sizeof(value), "%zu", block.SampleCount());
                    RenderProperty("Samples", value);
                    std::snprintf(value, sizeof(value), "%.3f ms",
                        static_cast<double>(block.SampleCount()) * 1000.0 / block.SampleRateHz());
                    RenderProperty("Duration", value);
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    RenderPanelHeading("MEASUREMENTS", "SAMPLE DATA");
                    RenderMeasurement("MEAN", signal.statistics.mean);
                    RenderMeasurement("RMS", signal.statistics.rms);
                    RenderMeasurement("MINIMUM", signal.statistics.minimum);
                    RenderMeasurement("MAXIMUM", signal.statistics.maximum);
                    RenderMeasurement("PEAK TO PEAK", signal.statistics.peak_to_peak);
                }
                ImGui::EndChild();
                ImGui::EndTable();
            }
            else
            {
                ImGui::TextDisabled("Workspace is too narrow to display panels.");
            }
            ImGui::PopStyleVar();
        }
        ImGui::End();
    }
}
