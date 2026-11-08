#ifndef KPENGINE_RUNTIME_SIGNAL_SIGNAL_GENERATOR_H
#define KPENGINE_RUNTIME_SIGNAL_SIGNAL_GENERATOR_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "signal/sample_block.h"

namespace kpengine::signal
{
    enum class SignalKind : std::uint8_t
    {
        Constant,
        Impulse,
        Sine,
        Cosine,
        Square,
        UniformNoise
    };

    struct SignalGeneratorConfig
    {
        SignalKind kind = SignalKind::Sine;
        double sample_rate_hz = 48'000.0;
        double frequency_hz = 1'000.0;
        double amplitude = 1.0;
        double offset = 0.0;
        double constant_value = 0.0;
        double phase_radians = 0.0;
        // RMS ratio of the added 2nd/3rd harmonics to a periodic fundamental.
        double harmonic_distortion = 0.0;
        // Deterministic uniform noise RMS, expressed in the signal's unit.
        double noise_rms = 0.0;
        std::uint64_t impulse_sample_index = 0;
        std::uint64_t seed = 0;
        SampleUnit unit = SampleUnit::Dimensionless;
    };

    class SignalGenerator final
    {
    public:
        static std::optional<SignalGenerator> Create(SignalGeneratorConfig config,
                                                     std::string &diagnostic);

        std::optional<SampleBlock> Generate(std::size_t sample_count,
                                            std::string &diagnostic);
        bool Reset(std::string &diagnostic);

        std::uint64_t Epoch() const noexcept { return epoch_; }
        std::uint64_t NextSampleIndex() const noexcept { return next_sample_index_; }
        const SignalGeneratorConfig &Config() const noexcept { return config_; }

    private:
        explicit SignalGenerator(SignalGeneratorConfig config) noexcept;

        SignalGeneratorConfig config_;
        std::uint64_t epoch_ = 0;
        std::uint64_t next_sample_index_ = 0;
        std::uint64_t random_state_ = 0;
        double phase_radians_ = 0.0;
    };
}

#endif
