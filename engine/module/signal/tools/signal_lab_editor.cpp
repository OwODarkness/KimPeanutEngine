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
#include <string>

#include "runtime/core/log/editor/log_editor_view.h"
#include "editor/ui/host/editor_ui_host.h"
#include "editor/ui/api/editor_ui/ui_plot.h"
#include "editor/ui/api/editor_ui/ui_theme.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"
#include "runtime/window/window_system.h"

namespace kpengine::signal
{
    namespace
    {
        using editor::ui::UiColor;
        using editor::ui::UiContext;
        using editor::ui::UiPlotPoint;
        using editor::ui::UiPlotSeries;
        using editor::ui::UiPlotSeriesKind;
        using editor::ui::UiTextRole;

        constexpr UiColor kMixColor{0.94F, 0.70F, 0.44F, 1.0F};
        constexpr std::array<UiColor, 8> kSignalColors{{
            {0.635F, 0.749F, 0.800F, 1.0F}, {0.420F, 0.839F, 0.694F, 1.0F},
            {0.804F, 0.592F, 0.922F, 1.0F}, {0.937F, 0.698F, 0.435F, 1.0F},
            {0.922F, 0.486F, 0.588F, 1.0F}, {0.494F, 0.682F, 0.937F, 1.0F},
            {0.784F, 0.804F, 0.467F, 1.0F}, {0.471F, 0.804F, 0.812F, 1.0F}}};

        std::size_t SignalColorIndex(const UiColor color)
        {
            std::size_t nearest = 0;
            float nearest_distance = std::numeric_limits<float>::max();
            for (std::size_t index = 0; index < kSignalColors.size(); ++index)
            {
                const UiColor candidate = kSignalColors[index];
                const float dr = color.r - candidate.r;
                const float dg = color.g - candidate.g;
                const float db = color.b - candidate.b;
                const float distance = dr * dr + dg * dg + db * db;
                if (distance < nearest_distance)
                {
                    nearest = index;
                    nearest_distance = distance;
                }
            }
            return nearest;
        }

        double NiceAxisStep(const double approximate_step)
        {
            if (!(approximate_step > 0.0) || !std::isfinite(approximate_step)) return 1.0;
            const double magnitude = std::pow(10.0, std::floor(std::log10(approximate_step)));
            const double fraction = approximate_step / magnitude;
            const double nice_fraction = fraction <= 1.0 ? 1.0
                : fraction <= 2.0 ? 2.0 : fraction <= 2.5 ? 2.5
                : fraction <= 5.0 ? 5.0 : 10.0;
            return nice_fraction * magnitude;
        }

        editor::ui::UiPlotStyle SignalPlotStyle()
        {
            editor::ui::UiPlotStyle style{};
            style.background = {0.035F, 0.047F, 0.058F, 1.0F};
            style.border = {0.25F, 0.31F, 0.35F, 1.0F};
            style.grid = {0.26F, 0.32F, 0.36F, 0.53F};
            style.axis = {0.65F, 0.75F, 0.80F, 1.0F};
            style.labels = {0.61F, 0.69F, 0.73F, 1.0F};
            return style;
        }

        editor::ui::UiVec2 PlotExtent(UiContext &ui)
        {
            const auto available = ui.AvailableContent();
            return {std::max(available.x, 240.0F), std::max(available.y - 42.0F, 220.0F)};
        }

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

