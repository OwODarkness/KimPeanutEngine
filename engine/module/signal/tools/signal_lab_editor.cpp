#include "signal_lab_editor.h"
#include "signal_scope_session.h"

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

        void RenderScopePlot(const SignalScopeSnapshot &snapshot, const ImU32 trace_color,
                             const bool trace_visible)
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 size{std::max(available.x, 260.0f),
                              std::max(available.y - 42.0f, 220.0f)};
            ImGui::InvisibleButton("##scope_capture_plot", size);
            const ImVec2 origin = ImGui::GetItemRectMin();
            const ImVec2 end = ImGui::GetItemRectMax();
            ImDrawList *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);
            draw->AddRect(origin, end, ImGui::GetColorU32(ImGuiCol_Border), 6.0f);
            const ImVec2 graph_min{origin.x + 52.0f, origin.y + 20.0f};
            const ImVec2 graph_max{end.x - 14.0f, end.y - 42.0f};
            const float width = std::max(graph_max.x - graph_min.x, 1.0f);
            const float height = std::max(graph_max.y - graph_min.y, 1.0f);
            const ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border, 0.65f);
            const ImU32 axis = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImU32 labels = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const std::span<const double> samples = snapshot.capture != nullptr
                ? std::span<const double>(*snapshot.capture) : std::span<const double>{};
            double minimum = -1.0;
            double maximum = 1.0;
            if (!samples.empty())
            {
                const auto bounds = std::minmax_element(samples.begin(), samples.end());
                const double peak = std::max(std::abs(*bounds.first), std::abs(*bounds.second));
                const double tick = NiceAxisStep(std::max(peak * 1.1 / 4.0, 1.0e-9));
                minimum = -tick * 4.0;
                maximum = tick * 4.0;
            }
            const double rate = snapshot.source.sample_rate_hz;
            const double duration_ms = !samples.empty() && rate > 0.0
                ? static_cast<double>(samples.size()) * 1000.0 / rate : 10.0;
            for (int i = 0; i <= 8; ++i)
            {
                const float fraction = static_cast<float>(i) / 8.0f;
                const float y = graph_min.y + fraction * height;
                draw->AddLine({graph_min.x, y}, {graph_max.x, y}, grid);
                char label[32]{};
                std::snprintf(label, sizeof(label), "%.3g", maximum - fraction * (maximum - minimum));
                const ImVec2 text = ImGui::CalcTextSize(label);
                draw->AddText({graph_min.x - text.x - 7.0f, y - text.y * 0.5f}, labels, label);
            }
            for (int i = 0; i <= 10; ++i)
            {
                const float fraction = static_cast<float>(i) / 10.0f;
                const float x = graph_min.x + fraction * width;
                draw->AddLine({x, graph_min.y}, {x, graph_max.y}, grid);
                char label[32]{};
                std::snprintf(label, sizeof(label), "%.3g", duration_ms * fraction);
                const ImVec2 text = ImGui::CalcTextSize(label);
                draw->AddText({x - text.x * 0.5f, graph_max.y + 7.0f}, labels, label);
            }
            draw->AddLine({graph_min.x, graph_min.y}, {graph_min.x, graph_max.y}, axis, 1.3f);
            draw->AddText({graph_min.x, origin.y + 2.0f}, labels, "VOLTAGE (V)");
            draw->AddText({graph_max.x - 64.0f, graph_max.y + 24.0f}, labels, "TIME (ms)");
            if (samples.empty())
            {
                const ImVec2 text = ImGui::CalcTextSize(snapshot.state_label.c_str());
                draw->AddText({graph_min.x + (width - text.x) * 0.5f,
                               graph_min.y + (height - text.y) * 0.5f},
                              ImGui::GetColorU32(kMuted), snapshot.state_label.c_str());
                return;
            }
            if (!trace_visible)
            {
                const char *message = "Selected signal is hidden";
                const ImVec2 text = ImGui::CalcTextSize(message);
                draw->AddText({graph_min.x + (width - text.x) * 0.5f,
                               graph_min.y + (height - text.y) * 0.5f},
                              ImGui::GetColorU32(kMuted), message);
                return;
            }
            const std::size_t trigger_offset = snapshot.trigger_sample.has_value()
                ? snapshot.pretrigger_samples : samples.size() / 4;
            const float trigger_x = graph_min.x + width * static_cast<float>(trigger_offset) /
                                    static_cast<float>(samples.size());
            draw->AddLine({trigger_x, graph_min.y}, {trigger_x, graph_max.y},
                          IM_COL32(239, 166, 82, 190), 1.2f);
            const auto map_y = [&](const double value)
            {
                return graph_min.y + static_cast<float>((maximum - value) /
                    (maximum - minimum)) * height;
            };
            draw->PushClipRect(graph_min, graph_max, true);
            for (std::size_t i = 1; i < samples.size(); ++i)
            {
                const float x0 = graph_min.x + width * static_cast<float>(i - 1) /
                                 static_cast<float>(samples.size() - 1);
                const float x1 = graph_min.x + width * static_cast<float>(i) /
                                 static_cast<float>(samples.size() - 1);
                draw->AddLine({x0, map_y(samples[i - 1])}, {x1, map_y(samples[i])},
                              trace_color, 1.8f);
            }
            for (std::size_t i = 0; i < samples.size(); i += 24)
            {
                const float x = graph_min.x + width * static_cast<float>(i) /
                                static_cast<float>(samples.size() - 1);
                draw->AddCircleFilled({x, map_y(samples[i])}, 2.2f, trace_color);
            }
            draw->PopClipRect();
        }
    }

    SignalLabEditor::SignalLabEditor() = default;

    SignalLabEditor::~SignalLabEditor()
    {
        Shutdown();
    }

    bool SignalLabEditor::Initialize(runtime::Engine &engine,
                                     std::shared_ptr<SignalScopeSession> scope_session,
                                     std::string &diagnostic)
    {
        scope_session_ = std::move(scope_session);
        if (scope_session_ == nullptr)
        {
            diagnostic = "Signal Lab acquisition session is unavailable";
            return false;
        }
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
        config.sample_rate_hz = generator_sample_rate_hz_;
        config.frequency_hz = generator_frequency_hz_;
        config.amplitude = generator_amplitude_;
        config.offset = generator_offset_;
        config.phase_radians = generator_phase_degrees_ *
                               (std::numbers::pi_v<double> / 180.0);
        config.harmonic_distortion = generator_realistic_
            ? generator_harmonic_distortion_percent_ / 100.0 : 0.0;
        config.noise_rms = generator_realistic_ ? generator_noise_rms_ : 0.0;
        config.seed = signal_serial_ + 1;
        config.unit = SampleUnit::Volt;

        if (scope_session_ != nullptr && !scope_session_->SetSource(config, diagnostic))
        {
            return false;
        }

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
        const bool composite = config.harmonic_distortion > 0.0 || config.noise_rms > 0.0;
        std::snprintf(name, sizeof(name), "%s%s %02llu",
                      composite ? "Composite " : "", SignalKindName(config.kind),
                      static_cast<unsigned long long>(serial));
        const std::uint32_t color = static_cast<std::uint32_t>(
            kSignalColors[(serial - 1) % kSignalColors.size()]);
        signals_.emplace_back(name, config, std::move(*block), *statistics, color);
        selected_signal_ = signals_.size() - 1;
        RebuildComposite();
        if (scope_session_ != nullptr)
        {
            scope_session_->Run();
        }
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
        scope_session_.reset();
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
            ImGui::TextDisabled("/  SIMULATED OSCILLOSCOPE");
            ImGui::TextDisabled("Generate, sample, trigger, and inspect a deterministic signal.");
            ImGui::Separator();
            const auto scope = scope_session_ != nullptr ? scope_session_->Snapshot() : nullptr;
            if (scope != nullptr)
            {
                const bool running = scope->transport == ScopeTransport::Running;
                const bool single_needs_arm =
                    scope->trigger_mode == ScopeTriggerMode::Single && !running &&
                    scope->transport == ScopeTransport::Stopped;
                const char *transport_action = running ? "Pause"
                    : single_needs_arm ? "Arm" : "Run";
                if (ImGui::Button(transport_action, {82.0f, 0.0f}))
                {
                    if (running)
                    {
                        scope_session_->Pause();
                    }
                    else if (single_needs_arm)
                    {
                        scope_session_->Arm(generator_message_);
                    }
                    else
                    {
                        scope_session_->Run();
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(scope->transport == ScopeTransport::Running);
                if (ImGui::Button("Step 256", {82.0f, 0.0f}))
                {
                    if (!scope_session_->Step(generator_message_))
                    {
                        // Keep the step diagnostic visible beside the controls.
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Reset", {68.0f, 0.0f}))
                {
                    scope_session_->Reset(generator_message_);
                }
                ImGui::SameLine();
                ImGui::TextColored(kAccent, "%s", scope->state_label.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%llu samples", static_cast<unsigned long long>(
                    scope->sample_position));

                const char *trigger_modes[]{"Auto", "Normal", "Single"};
                int trigger_mode = scope->trigger_mode == ScopeTriggerMode::Normal ? 1
                    : scope->trigger_mode == ScopeTriggerMode::Single ? 2 : 0;
                ImGui::SetNextItemWidth(112.0f);
                bool trigger_changed = ImGui::Combo("Trigger", &trigger_mode,
                                                     trigger_modes, 3);
                const char *trigger_edges[]{"Rising", "Falling"};
                int trigger_edge = scope->trigger_edge == ScopeTriggerEdge::Falling ? 1 : 0;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(108.0f);
                trigger_changed = ImGui::Combo("Edge", &trigger_edge,
                                                trigger_edges, 2) || trigger_changed;
                double trigger_level = scope->trigger_level;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(104.0f);
                trigger_changed = ImGui::InputDouble("Level (V)", &trigger_level,
                    0.0, 0.0, "%.3f") || trigger_changed;
                double trigger_hysteresis = scope->trigger_hysteresis;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(112.0f);
                trigger_changed = ImGui::InputDouble("Hysteresis", &trigger_hysteresis,
                    0.0, 0.0, "%.3f") || trigger_changed;
                if (trigger_changed)
                {
                    const auto mode = trigger_mode == 1 ? ScopeTriggerMode::Normal
                        : trigger_mode == 2 ? ScopeTriggerMode::Single
                        : ScopeTriggerMode::Auto;
                    const auto edge = trigger_edge == 1 ? ScopeTriggerEdge::Falling
                        : ScopeTriggerEdge::Rising;
                    scope_session_->SetTrigger(trigger_level, trigger_hysteresis,
                                               edge, mode, generator_message_);
                }
                if (!generator_message_.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4{0.92f, 0.57f, 0.52f, 1.0f}, "%s",
                                       generator_message_.c_str());
                }
                std::uint64_t holdoff_samples = scope->holdoff_samples;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(112.0f);
                if (ImGui::InputScalar("Holdoff (samples)", ImGuiDataType_U64,
                                       &holdoff_samples))
                {
                    if (scope_session_->SetHoldoffSamples(holdoff_samples,
                                                          generator_message_))
                    {
                        generator_message_.clear();
                    }
                }
                double timebase_ms = scope->timebase_ms;
                ImGui::SetNextItemWidth(112.0f);
                if (ImGui::InputDouble("Timebase (ms)", &timebase_ms,
                                       0.0, 0.0, "%.3f"))
                {
                    if (scope_session_->SetTimebaseMs(timebase_ms, generator_message_))
                    {
                        generator_message_.clear();
                    }
                }
            }
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.0f, 8.0f});
            const float content_height = ImGui::GetContentRegionAvail().y;
            const float panel_height = std::max(content_height - 4.0f, 160.0f);
            if (ImGui::BeginTable("##signal_workspace", 3,
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                    {0.0f, panel_height}))
            {
                ImGui::TableSetupColumn("Signals", ImGuiTableColumnFlags_WidthFixed, 270.0f);
                ImGui::TableSetupColumn("Waveform", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 245.0f);
                ImGui::TableNextColumn();
                const float generator_card_height = generator_realistic_
                    ? std::min(370.0f, std::max(345.0f, panel_height * 0.62f))
                    : std::min(320.0f, std::max(270.0f, panel_height * 0.50f));
                ImGui::BeginChild("##signal_generator_card", {0.0f, generator_card_height},
                                  true, ImGuiWindowFlags_NoScrollbar);
                RenderPanelHeading("SIGNAL GENERATOR", "SOURCE CONFIGURATION");
                const char *waveforms[]{"Sine", "Cosine", "Square"};
                const char *models[]{"Ideal", "Harmonics + noise"};
                int waveform = generator_kind_ == SignalKind::Cosine ? 1
                    : generator_kind_ == SignalKind::Square ? 2 : 0;
                int model = generator_realistic_ ? 1 : 0;
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {4.0f, 3.0f});
                if (ImGui::BeginTable("##signal_generator_fields", 2,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableSetupColumn("##generator_label",
                        ImGuiTableColumnFlags_WidthFixed, 86.0f);
                    ImGui::TableSetupColumn("##generator_value",
                        ImGuiTableColumnFlags_WidthStretch);
                    const auto field_label = [](const char *label)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%s", label);
                        ImGui::TableSetColumnIndex(1);
                    };
                    field_label("Waveform");
                    if (ImGui::Combo("##waveform", &waveform, waveforms, 3))
                    {
                        generator_kind_ = waveform == 1 ? SignalKind::Cosine
                            : waveform == 2 ? SignalKind::Square : SignalKind::Sine;
                    }
                    field_label("Model");
                    if (ImGui::Combo("##signal_model", &model, models, 2))
                    {
                        generator_realistic_ = model == 1;
                    }
                    field_label("Freq (Hz)");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##frequency_hz", &generator_frequency_hz_,
                                       0.0, 0.0, "%.1f");
                    field_label("Amp (V)");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##amplitude", &generator_amplitude_,
                                       0.0, 0.0, "%.3f");
                    if (generator_realistic_)
                    {
                        field_label("Harmonic (%)");
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputDouble("##harmonic_distortion",
                            &generator_harmonic_distortion_percent_, 0.0, 0.0, "%.1f");
                        field_label("Noise RMS (V)");
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputDouble("##noise_rms", &generator_noise_rms_,
                                           0.0, 0.0, "%.3f");
                    }
                    field_label("Phase (deg)");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##phase_degrees", &generator_phase_degrees_,
                                       0.0, 0.0, "%.1f");
                    field_label("Offset (V)");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##offset", &generator_offset_,
                                       0.0, 0.0, "%.3f");
                    field_label("Rate (Hz)");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##sample_rate_hz", &generator_sample_rate_hz_,
                                       0.0, 0.0, "%.0f");
                    ImGui::EndTable();
                }
                ImGui::PopStyleVar();
                ImGui::Spacing();
                ImGui::BeginDisabled(signals_.size() >= kSignalColors.size());
                if (ImGui::Button("Generate signal", {-1.0f, 0.0f}) &&
                    GenerateSignal(generator_message_))
                {
                    generator_message_.clear();
                }
                ImGui::EndDisabled();
                if (!generator_message_.empty())
                {
                    ImGui::TextColored(ImVec4{0.92f, 0.57f, 0.52f, 1.0f}, "%s",
                                       generator_message_.c_str());
                }
                ImGui::EndChild();

                ImGui::BeginChild("##signal_list_panel", {0.0f, 0.0f}, true,
                                  ImGuiWindowFlags_None);
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
                        generator_kind_ = signal.config.kind;
                        generator_realistic_ = signal.config.harmonic_distortion > 0.0 ||
                                               signal.config.noise_rms > 0.0;
                        generator_sample_rate_hz_ = signal.config.sample_rate_hz;
                        generator_frequency_hz_ = signal.config.frequency_hz;
                        generator_amplitude_ = signal.config.amplitude;
                        generator_harmonic_distortion_percent_ =
                            signal.config.harmonic_distortion * 100.0;
                        generator_noise_rms_ = signal.config.noise_rms;
                        generator_phase_degrees_ = signal.config.phase_radians *
                            180.0 / std::numbers::pi_v<double>;
                        generator_offset_ = signal.config.offset;
                        scope_session_->SetSource(signal.config, generator_message_);
                        scope_session_->Run();
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
                const char *display_modes[]{"Scope", "Overlay", "Composite"};
                int display_mode = display_mode_ == DisplayMode::Overlay ? 1
                    : display_mode_ == DisplayMode::Composite ? 2 : 0;
                ImGui::SetNextItemWidth(142.0f);
                if (ImGui::Combo("View", &display_mode, display_modes, 3))
                {
                    display_mode_ = display_mode == 1 ? DisplayMode::Overlay
                        : display_mode == 2 ? DisplayMode::Composite : DisplayMode::Scope;
                }
                const bool scope_mode = display_mode_ == DisplayMode::Scope;
                const bool composite_mode = display_mode_ == DisplayMode::Composite;
                RenderPanelHeading(scope_mode ? "SCOPE" : "GENERATOR VIEW",
                    scope_mode ? "VOLTAGE / TIME  ·  ADJUSTABLE TIMEBASE"
                               : composite_mode ? "TIME DOMAIN  /  COMPOSITE SUM"
                                                : "TIME DOMAIN  /  ALL SIGNALS");
                if (scope_mode && scope != nullptr)
                {
                    const bool visible = !signals_.empty() &&
                        signals_[std::min(selected_signal_, signals_.size() - 1)].visible;
                    const std::uint32_t color = signals_.empty() ? kSignalColors[0]
                        : signals_[std::min(selected_signal_, signals_.size() - 1)].color;
                    RenderScopePlot(*scope, static_cast<ImU32>(color), visible);
                    if (scope->capture != nullptr)
                    {
                        ImGui::TextColored(kAccent,
                            "Capture %llu  |  Vpp %.4f V  |  Vrms %.4f V  |  Frequency ",
                            static_cast<unsigned long long>(scope->capture_id),
                            scope->measurements.peak_to_peak, scope->measurements.rms);
                        ImGui::SameLine(0.0f, 0.0f);
                        if (scope->frequency_available)
                        {
                            ImGui::TextColored(kValue, "%.2f ± %.2f Hz",
                                scope->measured_frequency_hz,
                                scope->measured_frequency_uncertainty_hz);
                        }
                        else
                        {
                            ImGui::TextDisabled("unavailable");
                        }
                    }
                }
                else if (!scope_mode)
                {
                    RenderSignalPlot(signals_, composite_mode, composite_samples_,
                                     composite_valid_);
                    if (composite_mode && composite_valid_ && !composite_samples_.empty())
                    {
                        ImGui::TextColored(ImVec4{0.94f, 0.70f, 0.44f, 1.0f},
                            "Composite RMS %.4f  |  Peak-to-peak %.4f",
                            composite_statistics_.rms, composite_statistics_.peak_to_peak);
                    }
                    ImGui::TextDisabled("Select a signal on the left to inspect its properties.");
                }
                ImGui::EndChild();

                ImGui::TableNextColumn();
                ImGui::BeginChild("##signal_readout_panel", {0.0f, 0.0f}, true,
                                  ImGuiWindowFlags_NoScrollbar);
                if (!scope_mode)
                {
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
                        RenderProperty("Waveform", SignalKindName(signal.config.kind));
                        RenderProperty("Model",
                            signal.config.harmonic_distortion > 0.0 ||
                            signal.config.noise_rms > 0.0 ? "Harmonics + noise" : "Ideal");
                        std::snprintf(value, sizeof(value), "%.3f Hz",
                                      signal.config.frequency_hz);
                        RenderProperty("Frequency", value);
                        std::snprintf(value, sizeof(value), "%.4f", signal.config.amplitude);
                        RenderProperty("Amplitude", value);
                        if (signal.config.harmonic_distortion > 0.0 ||
                            signal.config.noise_rms > 0.0)
                        {
                            std::snprintf(value, sizeof(value), "%.1f %%",
                                signal.config.harmonic_distortion * 100.0);
                            RenderProperty("Harmonic level", value);
                            std::snprintf(value, sizeof(value), "%.4f Vrms",
                                          signal.config.noise_rms);
                            RenderProperty("Noise", value);
                        }
                        std::snprintf(value, sizeof(value), "%.2f deg",
                            signal.config.phase_radians * 180.0 / std::numbers::pi_v<double>);
                        RenderProperty("Phase", value);
                        std::snprintf(value, sizeof(value), "%.0f Hz", block.SampleRateHz());
                        RenderProperty("Sample rate", value);
                        std::snprintf(value, sizeof(value), "%zu", block.SampleCount());
                        RenderProperty("Samples", value);
                        RenderMeasurement("RMS", signal.statistics.rms);
                        RenderMeasurement("PEAK TO PEAK", signal.statistics.peak_to_peak);
                    }
                }
                else if (scope == nullptr || scope->capture == nullptr)
                {
                    RenderPanelHeading("ACQUISITION", "CAPTURE MEASUREMENTS");
                    ImGui::TextDisabled("Waiting for a complete triggered capture.");
                }
                else
                {
                    char value[48]{};
                    RenderPanelHeading("ACQUISITION", "CAPTURE MEASUREMENTS");
                    ImGui::TextColored(kAccent, "%s", scope->state_label.c_str());
                    RenderProperty("Capture", scope->stale ? "STALE" : "CURRENT");
                    RenderProperty("Waveform", SignalKindName(scope->source.kind));
                    RenderProperty("Model",
                        scope->source.harmonic_distortion > 0.0 ||
                        scope->source.noise_rms > 0.0 ? "Harmonics + noise" : "Ideal");
                    std::snprintf(value, sizeof(value), "%.3f Hz", scope->source.frequency_hz);
                    RenderProperty("Source freq", value);
                    std::snprintf(value, sizeof(value), "%.0f samples/s",
                                  scope->source.sample_rate_hz);
                    RenderProperty("Sample rate", value);
                    std::snprintf(value, sizeof(value), "%llu",
                        static_cast<unsigned long long>(scope->trigger_sample.value_or(0)));
                    RenderProperty("Trigger sample", scope->trigger_sample.has_value()
                        ? value : "Auto window");
                    std::snprintf(value, sizeof(value), "%llu",
                        static_cast<unsigned long long>(scope->epoch));
                    RenderProperty("Epoch", value);
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    RenderPanelHeading("MEASUREMENTS", "CAPTURE WINDOW");
                    RenderMeasurement("MEAN (V)", scope->measurements.mean);
                    RenderMeasurement("RMS (V)", scope->measurements.rms);
                    RenderMeasurement("MINIMUM (V)", scope->measurements.minimum);
                    RenderMeasurement("MAXIMUM (V)", scope->measurements.maximum);
                    RenderMeasurement("PEAK TO PEAK (V)", scope->measurements.peak_to_peak);
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
