#ifndef KPENGINE_MODULE_SIGNAL_TOOLS_SIGNAL_LAB_EDITOR_H
#define KPENGINE_MODULE_SIGNAL_TOOLS_SIGNAL_LAB_EDITOR_H

#include <memory>
#include <cstdint>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "runtime/signal/sample_block.h"
#include "runtime/signal/signal_statistics.h"
#include "runtime/signal/signal_generator.h"

namespace kpengine::editor
{
    class EditorLogComponent;
    class EditorUI;
}

namespace kpengine::runtime
{
    class Engine;
}

namespace kpengine::signal
{
    class SignalScopeSession;

    struct SignalLabSignal
    {
        SignalLabSignal(std::string signal_name,
                        SignalGeneratorConfig signal_config,
                        SampleBlock sample_block,
                        SignalStatistics measured,
                        std::uint32_t trace_color)
            : name(std::move(signal_name)),
              config(std::move(signal_config)),
              samples(std::move(sample_block)),
              statistics(measured),
              color(trace_color)
        {
        }

        std::string name;
        SignalGeneratorConfig config;
        SampleBlock samples;
        SignalStatistics statistics;
        std::uint32_t color = 0;
        bool visible = true;
    };

    class SignalLabEditor final
    {
    public:
        SignalLabEditor();
        ~SignalLabEditor();

        bool Initialize(runtime::Engine &engine,
                        std::shared_ptr<SignalScopeSession> scope_session,
                        std::string &diagnostic);
        bool Render(std::string &diagnostic);
        void Shutdown() noexcept;

    private:
        enum class DisplayMode : std::uint8_t
        {
            Scope,
            Overlay,
            Composite
        };

        void RenderWorkspace();
        bool GenerateSignal(std::string &diagnostic);
        void RebuildComposite();

        std::unique_ptr<editor::EditorUI> ui_;
        std::unique_ptr<editor::EditorLogComponent> log_component_;
        std::shared_ptr<SignalScopeSession> scope_session_;
        std::vector<SignalLabSignal> signals_;
        std::vector<double> composite_samples_;
        SignalStatistics composite_statistics_{};
        DisplayMode display_mode_ = DisplayMode::Scope;
        bool composite_valid_ = true;
        SignalKind generator_kind_ = SignalKind::Sine;
        bool generator_realistic_ = true;
        double generator_sample_rate_hz_ = 48'000.0;
        double generator_frequency_hz_ = 1'000.0;
        double generator_amplitude_ = 1.0;
        double generator_harmonic_distortion_percent_ = 18.0;
        double generator_noise_rms_ = 0.04;
        double generator_phase_degrees_ = 0.0;
        double generator_offset_ = 0.0;
        std::size_t selected_signal_ = 0;
        std::uint64_t signal_serial_ = 0;
        std::string generator_message_;
    };
}

#endif
