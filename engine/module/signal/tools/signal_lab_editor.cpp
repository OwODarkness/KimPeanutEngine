#include "signal_lab_editor.h"
#include "signal_scope_session.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
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
            case SignalKind::Square: return "Square (naive)";
            case SignalKind::Sawtooth: return "Sawtooth (naive)";
            case SignalKind::Triangle: return "Triangle (naive)";
            case SignalKind::Constant: return "Constant";
            case SignalKind::Impulse: return "Impulse";
            case SignalKind::UniformNoise: return "Uniform noise";
            }
            return "Unknown";
        }

        bool IsUnbandlimitedKind(const SignalKind kind) noexcept
        {
            return kind == SignalKind::Square || kind == SignalKind::Sawtooth ||
                   kind == SignalKind::Triangle;
        }

        bool SelectPairSignal(const char *label, std::size_t &selected,
                              const std::span<const SignalLabSignal> signals)
        {
            if (signals.empty())
            {
                ImGui::TextDisabled("%s: No signals", label);
                return false;
            }

            selected = std::min(selected, signals.size() - 1);
            bool changed = false;
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::BeginCombo(label, signals[selected].name.c_str()))
            {
                for (std::size_t index = 0; index < signals.size(); ++index)
                {
                    const bool is_selected = selected == index;
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(signals[index].name.c_str(), is_selected))
                    {
                        selected = index;
                        changed = true;
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
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

        void RenderPairMixPlot(const SignalLabSignal &signal_a,
                               const SignalLabSignal &signal_b,
                               const SampleBlock &mixed_block)
        {
            const std::span<const double> samples_a = signal_a.samples.ChannelSamples(0);
            const std::span<const double> samples_b = signal_b.samples.ChannelSamples(0);
            const std::span<const double> samples_mix = mixed_block.ChannelSamples(0);
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 size{std::max(available.x, 240.0f),
                              std::max(available.y - 42.0f, 220.0f)};
            ImGui::InvisibleButton("##pair_mix_plot", size);
            const ImVec2 origin = ImGui::GetItemRectMin();
            const ImVec2 end = ImGui::GetItemRectMax();
            ImDrawList *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);
            draw->AddRect(origin, end, ImGui::GetColorU32(ImGuiCol_Border), 6.0f);

            if (samples_a.empty() || samples_a.size() != samples_b.size() ||
                samples_a.size() != samples_mix.size())
            {
                const char *message = "The selected pair has no aligned samples";
                draw->AddText({origin.x + 16.0f, origin.y + 16.0f},
                              ImGui::GetColorU32(kMuted), message);
                return;
            }

            const ImVec2 graph_min{origin.x + 58.0f, origin.y + 28.0f};
            const ImVec2 graph_max{end.x - 16.0f, end.y - 48.0f};
            const float width = std::max(graph_max.x - graph_min.x, 1.0f);
            const float height = std::max(graph_max.y - graph_min.y, 1.0f);
            const float center_y = (graph_min.y + graph_max.y) * 0.5f;
            double peak = 0.0;
            for (std::size_t sample = 0; sample < samples_mix.size(); ++sample)
            {
                peak = std::max({peak, std::abs(samples_a[sample]),
                                 std::abs(samples_b[sample]), std::abs(samples_mix[sample])});
            }
            const double tick = NiceAxisStep(std::max(peak / 3.0 * 1.1, 1.0e-12));
            const double limit = tick * 3.0;
            const ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border, 0.65f);
            const ImU32 axis = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImU32 labels = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            for (int division = 0; division <= 6; ++division)
            {
                const float fraction = static_cast<float>(division) / 6.0f;
                const float y = graph_min.y + fraction * height;
                draw->AddLine({graph_min.x, y}, {graph_max.x, y},
                              division == 3 ? axis : grid,
                              division == 3 ? 1.4f : 1.0f);
                char value[32]{};
                std::snprintf(value, sizeof(value), "%.3g", limit * (1.0 - 2.0 * fraction));
                const ImVec2 text = ImGui::CalcTextSize(value);
                draw->AddText({graph_min.x - text.x - 7.0f, y - text.y * 0.5f}, labels, value);
            }
            constexpr int kTimeDivisions = 5;
            const double duration_ms = static_cast<double>(samples_mix.size()) * 1'000.0 /
                                       mixed_block.SampleRateHz();
            for (int division = 0; division <= kTimeDivisions; ++division)
            {
                const float fraction = static_cast<float>(division) / kTimeDivisions;
                const float x = graph_min.x + fraction * width;
                draw->AddLine({x, graph_min.y}, {x, graph_max.y}, grid);
                char value[32]{};
                std::snprintf(value, sizeof(value), "%.3g", duration_ms * fraction);
                const ImVec2 text = ImGui::CalcTextSize(value);
                draw->AddText({x - text.x * 0.5f, graph_max.y + 7.0f}, labels, value);
            }
            draw->AddLine({graph_min.x, graph_min.y}, {graph_min.x, graph_max.y}, axis, 1.3f);
            draw->AddText({graph_min.x, origin.y + 5.0f}, labels, "PAIR MIX: A + B (V)");
            draw->AddText({graph_max.x - 58.0f, graph_max.y + 25.0f}, labels, "TIME (ms)");
            float legend_x = graph_max.x - 146.0f;
            const float legend_y = origin.y + 10.0f;
            const auto draw_legend_entry = [&](const char *name, const ImU32 color,
                                               const float entry_width)
            {
                draw->AddLine({legend_x, legend_y + 6.0f},
                              {legend_x + 12.0f, legend_y + 6.0f}, color, 2.0f);
                draw->AddText({legend_x + 16.0f, legend_y}, labels, name);
                legend_x += entry_width;
            };
            draw_legend_entry("A", static_cast<ImU32>(signal_a.color), 38.0f);
            draw_legend_entry("B", static_cast<ImU32>(signal_b.color), 38.0f);
            draw_legend_entry("A+B", IM_COL32(239, 178, 111, 255), 48.0f);

            std::array<ImVec2, 2'048> points{};
            const auto draw_trace = [&](const std::span<const double> samples,
                                        const ImU32 color, const float thickness)
            {
                const std::size_t count = std::min(samples.size(), points.size());
                for (std::size_t point = 0; point < count; ++point)
                {
                    const std::size_t index = count == 1 ? 0
                        : point * (samples.size() - 1) / (count - 1);
                    const float x = count == 1 ? (graph_min.x + graph_max.x) * 0.5f
                        : graph_min.x + width * static_cast<float>(point) /
                            static_cast<float>(count - 1);
                    const float y = center_y - static_cast<float>(samples[index] / limit) *
                        height * 0.5f;
                    points[point] = {x, y};
                }
                if (count > 1)
                {
                    draw->AddPolyline(points.data(), static_cast<int>(count), color,
                                      ImDrawFlags_None, thickness);
                }
            };
            draw->PushClipRect(graph_min, graph_max, true);
            draw_trace(samples_a, static_cast<ImU32>(signal_a.color), 1.5f);
            draw_trace(samples_b, static_cast<ImU32>(signal_b.color), 1.5f);
            draw_trace(samples_mix, IM_COL32(239, 178, 111, 255), 2.4f);
            draw->PopClipRect();

            if (ImGui::IsItemHovered())
            {
                const float cursor = std::clamp(ImGui::GetIO().MousePos.x - graph_min.x,
                                                0.0f, width);
                const std::size_t sample = static_cast<std::size_t>(cursor / width *
                    static_cast<float>(samples_mix.size() - 1));
                const float x = graph_min.x + cursor;
                draw->AddLine({x, graph_min.y}, {x, graph_max.y},
                              IM_COL32(210, 229, 229, 110));
                ImGui::BeginTooltip();
                ImGui::Text("Sample %zu  |  %.4f ms", sample,
                    static_cast<double>(sample) * 1'000.0 / mixed_block.SampleRateHz());
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                    static_cast<ImU32>(signal_a.color)), "A: %.6f V", samples_a[sample]);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                    static_cast<ImU32>(signal_b.color)), "B: %.6f V", samples_b[sample]);
                ImGui::TextColored(ImVec4{0.94f, 0.70f, 0.44f, 1.0f},
                                   "A + B: %.6f V", samples_mix[sample]);
                ImGui::EndTooltip();
            }
        }

        void RenderPhasePlot(const SignalLabSignal &signal_a,
                             const SignalLabSignal &signal_b)
        {
            const std::span<const double> samples_a = signal_a.samples.ChannelSamples(0);
            const std::span<const double> samples_b = signal_b.samples.ChannelSamples(0);
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 size{std::max(available.x, 240.0f),
                              std::max(available.y - 42.0f, 220.0f)};
            ImGui::InvisibleButton("##phase_plot", size);
            const ImVec2 origin = ImGui::GetItemRectMin();
            const ImVec2 end = ImGui::GetItemRectMax();
            ImDrawList *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);
            draw->AddRect(origin, end, ImGui::GetColorU32(ImGuiCol_Border), 6.0f);

            if (samples_a.empty() || samples_a.size() != samples_b.size())
            {
                const char *message = "The selected pair has no aligned samples";
                draw->AddText({origin.x + 16.0f, origin.y + 16.0f},
                              ImGui::GetColorU32(kMuted), message);
                return;
            }

            const ImVec2 graph_min{origin.x + 58.0f, origin.y + 24.0f};
            const ImVec2 graph_max{end.x - 16.0f, end.y - 50.0f};
            const float width = std::max(graph_max.x - graph_min.x, 1.0f);
            const float height = std::max(graph_max.y - graph_min.y, 1.0f);
            double peak = 0.0;
            for (std::size_t sample = 0; sample < samples_a.size(); ++sample)
            {
                peak = std::max({peak, std::abs(samples_a[sample]),
                                 std::abs(samples_b[sample])});
            }
            const double tick = NiceAxisStep(std::max(peak / 3.0 * 1.1, 1.0e-12));
            const double limit = tick * 3.0;
            const ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border, 0.65f);
            const ImU32 axis = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImU32 labels = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            for (int division = 0; division <= 6; ++division)
            {
                const float fraction = static_cast<float>(division) / 6.0f;
                const double x_value = -limit + 2.0 * limit * fraction;
                const double y_value = limit - 2.0 * limit * fraction;
                const float x = graph_min.x + width * fraction;
                const float y = graph_max.y - height * fraction;
                draw->AddLine({x, graph_min.y}, {x, graph_max.y},
                              division == 3 ? axis : grid,
                              division == 3 ? 1.4f : 1.0f);
                draw->AddLine({graph_min.x, y}, {graph_max.x, y},
                              division == 3 ? axis : grid,
                              division == 3 ? 1.4f : 1.0f);
                char value[32]{};
                std::snprintf(value, sizeof(value), "%.3g", y_value);
                const ImVec2 text = ImGui::CalcTextSize(value);
                draw->AddText({graph_min.x - text.x - 7.0f, y - text.y * 0.5f}, labels, value);
                std::snprintf(value, sizeof(value), "%.3g", x_value);
                const ImVec2 x_text = ImGui::CalcTextSize(value);
                draw->AddText({x - x_text.x * 0.5f, graph_max.y + 7.0f}, labels, value);
            }
            draw->AddText({graph_min.x, origin.y + 3.0f}, labels, "A (V)");
            draw->AddText({graph_max.x - 36.0f, graph_max.y + 26.0f}, labels, "B (V)");

            const std::size_t count = std::min<std::size_t>(samples_a.size(), 2'048);
            std::array<ImVec2, 2'048> points{};
            const auto map_point = [&](const std::size_t point)
            {
                const std::size_t sample = count == 1 ? 0
                    : point * (samples_a.size() - 1) / (count - 1);
                return ImVec2{
                    graph_min.x + (static_cast<float>(samples_a[sample] / limit) * 0.5f + 0.5f) * width,
                    graph_max.y - (static_cast<float>(samples_b[sample] / limit) * 0.5f + 0.5f) * height};
            };
            for (std::size_t point = 0; point < count; ++point)
            {
                points[point] = map_point(point);
            }
            draw->PushClipRect(graph_min, graph_max, true);
            if (count > 1)
            {
                draw->AddPolyline(points.data(), static_cast<int>(count),
                    (signal_a.color & 0x00FFFFFFU) | 0x28000000U,
                    ImDrawFlags_None, 5.0f);
                draw->AddPolyline(points.data(), static_cast<int>(count),
                    static_cast<ImU32>(signal_a.color), ImDrawFlags_None, 2.0f);
            }
            draw->PopClipRect();

            if (ImGui::IsItemHovered() && count > 0)
            {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                std::size_t nearest = 0;
                float nearest_distance = std::numeric_limits<float>::max();
                for (std::size_t point = 0; point < count; ++point)
                {
                    const float dx = points[point].x - mouse.x;
                    const float dy = points[point].y - mouse.y;
                    const float distance = dx * dx + dy * dy;
                    if (distance < nearest_distance)
                    {
                        nearest = point;
                        nearest_distance = distance;
                    }
                }
                draw->AddCircleFilled(points[nearest], 4.0f,
                                      static_cast<ImU32>(signal_a.color));
                const std::size_t sample = count == 1 ? 0
                    : nearest * (samples_a.size() - 1) / (count - 1);
                ImGui::BeginTooltip();
                ImGui::Text("Sample %zu  |  t = %.4f ms", sample,
                    static_cast<double>(sample) * 1'000.0 /
                        signal_a.samples.SampleRateHz());
                ImGui::Text("X (%s): %.6f V", signal_a.name.c_str(), samples_a[sample]);
                ImGui::Text("Y (%s): %.6f V", signal_b.name.c_str(), samples_b[sample]);
                ImGui::EndTooltip();
            }
        }

        void RenderSpectrumPlot(const Spectrum &spectrum,
                                const ImU32 trace_color,
                                const bool show_psd)
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 size{std::max(available.x, 240.0f),
                              std::max(available.y - 42.0f, 220.0f)};
            ImGui::InvisibleButton("##spectrum_plot", size);
            const ImVec2 origin = ImGui::GetItemRectMin();
            const ImVec2 end = ImGui::GetItemRectMax();
            ImDrawList *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);
            draw->AddRect(origin, end, ImGui::GetColorU32(ImGuiCol_Border), 6.0f);

            if (spectrum.bins.empty())
            {
                const char *message = "No spectrum bins available";
                const ImVec2 text = ImGui::CalcTextSize(message);
                draw->AddText({(origin.x + end.x - text.x) * 0.5f,
                               (origin.y + end.y - text.y) * 0.5f},
                              ImGui::GetColorU32(kMuted), message);
                return;
            }

            const ImVec2 graph_min{origin.x + 62.0f, origin.y + 24.0f};
            const ImVec2 graph_max{end.x - 14.0f, end.y - 48.0f};
            const float width = std::max(graph_max.x - graph_min.x, 1.0f);
            const float height = std::max(graph_max.y - graph_min.y, 1.0f);
            const auto value_for = [show_psd](const SpectrumBin &bin)
            {
                return show_psd ? bin.power_spectral_density : bin.peak_amplitude;
            };
            double maximum = 0.0;
            for (const SpectrumBin &bin : spectrum.bins)
            {
                maximum = std::max(maximum, value_for(bin));
            }
            const double tick = NiceAxisStep(std::max(maximum * 1.1 / 4.0, 1.0e-12));
            const double limit = tick * 4.0;
            const ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border, 0.65f);
            const ImU32 axis = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImU32 labels = ImGui::GetColorU32(ImGuiCol_TextDisabled);

            for (int division = 0; division <= 4; ++division)
            {
                const float fraction = static_cast<float>(division) / 4.0f;
                const float y = graph_min.y + fraction * height;
                draw->AddLine({graph_min.x, y}, {graph_max.x, y}, grid);
                char value[32]{};
                std::snprintf(value, sizeof(value), "%.3g", limit * (1.0 - fraction));
                const ImVec2 text = ImGui::CalcTextSize(value);
                draw->AddText({graph_min.x - text.x - 7.0f, y - text.y * 0.5f}, labels, value);
            }
            const double max_frequency = spectrum.sample_rate_hz * 0.5;
            constexpr int kFrequencyDivisions = 6;
            for (int division = 0; division <= kFrequencyDivisions; ++division)
            {
                const float fraction = static_cast<float>(division) /
                    static_cast<float>(kFrequencyDivisions);
                const float x = graph_min.x + fraction * width;
                draw->AddLine({x, graph_min.y}, {x, graph_max.y}, grid);
                char frequency[32]{};
                const double hz = max_frequency * fraction;
                if (max_frequency >= 1'000.0)
                {
                    std::snprintf(frequency, sizeof(frequency), "%.3g kHz", hz / 1'000.0);
                }
                else
                {
                    std::snprintf(frequency, sizeof(frequency), "%.3g Hz", hz);
                }
                const ImVec2 text = ImGui::CalcTextSize(frequency);
                draw->AddText({x - text.x * 0.5f, graph_max.y + 7.0f}, labels, frequency);
            }
            draw->AddLine({graph_min.x, graph_min.y}, {graph_min.x, graph_max.y}, axis, 1.3f);
            draw->AddText({graph_min.x, origin.y + 3.0f}, labels,
                          show_psd ? "POWER DENSITY (V^2/Hz)" : "PEAK AMPLITUDE (V)");
            const char *frequency_label = "FREQUENCY";
            const ImVec2 frequency_text = ImGui::CalcTextSize(frequency_label);
            draw->AddText({graph_max.x - frequency_text.x,
                           graph_max.y + 26.0f}, labels, frequency_label);

            std::array<ImVec2, 2'049> points{};
            const std::size_t point_count = std::min(points.size(), spectrum.bins.size());
            const auto map_y = [&](const double value)
            {
                return graph_max.y - static_cast<float>(value / limit) * height;
            };
            draw->PushClipRect(graph_min, graph_max, true);
            for (std::size_t point = 0; point < point_count; ++point)
            {
                const std::size_t bin = point_count == 1
                    ? 0 : point * (spectrum.bins.size() - 1) / (point_count - 1);
                const float x = point_count == 1
                    ? (graph_min.x + graph_max.x) * 0.5f
                    : graph_min.x + width * static_cast<float>(point) /
                          static_cast<float>(point_count - 1);
                points[point] = {x, map_y(value_for(spectrum.bins[bin]))};
            }
            if (point_count == 1)
            {
                draw->AddCircleFilled(points[0], 3.0f, trace_color);
            }
            else
            {
                draw->AddPolyline(points.data(), static_cast<int>(point_count),
                    (trace_color & 0x00FFFFFFU) | 0x28000000U, ImDrawFlags_None, 5.0f);
                draw->AddPolyline(points.data(), static_cast<int>(point_count),
                    trace_color, ImDrawFlags_None, 2.0f);
            }
            draw->PopClipRect();

            if (ImGui::IsItemHovered())
            {
                const float cursor = std::clamp(ImGui::GetIO().MousePos.x - graph_min.x,
                                                0.0f, width);
                const std::size_t bin = static_cast<std::size_t>(cursor / width *
                    static_cast<float>(spectrum.bins.size() - 1));
                const float x = graph_min.x + cursor;
                draw->AddLine({x, graph_min.y}, {x, graph_max.y},
                              IM_COL32(210, 229, 229, 110));
                const SpectrumBin &selected = spectrum.bins[bin];
                ImGui::BeginTooltip();
                ImGui::Text("Bin %zu  |  %.3f Hz", bin, selected.frequency_hz);
                ImGui::Text("Peak amplitude %.6g V", selected.peak_amplitude);
                ImGui::Text("PSD %.6g V^2/Hz", selected.power_spectral_density);
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
        config.harmonic_distortion = generator_realistic_ &&
            !IsUnbandlimitedKind(config.kind)
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
        RebuildPairMix();
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

    void SignalLabEditor::RebuildPairMix()
    {
        pair_mix_.reset();
        pair_mix_statistics_ = {};
        pair_mix_diagnostic_.clear();
        if (signals_.size() < 2)
        {
            pair_mix_diagnostic_ = "Generate at least two signals to analyze a pair.";
            return;
        }
        if (pair_signal_a_ >= signals_.size() || pair_signal_b_ >= signals_.size() ||
            pair_signal_a_ == pair_signal_b_)
        {
            pair_mix_diagnostic_ = "Select two different signals.";
            return;
        }

        pair_mix_ = MixSignals(signals_[pair_signal_a_].samples.View(),
                               signals_[pair_signal_b_].samples.View(),
                               pair_mix_diagnostic_);
        if (!pair_mix_.has_value())
        {
            return;
        }
        const std::optional<SignalStatistics> statistics = Measure(
            pair_mix_->ChannelSamples(0), pair_mix_diagnostic_);
        if (!statistics.has_value())
        {
            pair_mix_.reset();
            return;
        }
        pair_mix_statistics_ = *statistics;
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
            std::optional<Spectrum> spectrum;
            std::string spectrum_diagnostic;
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
                const float desired_generator_height = generator_realistic_
                    ? generator_advanced_open_ ? 292.0f : 222.0f
                    : generator_advanced_open_ ? 246.0f : 174.0f;
                const float maximum_generator_height =
                    std::max(112.0f, panel_height - 96.0f);
                const float generator_card_height =
                    std::min(desired_generator_height, maximum_generator_height);
                ImGui::BeginChild("##signal_generator_card", {0.0f, generator_card_height},
                                  true, ImGuiWindowFlags_None);
                ImGui::TextColored(kAccent, "SIGNAL GENERATOR");
                ImGui::SameLine();
                ImGui::TextDisabled("/ SOURCE CONFIGURATION");
                ImGui::Spacing();
                const bool unbandlimited_waveform = IsUnbandlimitedKind(generator_kind_);
                const char *waveforms[]{"Sine", "Cosine", "Square (naive)",
                                        "Sawtooth (naive)", "Triangle (naive)"};
                const char *models[]{unbandlimited_waveform ? "Naive waveform" : "Ideal",
                    unbandlimited_waveform ? "Naive + noise" : "Harmonics + noise"};
                int waveform = generator_kind_ == SignalKind::Cosine ? 1
                    : generator_kind_ == SignalKind::Square ? 2
                    : generator_kind_ == SignalKind::Sawtooth ? 3
                    : generator_kind_ == SignalKind::Triangle ? 4 : 0;
                int model = generator_realistic_ ? 1 : 0;
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {4.0f, 1.0f});
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4.0f, 2.0f});
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
                    if (ImGui::Combo("##waveform", &waveform, waveforms, 5))
                    {
                        generator_kind_ = waveform == 1 ? SignalKind::Cosine
                            : waveform == 2 ? SignalKind::Square
                            : waveform == 3 ? SignalKind::Sawtooth
                            : waveform == 4 ? SignalKind::Triangle : SignalKind::Sine;
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
                    if (generator_realistic_ && !unbandlimited_waveform)
                    {
                        field_label("Harmonic (%)");
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputDouble("##harmonic_distortion",
                            &generator_harmonic_distortion_percent_, 0.0, 0.0, "%.1f");
                    }
                    if (generator_realistic_)
                    {
                        field_label("Noise RMS (V)");
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputDouble("##noise_rms", &generator_noise_rms_,
                                           0.0, 0.0, "%.3f");
                    }
                    ImGui::EndTable();
                }
                ImGui::PopStyleVar(2);
                generator_advanced_open_ = ImGui::CollapsingHeader("Advanced");
                if (generator_advanced_open_ &&
                    ImGui::BeginTable("##signal_generator_advanced", 2,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableSetupColumn("##advanced_label",
                        ImGuiTableColumnFlags_WidthFixed, 86.0f);
                    ImGui::TableSetupColumn("##advanced_value",
                        ImGuiTableColumnFlags_WidthStretch);
                    const auto advanced_label = [](const char *label)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%s", label);
                        ImGui::TableSetColumnIndex(1);
                    };
                    advanced_label("Phase (deg)");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##phase_degrees", &generator_phase_degrees_,
                                       0.0, 0.0, "%.1f");
                    advanced_label("Offset (V)");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##offset", &generator_offset_,
                                       0.0, 0.0, "%.3f");
                    advanced_label("Rate (Hz)");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##sample_rate_hz", &generator_sample_rate_hz_,
                                       0.0, 0.0, "%.0f");
                    ImGui::EndTable();
                }
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
                const char *display_modes[]{"Scope", "Overlay", "Composite", "Spectrum",
                                            "Mix", "Phase"};
                int display_mode = display_mode_ == DisplayMode::Overlay ? 1
                    : display_mode_ == DisplayMode::Composite ? 2
                    : display_mode_ == DisplayMode::Spectrum ? 3
                    : display_mode_ == DisplayMode::Mix ? 4
                    : display_mode_ == DisplayMode::Phase ? 5 : 0;
                ImGui::SetNextItemWidth(142.0f);
                if (ImGui::Combo("View", &display_mode, display_modes, 6))
                {
                    display_mode_ = display_mode == 1 ? DisplayMode::Overlay
                        : display_mode == 2 ? DisplayMode::Composite
                        : display_mode == 3 ? DisplayMode::Spectrum
                        : display_mode == 4 ? DisplayMode::Mix
                        : display_mode == 5 ? DisplayMode::Phase : DisplayMode::Scope;
                }
                const bool scope_mode = display_mode_ == DisplayMode::Scope;
                const bool composite_mode = display_mode_ == DisplayMode::Composite;
                const bool spectrum_mode = display_mode_ == DisplayMode::Spectrum;
                const bool mix_mode = display_mode_ == DisplayMode::Mix;
                const bool phase_mode = display_mode_ == DisplayMode::Phase;
                const bool pair_mode = mix_mode || phase_mode;
                if (pair_mode)
                {
                    bool pair_changed = false;
                    ImGui::SameLine();
                    pair_changed = SelectPairSignal("Signal A", pair_signal_a_, signals_) ||
                                   pair_changed;
                    if (ImGui::GetContentRegionAvail().x < 320.0f)
                    {
                        ImGui::NewLine();
                    }
                    else
                    {
                        ImGui::SameLine();
                    }
                    pair_changed = SelectPairSignal("Signal B", pair_signal_b_, signals_) ||
                                   pair_changed;
                    if (pair_changed)
                    {
                        RebuildPairMix();
                    }
                }
                if (spectrum_mode)
                {
                    const bool compact_controls =
                        ImGui::GetContentRegionAvail().x < 760.0f;
                    const char *window_names[]{"Rectangular", "Periodic Hann"};
                    int window_index = spectrum_window_ == WindowKind::PeriodicHann ? 1 : 0;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(126.0f);
                    if (ImGui::Combo("Window", &window_index, window_names, 2))
                    {
                        spectrum_window_ = window_index == 1
                            ? WindowKind::PeriodicHann : WindowKind::Rectangular;
                    }

                    constexpr std::array<std::size_t, 4> kFftSizes{512, 1'024, 2'048, 4'096};
                    int fft_size_index = 0;
                    for (std::size_t index = 0; index < kFftSizes.size(); ++index)
                    {
                        if (spectrum_fft_size_ == kFftSizes[index])
                        {
                            fft_size_index = static_cast<int>(index);
                        }
                    }
                    const char *fft_size_names[]{"512", "1024", "2048", "4096"};
                    if (compact_controls)
                    {
                        ImGui::NewLine();
                    }
                    else
                    {
                        ImGui::SameLine();
                    }
                    ImGui::SetNextItemWidth(88.0f);
                    if (ImGui::Combo("FFT", &fft_size_index, fft_size_names, 4))
                    {
                        spectrum_fft_size_ = kFftSizes[static_cast<std::size_t>(fft_size_index)];
                    }

                    int scale_index = spectrum_show_psd_ ? 1 : 0;
                    const char *scale_names[]{"Amplitude", "PSD"};
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(104.0f);
                    if (ImGui::Combo("Scale", &scale_index, scale_names, 2))
                    {
                        spectrum_show_psd_ = scale_index == 1;
                    }

                    if (!signals_.empty())
                    {
                        selected_signal_ = std::min(selected_signal_, signals_.size() - 1);
                        const SignalLabSignal &selected = signals_[selected_signal_];
                        spectrum = ComputeSpectrum(selected.samples.ChannelSamples(0),
                            selected.samples.SampleRateHz(), spectrum_window_,
                            spectrum_fft_size_, spectrum_diagnostic);
                    }
                }
                RenderPanelHeading(scope_mode ? "SCOPE"
                                              : pair_mode ? "PAIR ANALYSIS" : "GENERATOR VIEW",
                    spectrum_mode ? spectrum_show_psd_
                        ? "ONE-SIDED POWER / FREQUENCY"
                        : "ONE-SIDED PEAK AMPLITUDE / FREQUENCY"
                    : mix_mode ? "SAMPLE-ALIGNED ADDITION: A + B"
                    : phase_mode ? "XY / LISSAJOUS: X = A, Y = B"
                    : scope_mode ? "VOLTAGE / TIME  ·  ADJUSTABLE TIMEBASE"
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
                else if (spectrum_mode)
                {
                    if (spectrum.has_value())
                    {
                        const ImU32 color = signals_.empty() ? kSignalColors[0]
                            : static_cast<ImU32>(signals_[selected_signal_].color);
                        RenderSpectrumPlot(*spectrum, color, spectrum_show_psd_);
                    }
                    else
                    {
                        ImGui::TextDisabled("%s", signals_.empty()
                            ? "Generate a signal to inspect its spectrum."
                            : spectrum_diagnostic.c_str());
                    }
                }
                else if (pair_mode)
                {
                    if (signals_.size() < 2)
                    {
                        ImGui::TextDisabled("Generate two signals to compare a pair.");
                    }
                    else if (pair_signal_a_ == pair_signal_b_)
                    {
                        ImGui::TextDisabled("Choose two different signals in the pair selectors.");
                    }
                    else if (!signals_[pair_signal_a_].visible ||
                             !signals_[pair_signal_b_].visible)
                    {
                        ImGui::TextDisabled("Show both selected signals to analyze the pair.");
                    }
                    else if (!pair_mix_.has_value())
                    {
                        ImGui::TextDisabled("%s", pair_mix_diagnostic_.c_str());
                    }
                    else if (mix_mode)
                    {
                        RenderPairMixPlot(signals_[pair_signal_a_],
                                          signals_[pair_signal_b_], *pair_mix_);
                    }
                    else
                    {
                        RenderPhasePlot(signals_[pair_signal_a_],
                                        signals_[pair_signal_b_]);
                    }
                }
                else if (pair_mode)
                {
                    RenderPanelHeading(mix_mode ? "PAIR MIX" : "PHASE VIEW",
                                       "ALIGNED SIGNALS");
                    if (signals_.size() < 2 || pair_signal_a_ == pair_signal_b_ ||
                        !pair_mix_.has_value())
                    {
                        ImGui::TextDisabled("%s", pair_mix_diagnostic_.empty()
                            ? "Choose two valid signals to continue."
                            : pair_mix_diagnostic_.c_str());
                    }
                    else
                    {
                        const SignalLabSignal &signal_a = signals_[pair_signal_a_];
                        const SignalLabSignal &signal_b = signals_[pair_signal_b_];
                        RenderProperty("A", signal_a.name.c_str());
                        RenderProperty("B", signal_b.name.c_str());
                        char value[48]{};
                        if (mix_mode)
                        {
                            RenderMeasurement("MIX RMS (V)", pair_mix_statistics_.rms);
                            RenderMeasurement("MIX PEAK TO PEAK (V)",
                                              pair_mix_statistics_.peak_to_peak);
                            RenderProperty("Operation", "A[n] + B[n]");
                        }
                        else
                        {
                            std::snprintf(value, sizeof(value), "%.3f Hz",
                                          signal_a.config.frequency_hz);
                            RenderProperty("Freq A", value);
                            std::snprintf(value, sizeof(value), "%.3f Hz",
                                          signal_b.config.frequency_hz);
                            RenderProperty("Freq B", value);
                            const double phase_delta = std::remainder(
                                (signal_b.config.phase_radians - signal_a.config.phase_radians) *
                                    180.0 / std::numbers::pi_v<double>, 360.0);
                            std::snprintf(value, sizeof(value), "%.2f deg", phase_delta);
                            RenderProperty("Start phase Δ", value);
                            ImGui::TextWrapped("Each point uses the same sample index: "
                                               "X = A[n], Y = B[n].");
                        }
                        std::snprintf(value, sizeof(value), "%zu",
                                      pair_mix_->SampleCount());
                        RenderProperty("Samples", value);
                        std::snprintf(value, sizeof(value), "%.3f ms",
                            static_cast<double>(pair_mix_->SampleCount()) * 1'000.0 /
                                pair_mix_->SampleRateHz());
                        RenderProperty("Window", value);
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
                                  ImGuiWindowFlags_None);
                if (spectrum_mode)
                {
                    RenderPanelHeading("SPECTRUM", "SELECTED SIGNAL");
                    if (signals_.empty())
                    {
                        ImGui::TextDisabled("No signal selected.");
                    }
                    else if (!spectrum.has_value())
                    {
                        ImGui::TextColored(ImVec4{0.92f, 0.57f, 0.52f, 1.0f},
                                           "%s", spectrum_diagnostic.c_str());
                    }
                    else
                    {
                        const SignalLabSignal &selected = signals_[selected_signal_];
                        RenderProperty("Input", selected.name.c_str());
                        RenderProperty("Window", spectrum_window_ == WindowKind::PeriodicHann
                            ? "Periodic Hann" : "Rectangular");
                        char value[48]{};
                        std::snprintf(value, sizeof(value), "%zu", spectrum->fft_size);
                        RenderProperty("FFT size", value);
                        std::snprintf(value, sizeof(value), "%.3f Hz",
                            spectrum->sample_rate_hz / static_cast<double>(spectrum->fft_size));
                        RenderProperty("Bin spacing", value);
                        std::snprintf(value, sizeof(value), "%.4f",
                                      spectrum->coherent_gain);
                        RenderProperty("Coherent gain", value);
                        auto peak = spectrum->bins.begin();
                        if (spectrum->bins.size() > 1)
                        {
                            peak = std::max_element(spectrum->bins.begin() + 1,
                                spectrum->bins.end(), [](const SpectrumBin &lhs,
                                                         const SpectrumBin &rhs)
                            {
                                return lhs.peak_amplitude < rhs.peak_amplitude;
                            });
                        }
                        if (peak != spectrum->bins.end())
                        {
                            RenderMeasurement("PEAK FREQUENCY (HZ)", peak->frequency_hz);
                            RenderMeasurement("PEAK AMPLITUDE (V)", peak->peak_amplitude);
                        }
                        double integrated_psd = 0.0;
                        const double bin_width = spectrum->sample_rate_hz /
                            static_cast<double>(spectrum->fft_size);
                        for (const SpectrumBin &bin : spectrum->bins)
                        {
                            integrated_psd += bin.power_spectral_density * bin_width;
                        }
                        RenderMeasurement("INTEGRATED PSD (V^2)", integrated_psd);
                    }
                }
                else if (!scope_mode)
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
                        RenderProperty("Model", IsUnbandlimitedKind(signal.config.kind)
                            ? "Naive / unbandlimited"
                            : signal.config.harmonic_distortion > 0.0 ||
                              signal.config.noise_rms > 0.0
                                ? "Harmonics + noise" : "Ideal");
                        std::snprintf(value, sizeof(value), "%.3f Hz",
                                      signal.config.frequency_hz);
                        RenderProperty("Frequency", value);
                        std::snprintf(value, sizeof(value), "%.4f", signal.config.amplitude);
                        RenderProperty("Amplitude", value);
                        if (signal.config.harmonic_distortion > 0.0 ||
                            signal.config.noise_rms > 0.0)
                        {
                            if (signal.config.harmonic_distortion > 0.0)
                            {
                                std::snprintf(value, sizeof(value), "%.1f %%",
                                    signal.config.harmonic_distortion * 100.0);
                                RenderProperty("Harmonic level", value);
                            }
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
