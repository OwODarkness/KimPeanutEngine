#include "audio_preview_widget.h"
#include "audio_player_theme.h"
#include "audio_transport_controls.h"
#include "editor/ui/api/editor_ui/ui_plot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace kpengine::audio_player
{
    namespace
    {
        using namespace editor::ui;
        using Theme = AudioPlayerTheme;
        using Icon = editor::EditorIcon;

        class Plot final
        {
        public:
            explicit Plot(const UiVec2 extent) : size(extent) {}

            void Line(const UiVec2 a, const UiVec2 b, const UiColor color,
                      const float width = 1.0F, const float emission = 0.0F)
            {
                UiCanvasPrimitive mark{UiCanvasPrimitiveKind::Line, Point(a), Point(b), color, width};
                mark.emission_strength = emission;
                marks.push_back(std::move(mark));
            }

            void Box(const UiVec2 a, const UiVec2 b, const UiColor color, const bool fill = true)
            {
                marks.push_back({fill ? UiCanvasPrimitiveKind::FilledRect : UiCanvasPrimitiveKind::Rect,
                                 Point(a), Point(b), color});
            }

            void Dot(const UiVec2 center, const float radius, const UiColor color,
                     const float emission = 0.0F)
            {
                UiCanvasPrimitive mark{.kind = UiCanvasPrimitiveKind::Circle,
                    .first = Point(center), .color = color, .radius = radius / std::min(size.x, size.y)};
                mark.emission_strength = emission;
                marks.push_back(std::move(mark));
            }

            void Label(const UiVec2 point, const UiColor color, std::string label,
                       const UiVec2 anchor = {}, const float scale = 1.0F)
            {
                marks.push_back({.kind = UiCanvasPrimitiveKind::Text, .first = Point(point),
                    .color = color, .text = std::move(label), .text_anchor = anchor,
                    .text_scale = scale});
            }

            UiCanvasResult Draw(UiContext &ui, const UiControlDescriptor &control,
                                std::optional<UiChartObservation> chart = {})
            {
                return ui.Canvas(control, size, std::move(marks), std::move(chart));
            }

        private:
            UiVec2 Point(const UiVec2 pixel) const noexcept
            {
                return {pixel.x / size.x, pixel.y / size.y};
            }

            UiVec2 size;
            std::vector<UiCanvasPrimitive> marks;
        };

        UiColor Opacity(UiColor color, const float alpha)
        {
            color.a = alpha;
            return color;
        }

        float SmoothFade(const float distance, const float fade_distance)
        {
            const float value = std::clamp(distance / fade_distance, 0.0F, 1.0F);
            return value * value * (3.0F - 2.0F * value);
        }

        UiColor BlendColor(const UiColor from, const UiColor to, const float amount)
        {
            const float value = std::clamp(amount, 0.0F, 1.0F);
            return {std::lerp(from.r, to.r, value), std::lerp(from.g, to.g, value),
                    std::lerp(from.b, to.b, value), std::lerp(from.a, to.a, value)};
        }

        std::string TimeLabel(const float time, const bool tenths = false)
        {
            const auto value = static_cast<unsigned>(std::max(0.0F, time) * 10.0F);
            char text[24]{};
            if (tenths)
                std::snprintf(text, sizeof(text), "%02u:%02u.%u", value / 600, value / 10 % 60, value % 10);
            else
                std::snprintf(text, sizeof(text), "%02u:%02u", value / 600, value / 10 % 60);
            return text;
        }

        const char *StateLabel(const PlaybackView &playback)
        {
            if (!playback.error.empty()) return "ERROR";
            if (!playback.track) return "IDLE";
            switch (playback.state)
            {
            case audio::AudioState::Playing: return "PLAYING";
            case audio::AudioState::Buffering: return "BUFFERING";
            case audio::AudioState::Paused: return "PAUSED";
            default: return "READY";
            }
        }

        void DrawRadialScope(Plot &plot, const UiVec2 origin, const float side, const PlaybackView &playback)
        {
            const UiVec2 end{origin.x + side, origin.y + side};
            const UiVec2 center{origin.x + side * 0.5F, origin.y + side * 0.5F};
            plot.Box(origin, end, Theme::border, false);
            const float corner = side * 0.04F;
            for (const auto [x, y] : {UiVec2{origin.x, origin.y}, UiVec2{end.x, origin.y},
                                      UiVec2{origin.x, end.y}, end})
            {
                plot.Line({x, y}, {x + (x < center.x ? corner : -corner), y}, Theme::cyan, 2.0F);
                plot.Line({x, y}, {x, y + (y < center.y ? corner : -corner)}, Theme::cyan, 2.0F);
            }
            const auto at = [center](const float radius, const float angle)
            {
                return UiVec2{center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
            };
            constexpr float turn = 2.0F * std::numbers::pi_v<float>;
            const float radius = std::max(1.0F,
                (side * 0.5F - 10.0F) / 1.08F);
            const float rms = std::clamp(playback.rms, 0.0F, 1.0F);
            const auto digital_ring = [&](const float ring_radius, const int count,
                                           const float dot_radius, const UiColor color)
            {
                for (int index = 0; index < count; ++index)
                {
                    const float angle = -std::numbers::pi_v<float> * 0.5F + turn *
                        static_cast<float>(index) / static_cast<float>(count);
                    plot.Dot(at(ring_radius, angle), dot_radius, color);
                }
            };
            digital_ring(radius, 64, 1.35F, Theme::cyan_dim);
            digital_ring(radius * 0.72F, 48, 1.25F, Theme::cyan_dim);
            digital_ring(radius * (0.39F + rms * 0.07F), 36, 1.5F, Theme::cyan);

            const float progress = playback.duration_seconds > 0.0F
                ? std::clamp(playback.position_seconds / playback.duration_seconds, 0.0F, 1.0F) : 0.0F;
            if (progress > 0.0F)
            {
                const int count = std::clamp(static_cast<int>(std::ceil(progress * 64.0F)), 1, 64);
                for (int index = 0; index < count; ++index)
                {
                    const float angle = -std::numbers::pi_v<float> * 0.5F + progress * turn *
                        static_cast<float>(index) / static_cast<float>(count);
                    plot.Dot(at(radius * 1.08F, angle), 2.0F, Theme::amber);
                }
            }

            const bool signal_active = playback.state == audio::AudioState::Playing ||
                                       playback.state == audio::AudioState::Buffering;
            if (!playback.track || !signal_active || playback.spectrum.empty())
            {
                plot.Label(center, Theme::muted, playback.track ? "IDLE" : "NO SIGNAL",
                           {0.5F, 0.5F});
                return;
            }

            const std::size_t count = std::min<std::size_t>(playback.spectrum.size(), 64);
            float strongest_magnitude = 0.0F;
            for (std::size_t index = 0; index < count; ++index)
                strongest_magnitude = std::max(strongest_magnitude,
                    std::clamp(playback.spectrum[index], 0.0F, 1.0F));
            const float accent_threshold = strongest_magnitude * 0.78F;
            for (std::size_t index = 0; index < count; ++index)
            {
                const float magnitude = std::clamp(playback.spectrum[index], 0.0F, 1.0F);
                const float angle = -std::numbers::pi_v<float> * 0.5F +
                    static_cast<float>(index) * turn / static_cast<float>(count);
                const float inner = radius * 0.53F;
                const float outer = inner + radius * (0.06F + magnitude * 0.34F);
                const int dot_count = std::clamp(
                    static_cast<int>((outer - inner) / 4.0F) + 1, 2, 10);
                const bool accent_bar = strongest_magnitude > 0.02F &&
                                        magnitude >= accent_threshold;
                for (int dot = 0; dot < dot_count; ++dot)
                {
                    const float fraction = static_cast<float>(dot) /
                                           static_cast<float>(dot_count - 1);
                    const UiVec2 position = at(inner + (outer - inner) * fraction, angle);
                    const UiColor color = accent_bar && dot >= dot_count / 2
                        ? Theme::amber : Theme::cyan;
                    plot.Box({position.x - 1.25F, position.y - 1.25F},
                             {position.x + 1.25F, position.y + 1.25F}, color);
                }
            }
            char rms_text[16]{};
            std::snprintf(rms_text, sizeof(rms_text), "RMS %02d",
                static_cast<int>(std::round(rms * 99.0F)));
            plot.Label(center, Theme::cyan, rms_text, {0.5F, 0.5F});
        }

        struct WaveformView
        {
            UiVec2 min;
            UiVec2 max;
            float start = 0.0F;
            float duration = 0.0F;
        };

        void DrawWaveform(Plot &plot, const WaveformView &view, const PlaybackView &playback,
                          UiChartObservation *observation)
        {
            const auto [left, top] = view.min;
            const auto [right, bottom] = view.max;
            plot.Box(view.min, view.max, Theme::border, false);
            if (!playback.track || !playback.track->waveform ||
                playback.track->waveform->empty() || view.duration <= 0.0F)
            {
                const float middle = (top + bottom - 22.0F) * 0.5F;
                plot.Label({left + 9.0F, middle}, Theme::muted, "LOAD CLIP FOR WAVEFORM", {0.0F, 0.5F});
                if (observation) observation->empty_label = "Load a clip to view its waveform";
                return;
            }

            constexpr float axis_height = 22.0F;
            constexpr float horizontal_inset = 10.0F;
            constexpr float column_spacing = 12.0F;
            const float window_start = view.start;
            const float window_end = window_start + view.duration;
            const float plot_left = left + horizontal_inset;
            const float plot_right = right - horizontal_inset;
            const float plot_width = std::max(1.0F, plot_right - plot_left);
            const float axis_y = bottom - axis_height;
            const float plot_top = top + 7.0F;
            const float plot_bottom = axis_y - 5.0F;
            const float plot_height = std::max(1.0F, plot_bottom - plot_top);
            const float middle = plot_top + plot_height * 0.5F;
            constexpr float grid_spacing = 16.0F;
            const UiColor grid_dot{15.0F / 255.0F, 56.0F / 255.0F, 70.0F / 255.0F, 1.0F};
            for (float y = plot_top; y < plot_bottom; y += grid_spacing)
                for (float x = plot_left; x < plot_right; x += grid_spacing)
                    plot.Box({x, y}, {x + 1.0F, y + 1.0F}, grid_dot);
            plot.Line({plot_left, middle}, {plot_right, middle}, Theme::cyan_dim);

            UiChartSeriesObservation *observed_bars = nullptr;
            if (observation)
            {
                observed_bars = &observation->series.emplace_back();
                observed_bars->label = "Waveform envelope";
                observed_bars->kind = UiPlotSeriesKind::Bars;
                observed_bars->emission_strength = 0.55F;
            }
            const auto &samples = *playback.track->waveform;
            const int tick_interval = std::max(1, static_cast<int>(std::ceil(
                72.0F * view.duration / plot_width)));
            const int first_tick = static_cast<int>(std::ceil(
                window_start / static_cast<float>(tick_interval))) * tick_interval;
            for (int tick = first_tick; static_cast<float>(tick) <= window_end; tick += tick_interval)
            {
                const float fraction = (static_cast<float>(tick) - window_start) / view.duration;
                const float x = plot_left + fraction * plot_width;
                const float edge_distance = std::min(x - plot_left, plot_right - x);
                const float edge_alpha = SmoothFade(edge_distance, 40.0F);
                const UiColor axis_grid{19.0F / 255.0F, 66.0F / 255.0F,
                                        81.0F / 255.0F, edge_alpha};
                plot.Line({x, plot_top}, {x, axis_y}, axis_grid);
                plot.Line({x, axis_y - 3.0F}, {x, axis_y + 2.0F},
                          Opacity(Theme::cyan_dim, edge_alpha));
                if (x > plot_left + 17.0F && x < plot_right - 17.0F)
                    plot.Label({x, axis_y + 3.0F}, Opacity(Theme::muted, edge_alpha),
                               TimeLabel(static_cast<float>(tick)), {0.5F, 0.0F});
            }
            plot.Line({plot_left, axis_y}, {plot_right, axis_y}, Theme::cyan_dim);

            const float column_duration = view.duration * column_spacing / plot_width;
            const float column_width = plot_width * column_duration / view.duration;
            const float bar_width = std::min(2.5F, column_width * 0.5F);
            const std::size_t first_column = static_cast<std::size_t>(
                std::floor(window_start / column_duration));
            const std::size_t last_column = static_cast<std::size_t>(
                std::ceil(window_end / column_duration));
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            const float phase = std::chrono::duration<float>(now).count();
            for (std::size_t column = first_column; column < last_column; ++column)
            {
                const float start_time = static_cast<float>(column) * column_duration;
                const float end_time = std::min(playback.duration_seconds,
                                                start_time + column_duration);
                const float time = (start_time + end_time) * 0.5F;
                const float source_position = std::clamp(
                    time / playback.duration_seconds * static_cast<float>(samples.size()) - 0.5F,
                    0.0F, static_cast<float>(samples.size() - 1));
                const std::size_t first_sample = static_cast<std::size_t>(source_position);
                const std::size_t second_sample = std::min(first_sample + 1, samples.size() - 1);
                const float sample_blend = source_position - static_cast<float>(first_sample);
                const float amplitude = std::clamp(std::lerp(samples[first_sample],
                    samples[second_sample], sample_blend), 0.0F, 1.0F);
                const float x = plot_left + (time - window_start) / view.duration * plot_width;
                if (x < plot_left || x > plot_right) continue;
                if (observed_bars)
                {
                    ++observed_bars->point_count;
                    if (observed_bars->points.size() < 128)
                        observed_bars->points.push_back({time, amplitude});
                    else
                        observation->truncated = true;
                }
                const float proximity = std::clamp(
                    1.0F - std::abs(time - playback.position_seconds) / 2.5F, 0.0F, 1.0F);
                const float pulse = playback.state == audio::AudioState::Playing
                    ? std::sin(phase * 12.0F + static_cast<float>(column) * 0.37F) *
                        std::clamp(playback.rms, 0.0F, 1.0F) * proximity * 0.55F
                    : 0.0F;
                const float half = std::max(1.0F,
                    amplitude * plot_height * 0.46F * (1.0F + pulse));
                const UiVec2 a{x, middle - half};
                const UiVec2 b{x, middle + half};
                const float behind_playhead = playback.position_seconds - time;
                const float fade = behind_playhead >= 0.0F
                    ? std::clamp(1.0F - behind_playhead / 1.5F, 0.0F, 1.0F) : 0.0F;
                const float orange_amount = fade * fade * (3.0F - 2.0F * fade);
                const UiColor color = BlendColor(Theme::cyan, Theme::amber, orange_amount);
                plot.Line(a, b, color, bar_width, 0.55F);
            }

            const float playhead = plot_left + std::clamp(
                (playback.position_seconds - window_start) / view.duration, 0.0F, 1.0F) * plot_width;
            const UiVec2 a{playhead, plot_top};
            const UiVec2 b{playhead, plot_bottom};
            plot.Line(a, b, Theme::amber, 1.5F, 1.0F);
            for (const UiVec2 endpoint : {a, b})
            {
                plot.Dot(endpoint, 5.0F, Opacity(Theme::amber, 64.0F / 255.0F), 0.25F);
                plot.Dot(endpoint, 2.5F, Theme::amber, 1.0F);
            }
        }

        bool ClickIn(const UiCanvasResult &hit, const UiVec2 size, const WaveformView &wave)
        {
            const float x = hit.normalized_pointer.x * size.x;
            const float y = hit.normalized_pointer.y * size.y;
            return hit.pressed && x >= wave.min.x && x <= wave.max.x &&
                   y >= wave.min.y && y <= wave.max.y;
        }

    }

    AudioPreviewActions DrawAudioPreview(editor::ui::UiContext &ui, const PlaybackView &playback,
                                         const editor::EditorBuiltinIcons &icons)
    {
        AudioPreviewActions actions{};
        ui.Section("// AUDIO PREVIEW");
        const auto available = ui.AvailableContent();
        const float width = std::max(1.0F, available.x);
        const float height = std::max(146.0F, available.y - 82.0F);
        const UiVec2 size{width, height};
        Plot plot{size};
        plot.Box({}, size, Theme::surface);
        plot.Box({}, size, Theme::border, false);
        const bool playing = playback.state == audio::AudioState::Playing ||
                             playback.state == audio::AudioState::Buffering;
        const bool enabled = playback.track.has_value();
        plot.Label({width - 11.0F, 8.0F}, playing ? Theme::amber : Theme::muted,
                   StateLabel(playback), {1.0F, 0.0F});
        const float scope = width >= 350.0F ? std::min(width * 0.29F, height - 36.0F) : 0.0F;
        if (scope > 0.0F) DrawRadialScope(plot, {1.0F, 23.0F}, scope, playback);
        const float wave_left = scope + (scope > 0.0F ? 12.0F : 6.0F);
        const float title_width = width - wave_left - 8.0F;
        if (title_width > 56.0F)
        {
            plot.Label({wave_left, 27.0F}, Theme::cyan,
                       enabled ? playback.track->name : "NO TRACK SELECTED", {}, 1.40F);
            if (enabled)
                plot.Label({wave_left, 53.0F}, Theme::muted, playback.track->extension + " // " +
                    std::to_string(playback.track->sample_rate) + " Hz // " +
                    (playback.track->channels == 1 ? "MONO" : "STEREO"));
        }
        const float wave_top = 77.0F;
        const float wave_bottom = std::min(height - 20.0F, wave_top + std::clamp(height * 0.53F, 54.0F, 220.0F));
        const float visible = std::clamp(playback.duration_seconds, 0.0F, 18.0F);
        const WaveformView wave{{wave_left, wave_top}, {width - 5.0F, wave_bottom},
            std::clamp(playback.position_seconds - visible * 0.5F, 0.0F,
                       std::max(0.0F, playback.duration_seconds - visible)), visible};
        std::optional<UiChartObservation> waveform;
        std::string preview_summary;
        if (ui.IsObserving())
        {
            preview_summary = StateLabel(playback);
            if (playback.track) preview_summary += " — " + playback.track->name;
            if (scope > 0.0F && playback.track)
                preview_summary += " | Radial level meter: RMS " + std::to_string(
                    static_cast<int>(std::clamp(playback.rms, 0.0F, 1.0F) * 100.0F));
        }
        if (wave.max.x - wave.min.x > 16.0F)
        {
            if (ui.IsObserving())
            {
                waveform.emplace();
                waveform->x = {"Track time", "s", wave.start, wave.start + wave.duration};
                waveform->y = {"Normalized amplitude", "", 0.0, 1.0};
            }
            DrawWaveform(plot, wave, playback, waveform ? &*waveform : nullptr);
            if (height - wave_bottom > 32.0F)
            {
                plot.Label({wave_left + 9.0F, wave_bottom + 17.0F}, Theme::cyan, "// SUBTITLE");
                if (!playback.subtitle_text.empty())
                {
                    plot.Label({std::midpoint(wave.min.x, wave.max.x), (wave_bottom + height) * 0.5F + 12.0F},
                               Theme::text, playback.subtitle_text, {0.5F, 0.5F}, 1.45F);
                    if (ui.IsObserving()) preview_summary += " | Subtitle: " + playback.subtitle_text;
                }
            }
        }
        const auto hit = plot.Draw(ui, {.id = "audio.now_playing.waveform", .label = "Audio preview",
            .description = "Click the waveform to seek within the visible time window",
            .enabled = playback.can_seek,
            .unit = "s",
            .domain_summary = preview_summary,
            .semantic_value = editor::ui::UiValue{static_cast<double>(playback.position_seconds)},
            .semantic_minimum = editor::ui::UiValue{0.0},
            .semantic_maximum = editor::ui::UiValue{static_cast<double>(playback.duration_seconds)},
            .semantic_actions = {editor::ui::UiActionKind::Edit,
                                 editor::ui::UiActionKind::Commit}}, std::move(waveform));
        if (playback.can_seek && visible > 0.0F && ClickIn(hit, size, wave))
            actions.seek_seconds = wave.start + visible * (hit.normalized_pointer.x * width - wave.min.x) /
                                                   (wave.max.x - wave.min.x);

        const float fraction = playback.duration_seconds > 0.0F
            ? playback.position_seconds / playback.duration_seconds : 0.0F;
        audio_editor::AudioProgressStyle progress;
        progress.glow = true;
        const std::string timing = TimeLabel(playback.position_seconds, true) + " / " +
                                   TimeLabel(playback.duration_seconds, true);
        if (const auto edit = audio_editor::DrawAudioProgressControl(ui,
                {.id = "audio.now_playing.seek", .label = "Playback position",
                 .description = "Seek to a position within the selected track", .enabled = playback.can_seek,
                 .unit = "s",
                 .semantic_value = editor::ui::UiValue{static_cast<double>(playback.position_seconds)},
                 .semantic_minimum = editor::ui::UiValue{0.0},
                 .semantic_maximum = editor::ui::UiValue{static_cast<double>(playback.duration_seconds)},
                 .semantic_actions = {editor::ui::UiActionKind::Edit,
                                      editor::ui::UiActionKind::Commit}},
                fraction, {width, 23.0F}, timing, progress))
            actions.seek_seconds = *edit * playback.duration_seconds;

        const float spacing = Theme::style.item_spacing_x;
        const float button_width = std::clamp(width * 0.075F, 29.0F, 78.0F);
        const float rate_width = std::clamp(width * 0.12F, 61.0F, 132.0F);
        const float mute_width = std::clamp(width * 0.06F, 29.0F, 60.0F);
        const auto button = [&](const char *id, const char *label, const Icon icon, const float w,
                                const bool active = false, const bool available = true)
        {
            return ui.Button({.id = id, .label = label, .enabled = available, .width = w, .height = 36.0F,
                .text_role = active ? UiTextRole::Warning : UiTextRole::Accent,
                .emphasized = active, .icon = icons.Get(icon)}).activated;
        };
        actions.previous = button("audio.now_playing.previous", "Previous", Icon::SkipPrevious, button_width, false, enabled);
        ui.SameLine();
        actions.toggle_play_pause = button("audio.now_playing.toggle", playing ? "Pause" : "Play",
                                           playing ? Icon::Pause : Icon::Play, button_width, playing, enabled);
        ui.SameLine();
        actions.next = button("audio.now_playing.next", "Next", Icon::SkipNext, button_width, false, enabled);
        ui.SameLine();
        actions.stop = button("audio.now_playing.stop", "Stop", Icon::Stop, button_width, false, enabled);
        ui.SameLine();
        actions.toggle_loop = button("audio.now_playing.loop", "Loop track", Icon::Repeat, button_width, playback.loop_track);

        const float used = button_width * 5.0F + rate_width + mute_width + spacing * 7.0F;
        const bool inline_extras = width - used >= 50.0F;
        if (inline_extras) ui.SameLine();
        constexpr std::array rates{0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
        std::vector<UiChoice> choices;
        choices.reserve(rates.size() + 1);
        for (const auto rate : rates)
        {
            char label[16]{};
            std::snprintf(label, sizeof(label), "%.3gx", rate);
            choices.push_back({label, rate == 1.0 ? "1.0x" : label, rate});
        }
        const double current = playback.playback_rate;
        if (std::find(rates.begin(), rates.end(), current) == rates.end())
            choices.push_back({"current", std::to_string(current) + "x", current});
        const auto rate = ui.Combo<double>({.id = "audio.now_playing.rate", .label = "",
            .description = "Playback speed",
            .width = rate_width, .height = 36.0F, .text_role = UiTextRole::Accent}, current, std::move(choices));
        if (rate.changed) actions.playback_rate = static_cast<float>(rate.value);
        ui.SameLine();
        if (button("audio.now_playing.mute", playback.muted ? "Unmute" : "Mute",
                   playback.muted ? Icon::VolumeOff : Icon::VolumeOn, mute_width, playback.muted))
            actions.muted = !playback.muted;
        ui.SameLine();
        const float volume_width = std::max(1.0F, width - (inline_extras ? used : rate_width + mute_width + spacing * 2.0F));
        actions.volume = audio_editor::DrawAudioVolumeProgressControl(
            ui, "audio.now_playing.volume", playback.volume, volume_width);
        return actions;
    }

    void DrawAudioSpectrum(editor::ui::UiContext &ui, const PlaybackView &playback)
    {
        ui.Section("// SPECTRUM");
        ui.Text("// SOURCE SIGNAL / PLAYED CURSOR", UiTextRole::Muted);
        const auto available = ui.AvailableContent();
        const float width = std::max(1.0F, available.x);
        const float height = std::clamp(available.y - 44.0F, 95.0F, 198.0F);
        const double sample_rate = playback.track
            ? static_cast<double>(playback.track->sample_rate) : 48'000.0;
        const double minimum_frequency = std::max(1.0, sample_rate / 256.0);
        const double maximum_frequency = minimum_frequency * 120.0;
        constexpr std::array<std::size_t, 4> tick_bins{0, 16, 32, 47};
        std::array<std::string, 4> frequency_labels;
        std::array<UiPlotTick, 4> frequency_ticks;
        for (std::size_t index = 0; index < tick_bins.size(); ++index)
        {
            const double fraction = static_cast<double>(tick_bins[index]) / 47.0;
            const double frequency = minimum_frequency * std::pow(120.0, fraction);
            char label[20]{};
            if (frequency >= 1'000.0)
                std::snprintf(label, sizeof(label), "%.1fk", frequency / 1'000.0);
            else
                std::snprintf(label, sizeof(label), "%.0f", frequency);
            frequency_labels[index] = label;
            frequency_ticks[index] = {frequency, frequency_labels[index]};
        }
        const std::array<UiPlotTick, 4> level_ticks{{
            {-60.0, "-60"}, {-40.0, "-40"}, {-20.0, "-20"}, {0.0, "0"}}};
        std::vector<UiPlotPoint> bars;
        std::size_t peak_index = 0;
        if (playback.track)
        {
            peak_index = static_cast<std::size_t>(std::distance(playback.spectrum.begin(),
                std::max_element(playback.spectrum.begin(), playback.spectrum.end())));
            bars.reserve(playback.spectrum.size());
            for (std::size_t index = 1; index < playback.spectrum.size(); ++index)
            {
                const double frequency = minimum_frequency * std::pow(
                    maximum_frequency / minimum_frequency,
                    static_cast<double>(index) / static_cast<double>(playback.spectrum.size() - 1));
                const double amplitude = std::max<double>(playback.spectrum[index], 0.001);
                bars.push_back({frequency, std::clamp(20.0 * std::log10(amplitude), -60.0, 0.0)});
            }
        }
        UiPlotStyle plot_style;
        plot_style.background = Theme::surface;
        plot_style.border = Theme::border;
        plot_style.grid = Theme::grid;
        plot_style.axis = Theme::cyan_dim;
        plot_style.labels = Theme::muted;
        plot_style.left_margin = 36.0F;
        plot_style.top_margin = 16.0F;
        plot_style.right_margin = 10.0F;
        plot_style.bottom_margin = 27.0F;
        UiPlotSeries spectrum_series{.label = "Spectrum", .points = bars,
            .color = Theme::cyan, .kind = UiPlotSeriesKind::Bars,
            .bar_width = 3.4F, .bar_baseline = -60.0, .emission_strength = 0.55F};
        const std::size_t peak_bar_index = peak_index == 0 ? 0
            : std::min(peak_index - 1, bars.empty() ? std::size_t{0} : bars.size() - 1);
        const double peak_frequency = !bars.empty() ? bars[peak_bar_index].x : 0.0;
        const double peak_level = !bars.empty() ? bars[peak_bar_index].y : -60.0;
        const std::array<UiPlotPoint, 1> peak_point{{{peak_frequency, peak_level}}};
        const std::array<UiPlotSeries, 2> series{{
            spectrum_series,
            {.label = "Peak", .points = peak_point, .color = Theme::amber,
             .kind = UiPlotSeriesKind::Bars, .bar_width = 4.0F, .bar_baseline = -60.0,
             .emission_strength = 1.0F}}};
        char summary[64]{};
        std::snprintf(summary, sizeof(summary), "RMS %.3f  |  PEAK %.3f", playback.rms, playback.peak);
        (void)editor::ui::Plot(ui, {
            .control = {.id = "audio.spectrum.plot", .label = "Source frequency spectrum",
                        .description = "256-frame source FFT at the playback cursor",
                        .enabled = playback.track.has_value(),
                        .domain_summary = playback.track ? summary : "No source signal"},
            .size = {width, height},
            .x_axis = {.minimum = minimum_frequency, .maximum = maximum_frequency,
                       .scale = UiPlotScale::Log10,
                       .ticks = frequency_ticks, .unit = "Hz", .semantic_label = "Frequency"},
            .y_axis = {.minimum = -60.0, .maximum = 0.0, .ticks = level_ticks,
                       .unit = "dB", .semantic_label = "Relative level"},
            .series = playback.track ? std::span<const UiPlotSeries>{series}
                                    : std::span<const UiPlotSeries>{},
            .style = plot_style,
            .empty_label = playback.track ? std::string_view{} : std::string_view{"NO SOURCE SIGNAL"}});
        ui.Text(summary, UiTextRole::Accent);
        ui.WrappedText("Relative dB / 256-frame source FFT. Not device output.");
    }
}
