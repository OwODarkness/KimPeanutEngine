#include "signal/signal_generator.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <utility>

namespace kpengine::signal
{
    namespace
    {
        constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
        constexpr std::uint64_t kSplitMix64Increment = 0x9E3779B97F4A7C15ULL;
        constexpr std::uint64_t kSplitMix64Multiplier1 = 0xBF58476D1CE4E5B9ULL;
        constexpr std::uint64_t kSplitMix64Multiplier2 = 0x94D049BB133111EBULL;
        constexpr double kUnit53Scale = 0x1.0p-53;

        double WrapPhase(const double phase) noexcept
        {
            double wrapped = std::fmod(phase, kTwoPi);
            if (wrapped < 0.0)
            {
                wrapped += kTwoPi;
            }
            return wrapped;
        }

        // Fixed SplitMix64 constants avoid implementation-defined distribution behavior.
        std::uint64_t NextSplitMix64(std::uint64_t &state) noexcept
        {
            state += kSplitMix64Increment;
            std::uint64_t value = state;
            value = (value ^ (value >> 30)) * kSplitMix64Multiplier1;
            value = (value ^ (value >> 27)) * kSplitMix64Multiplier2;
            return value ^ (value >> 31);
        }

        bool IsSupportedKind(const SignalKind kind) noexcept
        {
            switch (kind)
            {
            case SignalKind::Constant:
            case SignalKind::Impulse:
            case SignalKind::Sine:
            case SignalKind::Cosine:
            case SignalKind::Square:
            case SignalKind::UniformNoise:
                return true;
            }
            return false;
        }
    }

    SignalGenerator::SignalGenerator(SignalGeneratorConfig config) noexcept
        : config_(std::move(config)),
          random_state_(config_.seed),
          phase_radians_(WrapPhase(config_.phase_radians))
    {
    }

    std::optional<SignalGenerator> SignalGenerator::Create(SignalGeneratorConfig config,
                                                            std::string &diagnostic)
    {
        diagnostic.clear();
        if (!IsSupportedKind(config.kind))
        {
            diagnostic = "signal generator kind is not supported";
            return std::nullopt;
        }
        if (!std::isfinite(config.sample_rate_hz) || config.sample_rate_hz <= 0.0)
        {
            diagnostic = "signal sample rate must be finite and greater than zero";
            return std::nullopt;
        }
        if (!std::isfinite(config.frequency_hz) || config.frequency_hz < 0.0)
        {
            diagnostic = "signal frequency must be finite and non-negative";
            return std::nullopt;
        }
        if (!std::isfinite(config.amplitude) || config.amplitude < 0.0 ||
            !std::isfinite(config.offset) || !std::isfinite(config.constant_value) ||
            !std::isfinite(config.phase_radians))
        {
            diagnostic = "signal amplitude must be non-negative and all values must be finite";
            return std::nullopt;
        }
        return SignalGenerator{std::move(config)};
    }

    std::optional<SampleBlock> SignalGenerator::Generate(const std::size_t sample_count,
                                                          std::string &diagnostic)
    {
        diagnostic.clear();
        if (sample_count > std::numeric_limits<std::uint64_t>::max() - next_sample_index_)
        {
            diagnostic = "signal sample index range overflows uint64";
            return std::nullopt;
        }

        std::optional<SampleBlock> block = SampleBlock::Create(
            config_.sample_rate_hz,
            next_sample_index_,
            epoch_,
            false,
            {{0, config_.unit}},
            sample_count,
            diagnostic);
        if (!block.has_value())
        {
            return std::nullopt;
        }

        if (sample_count == 0)
        {
            return block;
        }

        const double alias_frequency_hz = std::fmod(config_.frequency_hz,
                                                     config_.sample_rate_hz);
        const double phase_step = (alias_frequency_hz / config_.sample_rate_hz) * kTwoPi;
        double next_phase = phase_radians_;
        std::uint64_t next_random_state = random_state_;
        std::span<double> samples = block->MutableChannelSamples(0);
        for (std::size_t index = 0; index < sample_count; ++index)
        {
            double sample = 0.0;
            switch (config_.kind)
            {
            case SignalKind::Constant:
                sample = config_.constant_value;
                break;
            case SignalKind::Impulse:
                sample = next_sample_index_ + static_cast<std::uint64_t>(index) ==
                        config_.impulse_sample_index
                    ? config_.offset + config_.amplitude
                    : config_.offset;
                break;
            case SignalKind::Sine:
                sample = config_.offset + config_.amplitude * std::sin(next_phase);
                next_phase += phase_step;
                if (next_phase >= kTwoPi)
                {
                    next_phase -= kTwoPi;
                }
                break;
            case SignalKind::Cosine:
                sample = config_.offset + config_.amplitude * std::cos(next_phase);
                next_phase += phase_step;
                if (next_phase >= kTwoPi)
                {
                    next_phase -= kTwoPi;
                }
                break;
            case SignalKind::Square:
                sample = config_.offset + (next_phase < std::numbers::pi_v<double>
                    ? config_.amplitude : -config_.amplitude);
                next_phase += phase_step;
                if (next_phase >= kTwoPi)
                {
                    next_phase -= kTwoPi;
                }
                break;
            case SignalKind::UniformNoise:
            {
                const std::uint64_t random_bits = NextSplitMix64(next_random_state);
                const double unit_interval = static_cast<double>(random_bits >> 11) *
                                             kUnit53Scale;
                sample = config_.offset + config_.amplitude *
                         (2.0 * unit_interval - 1.0);
                break;
            }
            }

            if (!std::isfinite(sample))
            {
                diagnostic = "signal generation produced a non-finite sample";
                return std::nullopt;
            }
            samples[index] = sample;
        }

        next_sample_index_ += static_cast<std::uint64_t>(sample_count);
        phase_radians_ = next_phase;
        random_state_ = next_random_state;
        return block;
    }

    bool SignalGenerator::Reset(std::string &diagnostic)
    {
        diagnostic.clear();
        if (epoch_ == std::numeric_limits<std::uint64_t>::max())
        {
            diagnostic = "signal epoch cannot be incremented";
            return false;
        }
        ++epoch_;
        next_sample_index_ = 0;
        random_state_ = config_.seed;
        phase_radians_ = WrapPhase(config_.phase_radians);
        return true;
    }
}