        void RenderSignalScopePlot(UiContext &ui, const SignalScopeSnapshot &snapshot,
                                   const UiColor trace_color, const bool trace_visible)
        {
            using namespace editor::ui;
            const std::span<const double> samples = snapshot.capture != nullptr
                ? std::span<const double>(*snapshot.capture) : std::span<const double>{};
            double limit = 1.0;
            if (!samples.empty())
            {
                const auto bounds = std::minmax_element(samples.begin(), samples.end());
                const double peak = std::max(std::abs(*bounds.first), std::abs(*bounds.second));
                limit = NiceAxisStep(std::max(peak * 1.1 / 4.0, 1.0e-9)) * 4.0;
            }
            const double rate = snapshot.source.sample_rate_hz;
            const double duration_ms = !samples.empty() && rate > 0.0
                ? static_cast<double>(samples.size()) * 1000.0 / rate : 10.0;
            std::vector<UiPlotPoint> trace;
            const std::size_t count = std::min<std::size_t>(samples.size(), 2'048);
            trace.reserve(count);
            for (std::size_t point = 0; point < count; ++point)
            {
                const std::size_t sample = count <= 1 ? 0
                    : point * (samples.size() - 1) / (count - 1);
                const double x = samples.size() <= 1 ? 0.0
                    : duration_ms * static_cast<double>(sample) /
                        static_cast<double>(samples.size() - 1);
                trace.push_back({x, samples[sample]});
            }
            std::array<UiPlotPoint, 2> trigger_points{};
            std::array<UiPlotSeries, 2> series{};
            std::size_t series_count = 0;
            if (trace_visible && !trace.empty())
                series[series_count++] = {.label = "Signal", .points = trace,
                    .color = trace_color, .kind = UiPlotSeriesKind::Line, .thickness = 1.8F};
            if (snapshot.trigger_sample && !samples.empty() && rate > 0.0)
            {
                const double trigger_ms = static_cast<double>(snapshot.pretrigger_samples) * 1000.0 / rate;
                trigger_points = {{{trigger_ms, -limit}, {trigger_ms, limit}}};
                series[series_count++] = {.label = "Trigger", .points = trigger_points,
                    .color = {0.94F, 0.65F, 0.32F, 0.75F},
                    .kind = UiPlotSeriesKind::Line, .thickness = 1.2F};
            }
            UiPlotStyle style = SignalPlotStyle();
            style.top_margin = 24.0F;
            style.bottom_margin = 42.0F;
            const UiPlotResult result = Plot(ui, {
                .control = {.id = "signal.lab.scope.plot", .label = "Signal scope",
                            .enabled = !trace.empty()},
                .size = PlotExtent(ui),
                .x_axis = {.minimum = 0.0, .maximum = std::max(duration_ms, 0.001),
                           .label = "TIME (ms)", .divisions = 10},
                .y_axis = {.minimum = -limit, .maximum = limit,
                           .label = "VOLTAGE (V)", .divisions = 8},
                .series = {series.data(), series_count}, .style = style,
                .empty_label = !trace_visible ? "Selected signal is hidden"
                    : samples.empty() ? snapshot.state_label : std::string_view{}});
            if (result.has_pointer_value && !samples.empty())
            {
                const double fraction = std::clamp(result.pointer_x / duration_ms, 0.0, 1.0);
                const std::size_t sample = static_cast<std::size_t>(fraction * (samples.size() - 1));
                ui.Text("Sample " + std::to_string(sample) + " | " +
                    std::to_string(samples[sample]) + " V", UiTextRole::Muted);
            }
        }

        void RenderSignalSpectrumPlot(UiContext &ui, const Spectrum &spectrum,
                                      const UiColor trace_color, const bool show_psd)
        {
            using namespace editor::ui;
            if (spectrum.bins.empty())
            {
                ui.Text("No spectrum bins available", UiTextRole::Muted);
                return;
            }
            const auto value_for = [show_psd](const SpectrumBin &bin)
            { return show_psd ? bin.power_spectral_density : bin.peak_amplitude; };
            double maximum = 0.0;
            for (const SpectrumBin &bin : spectrum.bins) maximum = std::max(maximum, value_for(bin));
            const double limit = NiceAxisStep(std::max(maximum * 1.1 / 4.0, 1.0e-12)) * 4.0;
            std::vector<UiPlotPoint> points;
            points.reserve(spectrum.bins.size());
            for (const SpectrumBin &bin : spectrum.bins) points.push_back({bin.frequency_hz, value_for(bin)});
            UiPlotStyle style = SignalPlotStyle();
            style.top_margin = 30.0F;
            style.bottom_margin = 48.0F;
            const std::array<UiPlotSeries, 1> series{{
                {.label = "Spectrum", .points = points, .color = trace_color,
                 .kind = UiPlotSeriesKind::Line, .thickness = 2.0F}}};
            const auto result = Plot(ui, {
                .control = {.id = "signal.lab.spectrum.plot", .label = "Signal spectrum"},
                .size = PlotExtent(ui),
                .x_axis = {.minimum = 0.0, .maximum = std::max(spectrum.sample_rate_hz * 0.5, 1.0),
                           .label = "FREQUENCY (Hz)", .divisions = 6},
                .y_axis = {.minimum = 0.0, .maximum = limit,
                           .label = show_psd ? "POWER DENSITY (V^2/Hz)" : "PEAK AMPLITUDE (V)",
                           .divisions = 4},
                .series = series, .style = style});
            if (result.has_pointer_value)
            {
                const auto nearest = std::min_element(spectrum.bins.begin(), spectrum.bins.end(),
                    [&](const SpectrumBin &left, const SpectrumBin &right)
                    { return std::abs(left.frequency_hz - result.pointer_x) <
                             std::abs(right.frequency_hz - result.pointer_x); });
                ui.Text(std::to_string(nearest->frequency_hz) + " Hz | " +
                    std::to_string(value_for(*nearest)), UiTextRole::Muted);
            }
        }

        void RenderSignalPlot(UiContext &ui, const std::span<const SignalLabSignal> signals,
                              const bool composite_mode,
                              const std::span<const double> composite_samples,
                              const bool composite_valid)
        {
            using namespace editor::ui;
            if (signals.empty())
            {
                ui.Text("Generate a waveform to begin.", UiTextRole::Muted);
                return;
            }
            if (composite_mode && !composite_valid)
            {
                ui.Text("Composite exceeds the numeric range.", UiTextRole::Warning);
                return;
            }
            std::vector<const SignalLabSignal *> visible;
            for (const auto &signal : signals) if (signal.visible) visible.push_back(&signal);
            if (!composite_mode && visible.empty())
            {
                ui.Text("All signals are hidden.", UiTextRole::Muted);
                return;
            }
            const auto first = signals.front().samples.View();
            const double rate = first.SampleRateHz();
            const std::size_t extent = composite_mode ? composite_samples.size()
                : visible.front()->samples.SampleCount();
            const double duration = rate > 0.0 ? static_cast<double>(extent) / rate : 1.0;
            double peak = 1.0e-9;
            if (composite_mode)
            {
                for (const double sample : composite_samples) peak = std::max(peak, std::abs(sample));
            }
            else
            {
                for (const auto *signal : visible)
                    for (const double sample : signal->samples.ChannelSamples(0))
                        peak = std::max(peak, std::abs(sample));
            }
            const double limit = NiceAxisStep(peak * 1.1 / 3.0) * 3.0;
            std::vector<std::vector<UiPlotPoint>> points;
            std::vector<UiPlotSeries> series;
            points.reserve(composite_mode ? 1 : visible.size());
            series.reserve(composite_mode ? 1 : visible.size());
            const auto add_trace = [&](const std::span<const double> samples,
                                       const std::string_view label, const UiColor color)
            {
                const std::size_t count = std::min<std::size_t>(samples.size(), 2'048);
                points.emplace_back();
                auto &trace = points.back();
                trace.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::size_t sample = count <= 1 ? 0 : i * (samples.size() - 1) / (count - 1);
                    const double x = samples.size() <= 1 ? 0.0
                        : duration * static_cast<double>(sample) / (samples.size() - 1);
                    trace.push_back({x, samples[sample]});
                }
                series.push_back({.label = label, .points = trace, .color = color,
                    .kind = UiPlotSeriesKind::Line, .thickness = 1.7F});
            };
            if (composite_mode)
                add_trace(composite_samples, "Composite", kMixColor);
            else
                for (const auto *signal : visible)
                    add_trace(signal->samples.ChannelSamples(0), signal->name, signal->color);
            auto style = SignalPlotStyle();
            style.left_margin = 58.0F;
            style.bottom_margin = 42.0F;
            (void)Plot(ui, {
                .control = {.id = "signal.lab.overlay.plot", .label = "Signal overlay"},
                .size = PlotExtent(ui),
                .x_axis = {.minimum = 0.0, .maximum = std::max(duration * 1000.0, 1.0e-6),
                           .label = "TIME (ms)", .divisions = 5},
                .y_axis = {.minimum = -limit, .maximum = limit,
                           .label = "AMPLITUDE (V)", .divisions = 6},
                .series = series, .style = style,
                .empty_label = "No samples available"});
        }

        void RenderPairMixPlot(UiContext &ui, const SignalLabSignal &signal_a,
                               const SignalLabSignal &signal_b, const SampleBlock &mixed_block)
        {
            using namespace editor::ui;
            const auto a = signal_a.samples.ChannelSamples(0);
            const auto b = signal_b.samples.ChannelSamples(0);
            const auto mixed = mixed_block.ChannelSamples(0);
            if (a.empty() || a.size() != b.size() || a.size() != mixed.size())
            {
                ui.Text("The selected pair has no aligned samples.", UiTextRole::Warning);
                return;
            }
            const std::array<std::span<const double>, 3> inputs{a, b, mixed};
            const std::array<std::string_view, 3> labels{signal_a.name, signal_b.name, "A + B"};
            const std::array<UiColor, 3> colors{signal_a.color, signal_b.color, kMixColor};
            double peak = 1.0e-9;
            for (const auto span : inputs)
                for (const double sample : span) peak = std::max(peak, std::abs(sample));
            const double limit = NiceAxisStep(peak * 1.1 / 3.0) * 3.0;
            const double duration = static_cast<double>(mixed.size()) / mixed_block.SampleRateHz();
            std::vector<std::vector<UiPlotPoint>> points(3);
            std::array<UiPlotSeries, 3> series{};
            for (std::size_t trace = 0; trace < inputs.size(); ++trace)
            {
                const std::size_t count = std::min<std::size_t>(inputs[trace].size(), 2'048);
                points[trace].reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::size_t sample = count <= 1 ? 0
                        : i * (inputs[trace].size() - 1) / (count - 1);
                    points[trace].push_back({duration * static_cast<double>(sample) /
                        std::max<std::size_t>(inputs[trace].size() - 1, 1), inputs[trace][sample]});
                }
                series[trace] = {.label = labels[trace], .points = points[trace],
                    .color = colors[trace], .kind = UiPlotSeriesKind::Line,
                    .thickness = trace == 2 ? 2.2F : 1.5F};
            }
            auto style = SignalPlotStyle();
            style.left_margin = 58.0F;
            (void)Plot(ui, {
                .control = {.id = "signal.lab.pair_mix.plot", .label = "Pair mix"},
                .size = PlotExtent(ui),
                .x_axis = {.minimum = 0.0, .maximum = std::max(duration * 1000.0, 1.0e-6),
                           .label = "TIME (ms)", .divisions = 5},
                .y_axis = {.minimum = -limit, .maximum = limit,
                           .label = "VOLTAGE (V)", .divisions = 6},
                .series = series, .style = style});
        }

        void RenderPhasePlot(UiContext &ui, const SignalLabSignal &signal_a,
                             const SignalLabSignal &signal_b)
        {
            using namespace editor::ui;
            const auto a = signal_a.samples.ChannelSamples(0);
            const auto b = signal_b.samples.ChannelSamples(0);
            const std::size_t size = std::min(a.size(), b.size());
            if (size == 0)
            {
                ui.Text("The selected pair has no aligned samples.", UiTextRole::Warning);
                return;
            }
            double limit = 1.0e-9;
            for (std::size_t i = 0; i < size; ++i)
                limit = std::max({limit, std::abs(a[i]), std::abs(b[i])});
            limit = NiceAxisStep(limit * 1.1 / 3.0) * 3.0;
            std::vector<UiPlotPoint> points;
            const std::size_t count = std::min<std::size_t>(size, 4'096);
            points.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t sample = count <= 1 ? 0 : i * (size - 1) / (count - 1);
                points.push_back({a[sample], b[sample]});
            }
            const std::array<UiPlotSeries, 1> series{{
                {.label = "A[n], B[n]", .points = points,
                 .color = signal_a.color, .kind = UiPlotSeriesKind::Line,
                 .thickness = 1.8F}}};
            auto style = SignalPlotStyle();
            style.left_margin = 56.0F;
            style.bottom_margin = 40.0F;
            (void)Plot(ui, {
                .control = {.id = "signal.lab.phase.plot", .label = "Phase view"},
                .size = PlotExtent(ui),
                .x_axis = {.minimum = -limit, .maximum = limit, .label = "A (V)", .divisions = 6},
                .y_axis = {.minimum = -limit, .maximum = limit, .label = "B (V)", .divisions = 6},
                .series = series, .style = style});
        }

        bool SelectPairSignal(UiContext &ui, const std::string_view id,
                              const std::string_view label, std::size_t &selected,
                              const std::span<const SignalLabSignal> signals)
        {
            if (signals.empty())
            {
                ui.Text(std::string(label) + ": No signals", UiTextRole::Muted);
                return false;
            }
            selected = std::min(selected, signals.size() - 1);
            std::vector<editor::ui::UiChoice> choices;
            choices.reserve(signals.size());
            for (std::size_t i = 0; i < signals.size(); ++i)
                choices.push_back({std::to_string(i), signals[i].name,
                    static_cast<std::uint64_t>(i)});
            const auto result = ui.Combo<std::uint64_t>({.id = id, .label = label},
                static_cast<std::uint64_t>(selected), std::move(choices));
            if (result.changed) selected = static_cast<std::size_t>(result.value);
            return result.changed;
        }

        void RenderPanelHeading(UiContext &ui, const std::string_view title,
                                const std::string_view detail)
        {
            ui.Section(title);
            ui.Text(detail, UiTextRole::Muted);
        }

        void RenderProperty(UiContext &ui, const std::string_view label,
                            const std::string_view value)
        {
            ui.Text(std::string(label) + "  " + std::string(value), UiTextRole::Muted);
        }

        void RenderMeasurement(UiContext &ui, const std::string_view label, const double value)
        {
            char formatted[64]{};
            std::snprintf(formatted, sizeof(formatted), "%.5g", value);
            ui.Text(std::string(label) + "  " + formatted, UiTextRole::Accent);
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

        try
        {
            log_view_ = std::make_unique<editor::LogEditorView>();
            ui_ = std::make_unique<editor::EditorUiHost>();
            ui_->Initialize({
                .window = context.window_system_->GetNativeHandle(),
                .presentation_bridge = context.render_system_->GetEditorPresentationBridge()});
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
        const UiColor color = kSignalColors[(serial - 1) % kSignalColors.size()];
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
        if (ui_ == nullptr || !ui_->RenderFrame([this](editor::ui::UiContext &ui)
            {
                RenderWorkspace(ui);
                RenderLog(ui);
            }))
        {
            diagnostic = "Signal Lab UI presentation failed";
            return false;
        }
        return true;
    }

    void SignalLabEditor::Shutdown() noexcept
    {
        log_view_.reset();
        scope_session_.reset();
        if (ui_ != nullptr)
        {
            ui_->Close();
            ui_.reset();
        }
    }

    void SignalLabEditor::RenderWorkspace(editor::ui::UiContext &ui)
    {
        using namespace editor::ui;
        const UiViewportInfo &viewport = ui.Viewport();
        const UiVec2 origin = viewport.logical_origin;
        const UiVec2 extent = viewport.logical_extent;
        UiTheme theme{};
        theme.surface = {0.025F, 0.035F, 0.045F, 1.0F};
        theme.panel = {0.045F, 0.060F, 0.073F, 1.0F};
        theme.raised = {0.075F, 0.095F, 0.110F, 1.0F};
        theme.border = {0.14F, 0.25F, 0.29F, 1.0F};
        theme.text = {0.82F, 0.89F, 0.92F, 1.0F};
        theme.muted = {0.48F, 0.61F, 0.66F, 1.0F};
        theme.accent = {0.08F, 0.78F, 0.88F, 1.0F};
        theme.accent_active = {1.0F, 0.67F, 0.18F, 1.0F};
        theme.selected = {0.22F, 0.16F, 0.07F, 1.0F};
        theme.selected_hovered = {0.34F, 0.24F, 0.09F, 1.0F};
        theme.window_padding_x = 10.0F;
        theme.window_padding_y = 8.0F;
        theme.frame_padding_x = 7.0F;
        theme.frame_padding_y = 4.0F;

        const auto scope = scope_session_ != nullptr ? scope_session_->Snapshot() : nullptr;
        const UiVec2 workspace_size{extent.x, extent.y * 0.72F};
        const auto window = ui.Window({.id = "signal.lab.window", .label = "Signal Lab"},
            origin, workspace_size);
        if (!window.ContentVisible()) return;
        [[maybe_unused]] const auto theme_scope = ui.Theme(theme);
        ui.Text("SIGNAL LAB  /  SIMULATED OSCILLOSCOPE", UiTextRole::Accent);
        ui.Text("Generate, sample, trigger, and inspect deterministic signals.", UiTextRole::Muted);

        if (scope != nullptr)
        {
            const bool running = scope->transport == ScopeTransport::Running;
            if (ui.Button({.id = "signal.lab.transport", .label = running ? "Pause" : "Run"}).activated)
            {
                if (running) scope_session_->Pause(); else scope_session_->Run();
            }
            ui.SameLine();
            if (ui.Button({.id = "signal.lab.arm", .label = "Arm"}).activated)
            {
                std::string message;
                (void)scope_session_->Arm(message);
                if (!message.empty()) generator_message_ = std::move(message);
            }
            ui.SameLine();
            if (ui.Button({.id = "signal.lab.step", .label = "Step 256",
                           .enabled = !running}).activated)
            {
                std::string message;
                (void)scope_session_->Step(message);
                if (!message.empty()) generator_message_ = std::move(message);
            }
            ui.SameLine();
            if (ui.Button({.id = "signal.lab.reset", .label = "Reset"}).activated)
            {
                std::string message;
                (void)scope_session_->Reset(message);
                if (!message.empty()) generator_message_ = std::move(message);
            }
            ui.SameLine();
            ui.Text(scope->state_label, UiTextRole::Accent);

            std::vector<UiChoice> trigger_modes{
                {"auto", "Auto", static_cast<std::int64_t>(ScopeTriggerMode::Auto)},
                {"normal", "Normal", static_cast<std::int64_t>(ScopeTriggerMode::Normal)},
                {"single", "Single", static_cast<std::int64_t>(ScopeTriggerMode::Single)}};
            auto mode = ui.Combo<std::int64_t>({.id = "signal.lab.trigger.mode", .label = "Trigger"},
                static_cast<std::int64_t>(scope->trigger_mode), std::move(trigger_modes));
            auto edge = ui.Combo<std::int64_t>({.id = "signal.lab.trigger.edge", .label = "Edge"},
                static_cast<std::int64_t>(scope->trigger_edge), {
                    {"rising", "Rising", static_cast<std::int64_t>(ScopeTriggerEdge::Rising)},
                    {"falling", "Falling", static_cast<std::int64_t>(ScopeTriggerEdge::Falling)}});
            auto level = ui.NumericInput<double>({.id = "signal.lab.trigger.level", .label = "Level (V)"},
                scope->trigger_level, -1.0e6, 1.0e6, 0.01);
            auto hysteresis = ui.NumericInput<double>({.id = "signal.lab.trigger.hysteresis", .label = "Hysteresis"},
                scope->trigger_hysteresis, 0.0, 1.0e6, 0.01);
            auto timebase = ui.NumericInput<double>({.id = "signal.lab.timebase", .label = "Timebase (ms)"},
                scope->timebase_ms, 0.01, 10'000.0, 0.1);
            auto holdoff = ui.NumericInput<std::uint64_t>(
                {.id = "signal.lab.holdoff", .label = "Holdoff (samples)"},
                scope->holdoff_samples, 0ULL, 65'536ULL, 1ULL);
            if (mode.changed || edge.changed || level.changed || hysteresis.changed)
            {
                std::string message;
                (void)scope_session_->SetTrigger(level.value, hysteresis.value,
                    static_cast<ScopeTriggerEdge>(edge.value),
                    static_cast<ScopeTriggerMode>(mode.value), message);
                if (!message.empty()) generator_message_ = std::move(message);
            }
            if (timebase.changed)
            {
                std::string message;
                (void)scope_session_->SetTimebaseMs(timebase.value, message);
                if (!message.empty()) generator_message_ = std::move(message);
            }
            if (holdoff.changed)
            {
                std::string message;
                (void)scope_session_->SetHoldoffSamples(holdoff.value, message);
                if (!message.empty()) generator_message_ = std::move(message);
            }
        }

        ui.Section("WORKSPACE");
        const float table_height = std::max(extent.y * 0.72F - 160.0F, 240.0F);
        const std::array<UiTableColumn, 3> columns{{
            {"Generator / Signals", 250.0F}, {"Waveform", 0.0F}, {"Details", 250.0F}}};
        ui.Table("signal.lab.workspace", columns, table_height, true, [&]
        {
            ui.TableColumn();
            ui.Section("SIGNAL GENERATOR");
            std::vector<UiChoice> waveform_choices;
            constexpr std::array<SignalKind, 8> kinds{
                SignalKind::Sine, SignalKind::Cosine, SignalKind::Square, SignalKind::Sawtooth,
                SignalKind::Triangle, SignalKind::Constant, SignalKind::Impulse,
                SignalKind::UniformNoise};
            for (std::size_t i = 0; i < kinds.size(); ++i)
                waveform_choices.push_back({std::to_string(i), SignalKindName(kinds[i]),
                    static_cast<std::int64_t>(kinds[i])});
            const auto waveform = ui.Combo<std::int64_t>(
                {.id = "signal.lab.generator.waveform", .label = "Waveform"},
                static_cast<std::int64_t>(generator_kind_), std::move(waveform_choices));
            if (waveform.changed) generator_kind_ = static_cast<SignalKind>(waveform.value);
            auto realistic = ui.Checkbox({.id = "signal.lab.generator.realistic", .label = "Composite model"},
                generator_realistic_);
            if (realistic.changed) generator_realistic_ = realistic.value;
            auto frequency = ui.NumericInput<double>(
                {.id = "signal.lab.generator.frequency", .label = "Frequency (Hz)"},
                generator_frequency_hz_, 0.1,
                std::max(0.1, generator_sample_rate_hz_ * 0.5), 1.0);
            if (frequency.changed) generator_frequency_hz_ = frequency.value;
            auto amplitude = ui.NumericInput<double>(
                {.id = "signal.lab.generator.amplitude", .label = "Amplitude (V)"},
                generator_amplitude_, 0.0, 1.0e6, 0.01);
            if (amplitude.changed) generator_amplitude_ = amplitude.value;
            auto advanced = ui.Checkbox({.id = "signal.lab.generator.advanced", .label = "Advanced"},
                generator_advanced_open_);
            if (advanced.changed) generator_advanced_open_ = advanced.value;
            if (generator_advanced_open_)
            {
                auto phase = ui.NumericInput<double>({.id = "signal.lab.generator.phase", .label = "Phase (deg)"},
                    generator_phase_degrees_, -360.0, 360.0, 1.0);
                auto offset = ui.NumericInput<double>({.id = "signal.lab.generator.offset", .label = "Offset (V)"},
                    generator_offset_, -1.0e6, 1.0e6, 0.01);
                auto rate = ui.NumericInput<double>({.id = "signal.lab.generator.rate", .label = "Sample rate (Hz)"},
                    generator_sample_rate_hz_, 1.0, 384'000.0, 100.0);
                if (phase.changed) generator_phase_degrees_ = phase.value;
                if (offset.changed) generator_offset_ = offset.value;
                if (rate.changed)
                {
                    generator_sample_rate_hz_ = rate.value;
                    generator_frequency_hz_ = std::min(generator_frequency_hz_, rate.value * 0.5);
                }
                if (generator_realistic_ && !IsUnbandlimitedKind(generator_kind_))
                {
                    auto harmonics = ui.NumericInput<double>(
                        {.id = "signal.lab.generator.harmonics", .label = "Harmonics (%)"},
                        generator_harmonic_distortion_percent_, 0.0, 100.0, 1.0);
                    if (harmonics.changed) generator_harmonic_distortion_percent_ = harmonics.value;
                }
                if (generator_realistic_)
                {
                    auto noise = ui.NumericInput<double>(
                        {.id = "signal.lab.generator.noise", .label = "Noise RMS (V)"},
                        generator_noise_rms_, 0.0, 1.0e6, 0.01);
                    if (noise.changed) generator_noise_rms_ = noise.value;
                }
            }
            const bool can_generate = signals_.size() < kSignalColors.size();
            UiControlDescriptor generate_control{
                .id = "signal.lab.generator.generate", .label = "Generate signal",
                .enabled = can_generate};
            if (!can_generate) generate_control.disabled_reason = "Signal limit reached";
            if (ui.Button(generate_control).activated)
            {
                if (GenerateSignal(generator_message_)) generator_message_.clear();
            }
            if (!generator_message_.empty()) ui.Text(generator_message_, UiTextRole::Warning);
            ui.Section("SIGNALS");
            for (std::size_t index = 0; index < signals_.size(); ++index)
            {
                SignalLabSignal &signal = signals_[index];
                const std::string signal_id = "signal.lab.signal." + std::to_string(index);
                const auto selected = ui.Selectable({.id = signal_id, .label = signal.name},
                    selected_signal_ == index);
                if (selected.changed)
                {
                    selected_signal_ = index;
                    generator_kind_ = signal.config.kind;
                    generator_realistic_ = signal.config.harmonic_distortion > 0.0 || signal.config.noise_rms > 0.0;
                    generator_sample_rate_hz_ = signal.config.sample_rate_hz;
                    generator_frequency_hz_ = signal.config.frequency_hz;
                    generator_amplitude_ = signal.config.amplitude;
                    generator_harmonic_distortion_percent_ = signal.config.harmonic_distortion * 100.0;
                    generator_noise_rms_ = signal.config.noise_rms;
                    generator_phase_degrees_ = signal.config.phase_radians * 180.0 / std::numbers::pi_v<double>;
                    generator_offset_ = signal.config.offset;
                    scope_session_->SetSource(signal.config, generator_message_);
                    scope_session_->Run();
                }
                const std::string visible_id = "signal.lab.visible." + std::to_string(index);
                const auto visible = ui.Checkbox({.id = visible_id, .label = "Visible"}, signal.visible);
                if (visible.changed)
                {
                    signal.visible = visible.value;
                    RebuildComposite();
                }
                std::vector<UiChoice> colors;
                for (std::size_t color = 0; color < kSignalColors.size(); ++color)
                    colors.push_back({std::to_string(color), "Color " + std::to_string(color + 1),
                        static_cast<std::uint64_t>(color)});
                const std::size_t color_index = SignalColorIndex(signal.color);
                const std::string color_id = "signal.lab.color." + std::to_string(index);
                const auto color_result = ui.Combo<std::uint64_t>(
                    {.id = color_id, .label = "Trace color"},
                    static_cast<std::uint64_t>(color_index), std::move(colors));
                if (color_result.changed) signal.color = kSignalColors[color_result.value % kSignalColors.size()];
            }
            if (signals_.empty()) ui.Text("Generate a waveform to begin.", UiTextRole::Muted);

            ui.TableColumn();
            std::vector<UiChoice> views{
                {"scope", "Scope", static_cast<std::int64_t>(DisplayMode::Scope)},
                {"overlay", "Overlay", static_cast<std::int64_t>(DisplayMode::Overlay)},
                {"composite", "Composite", static_cast<std::int64_t>(DisplayMode::Composite)},
                {"spectrum", "Spectrum", static_cast<std::int64_t>(DisplayMode::Spectrum)},
                {"mix", "Mix", static_cast<std::int64_t>(DisplayMode::Mix)},
                {"phase", "Phase", static_cast<std::int64_t>(DisplayMode::Phase)}};
            const auto view = ui.Combo<std::int64_t>({.id = "signal.lab.view", .label = "View"},
                static_cast<std::int64_t>(display_mode_), std::move(views));
            if (view.changed) display_mode_ = static_cast<DisplayMode>(view.value);
            const bool pair_view = display_mode_ == DisplayMode::Mix || display_mode_ == DisplayMode::Phase;
            if (pair_view)
            {
                bool pair_changed = SelectPairSignal(ui, "signal.lab.pair.a", "Signal A", pair_signal_a_, signals_);
                pair_changed = SelectPairSignal(ui, "signal.lab.pair.b", "Signal B", pair_signal_b_, signals_) || pair_changed;
                if (pair_changed) RebuildPairMix();
            }
            if (display_mode_ == DisplayMode::Spectrum)
            {
                auto window_kind = ui.Combo<std::int64_t>({.id = "signal.lab.spectrum.window", .label = "Window"},
                    static_cast<std::int64_t>(spectrum_window_), {
                        {"rect", "Rectangular", static_cast<std::int64_t>(WindowKind::Rectangular)},
                        {"hann", "Periodic Hann", static_cast<std::int64_t>(WindowKind::PeriodicHann)}});
                if (window_kind.changed) spectrum_window_ = static_cast<WindowKind>(window_kind.value);
                constexpr std::array<std::size_t, 4> fft_sizes{512, 1'024, 2'048, 4'096};
                std::vector<UiChoice> fft_choices;
                for (const std::size_t size : fft_sizes)
                    fft_choices.push_back({std::to_string(size), std::to_string(size),
                        static_cast<std::uint64_t>(size)});
                auto fft = ui.Combo<std::uint64_t>({.id = "signal.lab.spectrum.fft", .label = "FFT size"},
                    static_cast<std::uint64_t>(spectrum_fft_size_), std::move(fft_choices));
                if (fft.changed) spectrum_fft_size_ = static_cast<std::size_t>(fft.value);
                auto psd = ui.Checkbox({.id = "signal.lab.spectrum.psd", .label = "Power density"},
                    spectrum_show_psd_);
                if (psd.changed) spectrum_show_psd_ = psd.value;
            }
            ui.Section(display_mode_ == DisplayMode::Scope ? "SCOPE"
                : display_mode_ == DisplayMode::Spectrum ? "SPECTRUM"
                : pair_view ? "PAIR ANALYSIS" : "SIGNAL VIEW");
            if (display_mode_ == DisplayMode::Scope && scope != nullptr)
            {
                const bool visible = !signals_.empty() && signals_[std::min(selected_signal_, signals_.size() - 1)].visible;
                const UiColor color = signals_.empty() ? kSignalColors.front() : signals_[selected_signal_].color;
                RenderSignalScopePlot(ui, *scope, color, visible);
                if (scope->capture != nullptr)
                    RenderMeasurement(ui, "Capture RMS (V)", scope->measurements.rms);
            }
            else if (display_mode_ == DisplayMode::Overlay)
                RenderSignalPlot(ui, signals_, false, composite_samples_, composite_valid_);
            else if (display_mode_ == DisplayMode::Composite)
                RenderSignalPlot(ui, signals_, true, composite_samples_, composite_valid_);
            else if (display_mode_ == DisplayMode::Spectrum && !signals_.empty())
            {
                selected_signal_ = std::min(selected_signal_, signals_.size() - 1);
                const auto &selected = signals_[selected_signal_];
                std::string message;
                const auto spectrum = ComputeSpectrum(selected.samples.ChannelSamples(0),
                    selected.samples.SampleRateHz(), spectrum_window_, spectrum_fft_size_, message);
                if (spectrum) RenderSignalSpectrumPlot(ui, *spectrum, selected.color, spectrum_show_psd_);
                else ui.Text(message, UiTextRole::Warning);
            }
            else if (pair_view && pair_signal_a_ < signals_.size() && pair_signal_b_ < signals_.size())
            {
                if (display_mode_ == DisplayMode::Mix && pair_mix_)
                    RenderPairMixPlot(ui, signals_[pair_signal_a_], signals_[pair_signal_b_], *pair_mix_);
                else if (display_mode_ == DisplayMode::Phase)
                    RenderPhasePlot(ui, signals_[pair_signal_a_], signals_[pair_signal_b_]);
                else if (!pair_mix_diagnostic_.empty()) ui.Text(pair_mix_diagnostic_, UiTextRole::Muted);
            }

            ui.TableColumn();
            ui.Section("SELECTED SIGNAL");
            if (!signals_.empty())
            {
                selected_signal_ = std::min(selected_signal_, signals_.size() - 1);
                const SignalLabSignal &selected = signals_[selected_signal_];
                ui.Text(selected.name, UiTextRole::Accent);
                RenderProperty(ui, "Waveform", SignalKindName(selected.config.kind));
                RenderProperty(ui, "Frequency", std::to_string(selected.config.frequency_hz) + " Hz");
                RenderProperty(ui, "Amplitude", std::to_string(selected.config.amplitude) + " V");
                RenderProperty(ui, "Sample rate", std::to_string(selected.config.sample_rate_hz) + " Hz");
                RenderProperty(ui, "Samples", std::to_string(selected.samples.SampleCount()));
                RenderMeasurement(ui, "RMS (V)", selected.statistics.rms);
                RenderMeasurement(ui, "Peak to peak (V)", selected.statistics.peak_to_peak);
            }
            if (scope != nullptr)
            {
                ui.Section("ACQUISITION");
                RenderProperty(ui, "Capture", scope->stale ? "STALE" : "CURRENT");
                RenderProperty(ui, "State", scope->state_label);
                RenderProperty(ui, "Trigger sample", scope->trigger_sample
                    ? std::to_string(*scope->trigger_sample) : "waiting");
                if (scope->frequency_available)
                    RenderMeasurement(ui, "Measured frequency (Hz)", scope->measured_frequency_hz);
                RenderMeasurement(ui, "Window RMS (V)", scope->measurements.rms);
                RenderMeasurement(ui, "Window peak-to-peak (V)", scope->measurements.peak_to_peak);
            }
        });
    }
    void SignalLabEditor::RenderLog(editor::ui::UiContext &ui)
    {
        if (!log_view_)
            return;
        const editor::ui::UiViewportInfo &viewport = ui.Viewport();
        const editor::ui::UiVec2 position{
            viewport.logical_origin.x,
            viewport.logical_origin.y + viewport.logical_extent.y * 0.72F};
        const editor::ui::UiVec2 size{viewport.logical_extent.x,
                                      viewport.logical_extent.y * 0.28F};
        const auto window = ui.Window(
            {.id = "signal.log.window", .label = "Output Log"}, position, size);
        if (window.ContentVisible())
        {
            log_view_->RenderContent(ui, runtime::global_runtime_context.log_system_.get());
        }
    }
}
