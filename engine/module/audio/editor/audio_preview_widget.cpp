#include "audio_preview_widget.h"
#include "audio_player_theme.h"
#include "audio_transport_controls.h"
#include "editor/ui/api/editor_ui/ui_plot.h"

#include <algorithm>
#include <array>
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

            void Line(const UiVec2 a, const UiVec2 b, const UiColor color, const float width = 1.0F)
            {
                marks.push_back({UiCanvasPrimitiveKind::Line, Point(a), Point(b), color, width});
            }

            void Box(const UiVec2 a, const UiVec2 b, const UiColor color, const bool fill = true)
            {
                marks.push_back({fill ? UiCanvasPrimitiveKind::FilledRect : UiCanvasPrimitiveKind::Rect,
                                 Point(a), Point(b), color});
            }

            void Dot(const UiVec2 center, const float radius, const UiColor color)
            {
                Box({center.x - radius, center.y - radius},
                    {center.x + radius, center.y + radius}, color);
            }

            void Label(const UiVec2 point, const UiColor color, std::string label,
                       const UiVec2 anchor = {}, const float scale = 1.0F)
            {
                marks.push_back({.kind = UiCanvasPrimitiveKind::Text, .first = Point(point),
                    .color = color, .text = std::move(label), .text_anchor = anchor,
                    .text_scale = scale});
            }

            UiCanvasResult Draw(UiContext &ui, const UiControlDescriptor &control)
            {
                return ui.Canvas(control, size, std::move(marks));
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
            const float progress = playback.duration_seconds > 0.0F
                ? std::clamp(playback.position_seconds / playback.duration_seconds, 0.0F, 1.0F) : 0.0F;
            constexpr int ring_dots = 64;
            for (int index = 0; index < ring_dots; ++index)
            {
                const float fraction = static_cast<float>(index) / ring_dots;
                const float angle = turn * (fraction - 0.25F);
                const bool played = fraction < progress;
                plot.Dot(at(side * 0.47F, angle), played ? 1.65F : 1.0F,
                         played ? Theme::amber : Theme::cyan_dim);
                if (index % 2 == 0)
                {
                    plot.Dot(at(side * 0.31F, angle), 0.9F, Theme::cyan_dim);
                    plot.Dot(at(side * 0.18F, angle), 1.15F, Theme::cyan);
                }
            }
            if (playback.track)
            {
                const float peak = *std::max_element(playback.spectrum.begin(), playback.spectrum.end());
                for (std::size_t index = 0; index < playback.spectrum.size(); ++index)
                {
                    const float angle = turn * (static_cast<float>(index) / playback.spectrum.size() - 0.25F);
                    const float value = std::clamp(playback.spectrum[index], 0.0F, 1.0F);
                    const int segments = 1 + static_cast<int>(value * 7.0F);
                    const UiColor color = peak > 0.02F && value >= peak * 0.8F ? Theme::amber : Theme::cyan;
                    for (int segment = 0; segment < segments; ++segment)
                    {
                        const float radius = side * (0.23F + 0.014F * segment);
                        plot.Line(at(radius, angle), at(radius + side * 0.008F, angle), color, 1.4F);
                    }
                }
            }
            plot.Label(center, Theme::cyan, playback.track
                ? "RMS " + std::to_string(static_cast<int>(std::clamp(playback.rms, 0.0F, 1.0F) * 100.0F))
                : "NO SIGNAL", {0.5F, 0.5F});
        }

        struct WaveformView
        {
            UiVec2 min;
            UiVec2 max;
            float start = 0.0F;
            float duration = 0.0F;
        };

        void DrawWaveform(Plot &plot, const WaveformView &view, const PlaybackView &playback)
        {
            const auto [left, top] = view.min;
            const auto [right, bottom] = view.max;
            const float axis = bottom - 21.0F;
            const float middle = (top + axis) * 0.5F;
            const float width = right - left;
            plot.Box(view.min, view.max, Theme::border, false);
            plot.Line({left, middle}, {right, middle}, Theme::grid);
            if (!playback.track || !playback.track->waveform ||
                playback.track->waveform->empty() || view.duration <= 0.0F)
            {
                plot.Label({left + 9.0F, middle}, Theme::muted, "LOAD CLIP FOR WAVEFORM", {0.0F, 0.5F});
                return;
            }

            const auto &samples = *playback.track->waveform;
            const auto pixel = [&](const float time) { return left + (time - view.start) / view.duration * width; };
            const auto tick_step = std::max(1, static_cast<int>(std::ceil(72.0F * view.duration / width)));
            const auto first_tick = static_cast<int>(std::ceil(view.start / tick_step)) * tick_step;
            for (int tick = first_tick; tick <= view.start + view.duration; tick += tick_step)
            {
                const float x = pixel(static_cast<float>(tick));
                plot.Line({x, top + 5.0F}, {x, axis}, Theme::grid);
                if (x > left + 17.0F && x < right - 17.0F)
                    plot.Label({x, axis + 3.0F}, Theme::muted, TimeLabel(static_cast<float>(tick)), {0.5F, 0.0F});
            }
            plot.Line({left, axis}, {right, axis}, Theme::cyan_dim);
            const float step = 12.0F * view.duration / width;
            const float first_bar = (std::floor(view.start / step) + 0.5F) * step;
            for (float time = first_bar; time < view.start + view.duration; time += step)
            {
                const float x = pixel(time);
                if (x < left + 3.0F || x > right - 3.0F) continue;
                const float sample = std::clamp(
                    time / playback.duration_seconds * static_cast<float>(samples.size()) - 0.5F,
                    0.0F, static_cast<float>(samples.size() - 1));
                const auto i = static_cast<std::size_t>(sample);
                const float amplitude = std::clamp(std::lerp(samples[i], samples[std::min(i + 1, samples.size() - 1)],
                                                             sample - static_cast<float>(i)), 0.0F, 1.0F);
                const float half = std::max(1.0F, amplitude * (axis - top) * 0.45F);
                const UiVec2 a{x, middle - half};
                const UiVec2 b{x, middle + half};
                const float recent = std::clamp(1.0F - (playback.position_seconds - time) / 1.5F, 0.0F, 1.0F);
                const float mix = time <= playback.position_seconds ? recent * recent : 0.0F;
                const UiColor color{std::lerp(Theme::cyan.r, Theme::amber.r, mix),
                    std::lerp(Theme::cyan.g, Theme::amber.g, mix),
                    std::lerp(Theme::cyan.b, Theme::amber.b, mix), 1.0F};
                plot.Line(a, b, Opacity(color, 0.10F), 9.0F);
                plot.Line(a, b, Opacity(color, 0.28F), 5.0F);
                plot.Line(a, b, color, 2.0F);
            }

            const float x = std::clamp(pixel(playback.position_seconds), left + 2.0F, right - 2.0F);
            const UiVec2 a{x, top + 7.0F};
            const UiVec2 b{x, axis - 5.0F};
            plot.Line(a, b, Opacity(Theme::amber, 0.18F), 9.0F);
            plot.Line(a, b, Opacity(Theme::amber, 0.45F), 5.0F);
            plot.Line(a, b, Theme::amber, 1.5F);
            plot.Dot(a, 2.0F, Theme::amber);
            plot.Dot(b, 2.0F, Theme::amber);
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
        if (wave.max.x - wave.min.x > 16.0F)
        {
            DrawWaveform(plot, wave, playback);
            if (height - wave_bottom > 32.0F)
            {
                plot.Label({wave_left + 9.0F, wave_bottom + 17.0F}, Theme::cyan, "// SUBTITLE");
                if (!playback.subtitle_text.empty())
                    plot.Label({std::midpoint(wave.min.x, wave.max.x), (wave_bottom + height) * 0.5F + 12.0F},
                               Theme::text, playback.subtitle_text, {0.5F, 0.5F}, 1.45F);
            }
        }
        const auto hit = plot.Draw(ui, {.id = "audio.now_playing.waveform", .label = "Audio preview",
            .description = "Click the waveform to seek within the visible time window",
            .enabled = playback.can_seek});
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
                {.id = "audio.now_playing.seek", .label = "Position", .enabled = playback.can_seek},
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
            .bar_baseline = -60.0};
        const std::size_t peak_bar_index = peak_index == 0 ? 0
            : std::min(peak_index - 1, bars.empty() ? std::size_t{0} : bars.size() - 1);
        const double peak_frequency = !bars.empty() ? bars[peak_bar_index].x : 0.0;
        const double peak_level = !bars.empty() ? bars[peak_bar_index].y : -60.0;
        const std::array<UiPlotPoint, 1> peak_point{{{peak_frequency, peak_level}}};
        const std::array<UiPlotSeries, 2> series{{
            spectrum_series,
            {.label = "Peak", .points = peak_point, .color = Theme::amber,
             .kind = UiPlotSeriesKind::Bars, .bar_width = 4.0F, .bar_baseline = -60.0}}};
        (void)editor::ui::Plot(ui, {
            .control = {.id = "audio.spectrum.plot", .label = "Spectrum",
                        .enabled = playback.track.has_value()},
            .size = {width, height},
            .x_axis = {.minimum = minimum_frequency, .maximum = maximum_frequency,
                       .scale = UiPlotScale::Log10,
                       .ticks = frequency_ticks},
            .y_axis = {.minimum = -60.0, .maximum = 0.0, .ticks = level_ticks},
            .series = playback.track ? std::span<const UiPlotSeries>{series}
                                    : std::span<const UiPlotSeries>{},
            .style = plot_style,
            .empty_label = playback.track ? std::string_view{} : std::string_view{"NO SOURCE SIGNAL"}});
        char summary[64]{};
        std::snprintf(summary, sizeof(summary), "RMS %.3f  |  PEAK %.3f", playback.rms, playback.peak);
        ui.Text(summary, UiTextRole::Accent);
        ui.WrappedText("Relative dB / 256-frame source FFT. Not device output.");
    }
}
