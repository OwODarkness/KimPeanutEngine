#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "signal/sample_block.h"
#include "signal/dft.h"
#include "signal/fft.h"
#include "signal/signal_statistics.h"
#include "signal/spectrum.h"
#include "signal/signal_generator.h"
#include "signal/signal_mixer.h"
#include "signal_scope_session.h"

namespace
{
    using namespace kpengine::signal;

    TEST(SignalScopeSessionTest, DefaultCaptureMatchesOneKilohertzSine)
    {
        SignalScopeSession session;
        std::string diagnostic;
        session.Pause();
        ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        const auto snapshot = session.Snapshot();
        ASSERT_NE(snapshot, nullptr);
        ASSERT_NE(snapshot->capture, nullptr);
        ASSERT_EQ(snapshot->capture->size(), SignalScopeSession::kCaptureSize);
        ASSERT_TRUE(snapshot->trigger_sample.has_value());
        EXPECT_EQ(snapshot->first_sample, *snapshot->trigger_sample -
            SignalScopeSession::kPretriggerSamples);
        EXPECT_NEAR(snapshot->measurements.peak_to_peak, 2.0, 1.0e-12);
        EXPECT_NEAR(snapshot->measurements.rms, std::sqrt(0.5), 1.0e-12);
        ASSERT_TRUE(snapshot->frequency_available);
        EXPECT_NEAR(snapshot->measured_frequency_hz, 1'000.0, 1.0e-9);
    }

    TEST(SignalScopeSessionTest, AutoPublishesAnUntriggeredWindowWithoutInventingEdge)
    {
        SignalScopeSession session;
        std::string diagnostic;
        SignalGeneratorConfig constant;
        constant.kind = SignalKind::Constant;
        constant.constant_value = 0.25;
        ASSERT_TRUE(session.SetSource(constant, diagnostic)) << diagnostic;
        ASSERT_TRUE(session.SetTrigger(1.0, 0.0, ScopeTriggerEdge::Rising,
                                      ScopeTriggerMode::Auto, diagnostic)) << diagnostic;
        session.Pause();
        for (int i = 0; i < 19; ++i)
        {
            ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        }
        const auto snapshot = session.Snapshot();
        ASSERT_NE(snapshot, nullptr);
        ASSERT_NE(snapshot->capture, nullptr);
        EXPECT_FALSE(snapshot->trigger_sample.has_value());
        EXPECT_EQ(snapshot->state_label, "Auto / no trigger / stale");
        EXPECT_TRUE(snapshot->stale);
        EXPECT_EQ(snapshot->measurements.sample_count, SignalScopeSession::kCaptureSize);
        EXPECT_DOUBLE_EQ(snapshot->measurements.minimum, 0.25);
        EXPECT_DOUBLE_EQ(snapshot->measurements.maximum, 0.25);
    }

    TEST(SignalScopeSessionTest, PauseAndStepKeepTheSampleClockExplicit)
    {
        SignalScopeSession session;
        std::string diagnostic;
        session.Pause();
        ASSERT_TRUE(session.Tick(3.0, diagnostic)) << diagnostic;
        EXPECT_EQ(session.Snapshot()->sample_position, 0u);
        ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        EXPECT_EQ(session.Snapshot()->sample_position, SignalScopeSession::kBlockSize);
        session.Run();
        ASSERT_TRUE(session.Tick(1.0 / 60.0, diagnostic)) << diagnostic;
        EXPECT_EQ(session.Snapshot()->sample_position, 1'024u);
    }

    TEST(SignalScopeSessionTest, SingleCaptureStopsAndRequiresExplicitArm)
    {
        SignalScopeSession session;
        std::string diagnostic;
        ASSERT_TRUE(session.SetTrigger(0.0, 0.01, ScopeTriggerEdge::Rising,
                                      ScopeTriggerMode::Single, diagnostic)) << diagnostic;
        session.Pause();
        for (int i = 0; i < 4 &&
             session.Snapshot()->transport != ScopeTransport::Stopped; ++i)
        {
            ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        }
        const auto captured = session.Snapshot();
        ASSERT_NE(captured->capture, nullptr);
        EXPECT_EQ(captured->transport, ScopeTransport::Stopped);
        const auto position = captured->sample_position;
        ASSERT_TRUE(session.Tick(1.0, diagnostic)) << diagnostic;
        EXPECT_EQ(session.Snapshot()->sample_position, position);
        ASSERT_TRUE(session.Arm(diagnostic)) << diagnostic;
        EXPECT_EQ(session.Snapshot()->transport, ScopeTransport::Running);
    }

    TEST(SignalScopeSessionTest, RejectsInvalidSourceAndTriggerWithoutMutatingSource)
    {
        SignalScopeSession session;
        std::string diagnostic;
        const auto before = session.Snapshot();
        SignalGeneratorConfig invalid = before->source;
        invalid.sample_rate_hz = 0.0;
        EXPECT_FALSE(session.SetSource(invalid, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
        EXPECT_FALSE(session.SetTrigger(0.0, -1.0, ScopeTriggerEdge::Rising,
                                       ScopeTriggerMode::Auto, diagnostic));
        EXPECT_DOUBLE_EQ(session.Snapshot()->source.sample_rate_hz, 48'000.0);
    }

    TEST(SignalScopeSessionTest, DetectsRisingTriggerAcrossGenerationBlockBoundary)
    {
        SignalScopeSession session;
        std::string diagnostic;
        SignalGeneratorConfig source;
        source.frequency_hz = 100.0;
        source.phase_radians = -2.0 * std::numbers::pi_v<double> *
            source.frequency_hz / source.sample_rate_hz * 255.5;
        ASSERT_TRUE(session.SetSource(source, diagnostic)) << diagnostic;
        ASSERT_TRUE(session.SetTrigger(0.0, 0.01, ScopeTriggerEdge::Rising,
                                      ScopeTriggerMode::Normal, diagnostic)) << diagnostic;
        session.Pause();
        for (int i = 0; i < 3; ++i)
        {
            ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        }
        const auto snapshot = session.Snapshot();
        ASSERT_TRUE(snapshot->trigger_sample.has_value());
        EXPECT_EQ(*snapshot->trigger_sample, 256u);
        EXPECT_EQ(snapshot->first_sample, 136u);
    }

    TEST(SignalScopeSessionTest, FallingTriggerUsesTheDownwardCrossing)
    {
        SignalScopeSession session;
        std::string diagnostic;
        SignalGeneratorConfig source;
        source.phase_radians = std::numbers::pi_v<double> / 2.0;
        ASSERT_TRUE(session.SetSource(source, diagnostic)) << diagnostic;
        ASSERT_TRUE(session.SetTrigger(0.0, 0.01, ScopeTriggerEdge::Falling,
                                      ScopeTriggerMode::Normal, diagnostic)) << diagnostic;
        session.Pause();
        for (int i = 0; i < 3; ++i)
        {
            ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        }
        const auto snapshot = session.Snapshot();
        ASSERT_TRUE(snapshot->trigger_sample.has_value());
        ASSERT_NE(snapshot->capture, nullptr);
        const auto trigger_offset = SignalScopeSession::kPretriggerSamples;
        EXPECT_GT((*snapshot->capture)[trigger_offset - 1], 0.0);
        EXPECT_LE((*snapshot->capture)[trigger_offset], 0.0);
    }

    TEST(SignalScopeSessionTest, CaptureStorageSurvivesAcquisitionRingWrap)
    {
        SignalScopeSession session;
        std::string diagnostic;
        ASSERT_TRUE(session.SetTrigger(0.0, 0.01, ScopeTriggerEdge::Rising,
                                      ScopeTriggerMode::Normal, diagnostic)) << diagnostic;
        session.Pause();
        for (std::size_t i = 0; i <
             SignalScopeSession::kRingCapacity / SignalScopeSession::kBlockSize + 4; ++i)
        {
            ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        }
        const auto snapshot = session.Snapshot();
        ASSERT_TRUE(snapshot->trigger_sample.has_value());
        ASSERT_NE(snapshot->capture, nullptr);
        EXPECT_GT(*snapshot->trigger_sample, SignalScopeSession::kRingCapacity);
        EXPECT_EQ(snapshot->capture->size(), SignalScopeSession::kCaptureSize);
    }

    TEST(SignalScopeSessionTest, EqualElapsedTimeHasTheSameAcquiredCountAtDifferentUiRates)
    {
        std::array<std::uint64_t, 3> positions{};
        constexpr std::array<int, 3> frame_rates{30, 60, 144};
        for (std::size_t rate_index = 0; rate_index < frame_rates.size(); ++rate_index)
        {
            SignalScopeSession session;
            std::string diagnostic;
            for (int frame = 0; frame < frame_rates[rate_index]; ++frame)
            {
                ASSERT_TRUE(session.Tick(1.0 / frame_rates[rate_index], diagnostic))
                    << diagnostic;
            }
            positions[rate_index] = session.Snapshot()->sample_position;
        }
        EXPECT_EQ(positions[0], positions[1]);
        EXPECT_EQ(positions[1], positions[2]);
        EXPECT_EQ(positions[0], (48'000 / SignalScopeSession::kBlockSize) *
                                 SignalScopeSession::kBlockSize);
    }

    TEST(SignalScopeSessionTest, TimebaseChangesCaptureWindowWithoutChangingSampleClock)
    {
        SignalScopeSession session;
        std::string diagnostic;
        session.Pause();
        ASSERT_TRUE(session.SetTimebaseMs(5.0, diagnostic)) << diagnostic;
        ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        const auto snapshot = session.Snapshot();
        ASSERT_NE(snapshot->capture, nullptr);
        EXPECT_DOUBLE_EQ(snapshot->timebase_ms, 5.0);
        EXPECT_EQ(snapshot->capture_size, 240u);
        EXPECT_EQ(snapshot->pretrigger_samples, 60u);
        EXPECT_EQ(snapshot->sample_position, 512u);
        EXPECT_NEAR(snapshot->measurements.rms, std::sqrt(0.5), 1.0e-12);

        EXPECT_FALSE(session.SetTimebaseMs(2'000.0, diagnostic));
        EXPECT_DOUBLE_EQ(session.Snapshot()->timebase_ms, 5.0);
    }

    TEST(SignalScopeSessionTest, HoldoffPreventsEarlyRetriggering)
    {
        SignalScopeSession session;
        std::string diagnostic;
        ASSERT_TRUE(session.SetTrigger(0.0, 0.01, ScopeTriggerEdge::Rising,
                                      ScopeTriggerMode::Normal, diagnostic)) << diagnostic;
        ASSERT_TRUE(session.SetHoldoffSamples(1'000, diagnostic)) << diagnostic;
        session.Pause();
        for (int i = 0; i < 10; ++i)
        {
            ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        }
        const auto snapshot = session.Snapshot();
        ASSERT_TRUE(snapshot->trigger_sample.has_value());
        EXPECT_GE(*snapshot->trigger_sample, 1'145u);
        EXPECT_EQ(snapshot->holdoff_samples, 1'000u);
    }

    TEST(SignalScopeSessionTest, ConstantAtThresholdDoesNotProduceAnEdge)
    {
        SignalScopeSession session;
        std::string diagnostic;
        SignalGeneratorConfig source;
        source.kind = SignalKind::Constant;
        source.constant_value = 1.0;
        ASSERT_TRUE(session.SetSource(source, diagnostic)) << diagnostic;
        ASSERT_TRUE(session.SetTrigger(1.0, 0.0, ScopeTriggerEdge::Rising,
                                      ScopeTriggerMode::Normal, diagnostic)) << diagnostic;
        session.Pause();
        for (int i = 0; i < 20; ++i)
        {
            ASSERT_TRUE(session.Step(diagnostic)) << diagnostic;
        }
        EXPECT_EQ(session.Snapshot()->capture, nullptr);
        EXPECT_FALSE(session.Snapshot()->trigger_sample.has_value());
    }

    SignalGenerator MakeSineGenerator()
    {
        std::string diagnostic;
        auto generator = SignalGenerator::Create({}, diagnostic);
        EXPECT_TRUE(generator.has_value()) << diagnostic;
        return std::move(*generator);
    }

    TEST(SampleBlockTest, ValidatesMetadataAndAllocationBounds)
    {
        std::string diagnostic;
        const std::vector<ChannelDescriptor> mono{{0, SampleUnit::Volt}};

        EXPECT_FALSE(SampleBlock::Create(0.0, 0, 0, false, mono, 1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(std::numeric_limits<double>::infinity(),
                                         0, 0, false, mono, 1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(48'000.0, 0, 0, false, {}, 1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(48'000.0, 0, 0, false,
                                         {{1, SampleUnit::Volt}, {1, SampleUnit::Volt}},
                                         1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(48'000.0, 0, 0, false, mono,
                                         SampleBlock::kMaximumSampleCount + 1, diagnostic));
        EXPECT_FALSE(SampleBlock::Create(48'000.0,
                                         std::numeric_limits<std::uint64_t>::max(),
                                         0, false, mono, 1, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(SampleBlockTest, ChannelsAreViewsIntoOneContiguousPlanarAllocation)
    {
        std::string diagnostic;
        auto block = SampleBlock::Create(48'000.0, 12, 3, false,
            {{0, SampleUnit::Volt}, {1, SampleUnit::NormalizedFullScale}}, 4, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;

        auto channel_0 = block->MutableChannelSamples(0);
        auto channel_1 = block->MutableChannelSamples(1);
        ASSERT_EQ(channel_0.size(), 4u);
        ASSERT_EQ(channel_1.size(), 4u);
        EXPECT_EQ(channel_0.data() + channel_0.size(), channel_1.data());

        channel_0[2] = 0.5;
        const SampleBlockView view = block->View();
        EXPECT_DOUBLE_EQ(view.ChannelSamples(0)[2], 0.5);
        EXPECT_EQ(view.SampleRateHz(), 48'000.0);
        EXPECT_EQ(view.FirstSampleIndex(), 12u);
        EXPECT_EQ(view.Epoch(), 3u);
        EXPECT_EQ(view.Channels().size(), 2u);
        EXPECT_TRUE(view.ChannelSamples(2).empty());
        static_assert(std::is_same_v<decltype(view.ChannelSamples(0)),
                                     std::span<const double>>);
    }

    TEST(SignalGeneratorTest, EmitsQuarterCycleSineSamplesAndMeasurements)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Sine;
        config.sample_rate_hz = 4'000.0;
        config.frequency_hz = 1'000.0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        auto block = generator->Generate(4, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;
        const auto samples = block->ChannelSamples(0);
        ASSERT_EQ(samples.size(), 4u);
        EXPECT_NEAR(samples[0], 0.0, 1e-14);
        EXPECT_NEAR(samples[1], 1.0, 1e-14);
        EXPECT_NEAR(samples[2], 0.0, 1e-14);
        EXPECT_NEAR(samples[3], -1.0, 1e-14);

        const auto statistics = Measure(samples, diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_NEAR(statistics->mean, 0.0, 1e-14);
        EXPECT_NEAR(statistics->rms, std::sqrt(0.5), 1e-14);
        EXPECT_NEAR(statistics->peak_to_peak, 2.0, 1e-14);
    }

    TEST(SignalGeneratorTest, OneKilohertzSineBlockHasExpectedStatistics)
    {
        std::string diagnostic;
        auto generator = MakeSineGenerator();
        auto block = generator.Generate(4'800, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;

        const auto statistics = Measure(block->ChannelSamples(0), diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_EQ(statistics->sample_count, 4'800u);
        EXPECT_NEAR(statistics->mean, 0.0, 1e-12);
        EXPECT_NEAR(statistics->rms, std::sqrt(0.5), 1e-12);
        EXPECT_NEAR(statistics->minimum, -1.0, 1e-12);
        EXPECT_NEAR(statistics->maximum, 1.0, 1e-12);
    }

    TEST(SignalGeneratorTest, SineIsPartitionInvariantAndEmptyCallsDoNotAdvance)
    {
        std::string diagnostic;
        auto contiguous_generator = MakeSineGenerator();
        auto partitioned_generator = MakeSineGenerator();
        auto contiguous = contiguous_generator.Generate(4'369, diagnostic);
        ASSERT_TRUE(contiguous.has_value()) << diagnostic;

        std::vector<double> partitioned_samples;
        for (const std::size_t count : std::array<std::size_t, 5>{1, 17, 255, 0, 4'096})
        {
            const std::uint64_t previous_index = partitioned_generator.NextSampleIndex();
            auto part = partitioned_generator.Generate(count, diagnostic);
            ASSERT_TRUE(part.has_value()) << diagnostic;
            if (count == 0)
            {
                EXPECT_EQ(partitioned_generator.NextSampleIndex(), previous_index);
            }
            const auto channel = part->ChannelSamples(0);
            partitioned_samples.insert(partitioned_samples.end(), channel.begin(), channel.end());
        }

        ASSERT_EQ(partitioned_samples.size(), contiguous->SampleCount());
        const auto expected = contiguous->ChannelSamples(0);
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(partitioned_samples[i], expected[i]);
        }
        EXPECT_EQ(partitioned_generator.NextSampleIndex(), contiguous_generator.NextSampleIndex());
    }

    TEST(SignalGeneratorTest, HarmonicCompositeHasExpectedFundamentalAndHarmonicLevels)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Sine;
        config.sample_rate_hz = 48'000.0;
        config.frequency_hz = 1'000.0;
        config.amplitude = 1.0;
        config.harmonic_distortion = 0.2;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;
        auto block = generator->Generate(4'800, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;

        const auto samples = block->ChannelSamples(0);
        const auto measured_amplitude = [samples](const std::size_t harmonic)
        {
            double sine = 0.0;
            double cosine = 0.0;
            for (std::size_t index = 0; index < samples.size(); ++index)
            {
                const double phase = 2.0 * std::numbers::pi_v<double> *
                    static_cast<double>(harmonic * index) /
                    static_cast<double>(samples.size());
                sine += samples[index] * std::sin(phase);
                cosine += samples[index] * std::cos(phase);
            }
            return 2.0 * std::hypot(sine, cosine) /
                   static_cast<double>(samples.size());
        };
        const double weight_norm = std::sqrt(0.65 * 0.65 + 0.35 * 0.35);
        EXPECT_NEAR(measured_amplitude(100), 1.0, 1e-12);
        EXPECT_NEAR(measured_amplitude(200), 0.2 * 0.65 / weight_norm, 1e-12);
        EXPECT_NEAR(measured_amplitude(300), 0.2 * 0.35 / weight_norm, 1e-12);

        const auto statistics = Measure(samples, diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_NEAR(statistics->rms, std::sqrt(0.5 * (1.0 + 0.2 * 0.2)), 1e-12);
    }

    TEST(SignalGeneratorTest, CompositeNoiseIsReproducibleAcrossPartitionsAndReset)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Sine;
        config.sample_rate_hz = 48'000.0;
        config.frequency_hz = 1'000.0;
        config.harmonic_distortion = 0.18;
        config.noise_rms = 0.04;
        config.seed = 0xA581D3ULL;
        std::string diagnostic;
        auto contiguous_generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(contiguous_generator.has_value()) << diagnostic;
        auto partitioned_generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(partitioned_generator.has_value()) << diagnostic;
        auto contiguous = contiguous_generator->Generate(4'369, diagnostic);
        ASSERT_TRUE(contiguous.has_value()) << diagnostic;

        std::vector<double> partitioned_samples;
        for (const std::size_t count : std::array<std::size_t, 5>{1, 17, 255, 0, 4'096})
        {
            auto part = partitioned_generator->Generate(count, diagnostic);
            ASSERT_TRUE(part.has_value()) << diagnostic;
            const auto channel = part->ChannelSamples(0);
            partitioned_samples.insert(partitioned_samples.end(), channel.begin(), channel.end());
        }
        ASSERT_EQ(partitioned_samples.size(), contiguous->SampleCount());
        const auto expected = contiguous->ChannelSamples(0);
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            EXPECT_DOUBLE_EQ(partitioned_samples[index], expected[index]) << "sample " << index;
        }

        ASSERT_TRUE(partitioned_generator->Reset(diagnostic)) << diagnostic;
        auto replay = partitioned_generator->Generate(64, diagnostic);
        ASSERT_TRUE(replay.has_value()) << diagnostic;
        EXPECT_TRUE(std::equal(replay->ChannelSamples(0).begin(),
                               replay->ChannelSamples(0).end(),
                               expected.begin()));
    }

    TEST(SignalGeneratorTest, AdditiveNoiseMatchesConfiguredRms)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Constant;
        config.noise_rms = 0.2;
        config.seed = 0xC0FFEEULL;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;
        auto block = generator->Generate(65'536, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;

        const auto samples = block->ChannelSamples(0);
        const auto statistics = Measure(samples, diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_NEAR(statistics->rms, config.noise_rms, 0.002);
        EXPECT_NEAR(statistics->mean, 0.0, 0.002);
        const double peak_limit = std::sqrt(3.0) * config.noise_rms;
        EXPECT_GE(statistics->minimum, -peak_limit);
        EXPECT_LE(statistics->maximum, peak_limit);
    }

    TEST(SignalGeneratorTest, SineResetStartsANewEpochAtSampleZero)
    {
        std::string diagnostic;
        auto generator = MakeSineGenerator();
        ASSERT_TRUE(generator.Generate(32, diagnostic).has_value()) << diagnostic;
        ASSERT_TRUE(generator.Reset(diagnostic)) << diagnostic;
        auto block = generator.Generate(8, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;
        EXPECT_EQ(block->Epoch(), 1u);
        EXPECT_EQ(block->FirstSampleIndex(), 0u);
        EXPECT_NEAR(block->ChannelSamples(0)[0], 0.0, 1e-14);
    }

    TEST(SignalGeneratorTest, SawtoothAndTriangleMatchNaivePeriodicSamples)
    {
        const std::array<std::pair<SignalKind, std::array<double, 8>>, 2> fixtures{{
            {SignalKind::Sawtooth, {-1.0, -0.75, -0.5, -0.25, 0.0, 0.25, 0.5, 0.75}},
            {SignalKind::Triangle, {-1.0, -0.5, 0.0, 0.5, 1.0, 0.5, 0.0, -0.5}}
        }};

        for (const auto &[kind, expected] : fixtures)
        {
            SignalGeneratorConfig config;
            config.kind = kind;
            config.sample_rate_hz = 8'000.0;
            config.frequency_hz = 1'000.0;
            config.amplitude = 1.0;
            config.unit = SampleUnit::Volt;
            std::string diagnostic;
            auto generator = SignalGenerator::Create(config, diagnostic);
            ASSERT_TRUE(generator.has_value()) << diagnostic;
            auto block = generator->Generate(expected.size(), diagnostic);
            ASSERT_TRUE(block.has_value()) << diagnostic;
            const auto samples = block->ChannelSamples(0);
            for (std::size_t index = 0; index < expected.size(); ++index)
            {
                EXPECT_NEAR(samples[index], expected[index], 1.0e-14)
                    << "kind " << static_cast<int>(kind) << ", sample " << index;
            }
        }
    }

    TEST(SignalGeneratorTest, SawtoothAndTrianglePreservePhaseAcrossPartitions)
    {
        for (const SignalKind kind : {SignalKind::Sawtooth, SignalKind::Triangle})
        {
            SignalGeneratorConfig config;
            config.kind = kind;
            config.sample_rate_hz = 44'100.0;
            config.frequency_hz = 997.0;
            config.phase_radians = 0.41;
            std::string diagnostic;
            auto contiguous_generator = SignalGenerator::Create(config, diagnostic);
            ASSERT_TRUE(contiguous_generator.has_value()) << diagnostic;
            auto partitioned_generator = SignalGenerator::Create(config, diagnostic);
            ASSERT_TRUE(partitioned_generator.has_value()) << diagnostic;
            auto contiguous = contiguous_generator->Generate(1'337, diagnostic);
            ASSERT_TRUE(contiguous.has_value()) << diagnostic;

            std::vector<double> partitioned;
            for (const std::size_t count : {1u, 127u, 0u, 256u, 953u})
            {
                auto block = partitioned_generator->Generate(count, diagnostic);
                ASSERT_TRUE(block.has_value()) << diagnostic;
                const auto samples = block->ChannelSamples(0);
                partitioned.insert(partitioned.end(), samples.begin(), samples.end());
            }
            ASSERT_EQ(partitioned.size(), contiguous->SampleCount());
            const auto expected = contiguous->ChannelSamples(0);
            for (std::size_t index = 0; index < expected.size(); ++index)
            {
                EXPECT_DOUBLE_EQ(partitioned[index], expected[index]) << "sample " << index;
            }
        }
    }

    TEST(SignalMixerTest, MixesAlignedSignalsSampleBySample)
    {
        SignalGeneratorConfig sine;
        sine.kind = SignalKind::Sine;
        sine.sample_rate_hz = 4'000.0;
        sine.frequency_hz = 1'000.0;
        sine.unit = SampleUnit::Volt;
        SignalGeneratorConfig cosine = sine;
        cosine.kind = SignalKind::Cosine;
        cosine.amplitude = 0.5;
        std::string diagnostic;
        auto first_generator = SignalGenerator::Create(sine, diagnostic);
        ASSERT_TRUE(first_generator.has_value()) << diagnostic;
        auto second_generator = SignalGenerator::Create(cosine, diagnostic);
        ASSERT_TRUE(second_generator.has_value()) << diagnostic;
        auto first = first_generator->Generate(4, diagnostic);
        ASSERT_TRUE(first.has_value()) << diagnostic;
        auto second = second_generator->Generate(4, diagnostic);
        ASSERT_TRUE(second.has_value()) << diagnostic;

        auto mixed = MixSignals(first->View(), second->View(), diagnostic);
        ASSERT_TRUE(mixed.has_value()) << diagnostic;
        ASSERT_EQ(mixed->SampleCount(), 4u);
        ASSERT_EQ(mixed->Channels()[0].unit, SampleUnit::Volt);
        const std::array<double, 4> expected{0.5, 1.0, -0.5, -1.0};
        const auto actual = mixed->ChannelSamples(0);
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            EXPECT_NEAR(actual[index], expected[index], 1.0e-14);
        }
        const auto statistics = Measure(actual, diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_NEAR(statistics->rms, std::sqrt(0.625), 1.0e-14);
    }

    TEST(SignalMixerTest, RejectsInputsWithoutAnIdenticalSampleClockAndUnit)
    {
        const auto make_block = [](const double rate, const std::uint64_t first_sample,
                                   const SampleUnit unit, std::string &diagnostic)
            -> std::optional<SampleBlock>
        {
            auto block = SampleBlock::Create(rate, first_sample, 0, false,
                {{0, unit}}, 4, diagnostic);
            if (block.has_value())
            {
                const auto samples = block->MutableChannelSamples(0);
                std::fill(samples.begin(), samples.end(), 0.25);
            }
            return block;
        };
        std::string diagnostic;
        auto reference = make_block(48'000.0, 0, SampleUnit::Volt, diagnostic);
        ASSERT_TRUE(reference.has_value()) << diagnostic;
        auto wrong_rate = make_block(44'100.0, 0, SampleUnit::Volt, diagnostic);
        ASSERT_TRUE(wrong_rate.has_value()) << diagnostic;
        auto wrong_index = make_block(48'000.0, 1, SampleUnit::Volt, diagnostic);
        ASSERT_TRUE(wrong_index.has_value()) << diagnostic;
        auto wrong_unit = make_block(48'000.0, 0, SampleUnit::Dimensionless, diagnostic);
        ASSERT_TRUE(wrong_unit.has_value()) << diagnostic;

        EXPECT_FALSE(MixSignals(reference->View(), wrong_rate->View(), diagnostic));
        EXPECT_FALSE(diagnostic.empty());
        EXPECT_FALSE(MixSignals(reference->View(), wrong_index->View(), diagnostic));
        EXPECT_FALSE(MixSignals(reference->View(), wrong_unit->View(), diagnostic));
    }

    TEST(SignalGeneratorTest, GeneratesConstantValue)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Constant;
        config.constant_value = 2.0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        auto block = generator->Generate(64, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;
        ASSERT_EQ(block->ChannelSamples(0).size(), 64u);
        for (const double sample : block->ChannelSamples(0))
        {
            EXPECT_DOUBLE_EQ(sample, 2.0);
        }

        const auto statistics = Measure(block->ChannelSamples(0), diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_DOUBLE_EQ(statistics->mean, 2.0);
        EXPECT_DOUBLE_EQ(statistics->rms, 2.0);
        EXPECT_DOUBLE_EQ(statistics->peak_to_peak, 0.0);
    }

    TEST(SignalGeneratorTest, UnitImpulseMatchesStatisticsAcrossABlockBoundary)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::Impulse;
        config.impulse_sample_index = 3;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        auto before_impulse = generator->Generate(3, diagnostic);
        ASSERT_TRUE(before_impulse.has_value()) << diagnostic;
        auto after_boundary = generator->Generate(61, diagnostic);
        ASSERT_TRUE(after_boundary.has_value()) << diagnostic;
        const auto baseline = before_impulse->ChannelSamples(0);
        const auto impulse = after_boundary->ChannelSamples(0);
        for (const double sample : baseline)
        {
            EXPECT_DOUBLE_EQ(sample, 0.0);
        }
        ASSERT_EQ(impulse.size(), 61u);
        EXPECT_DOUBLE_EQ(impulse[0], 1.0);
        for (std::size_t i = 1; i < impulse.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(impulse[i], 0.0);
        }

        std::vector<double> complete_run;
        complete_run.insert(complete_run.end(), baseline.begin(), baseline.end());
        complete_run.insert(complete_run.end(), impulse.begin(), impulse.end());
        const auto statistics = Measure(complete_run, diagnostic);
        ASSERT_TRUE(statistics.has_value()) << diagnostic;
        EXPECT_EQ(statistics->sample_count, 64u);
        EXPECT_DOUBLE_EQ(statistics->mean, 1.0 / 64.0);
        EXPECT_DOUBLE_EQ(statistics->rms, 1.0 / 8.0);
        EXPECT_DOUBLE_EQ(statistics->minimum, 0.0);
        EXPECT_DOUBLE_EQ(statistics->maximum, 1.0);
        EXPECT_DOUBLE_EQ(statistics->peak_to_peak, 1.0);
    }

    TEST(SignalGeneratorTest, FailedOversizedRequestDoesNotAdvanceState)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::UniformNoise;
        config.seed = 0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        EXPECT_FALSE(generator->Generate(SampleBlock::kMaximumSampleCount + 1, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
        EXPECT_EQ(generator->NextSampleIndex(), 0u);

        auto after_failure = generator->Generate(8, diagnostic);
        ASSERT_TRUE(after_failure.has_value()) << diagnostic;
        const std::array<double, 8> expected{
            0.7666216164272852,
            -0.13694400590298006,
            -0.9471324568148045,
            0.941763956307657,
            -0.7873066168655751,
            -0.3453484715637485,
            -0.6522642680806343,
            0.543093112663134};
        const auto actual = after_failure->ChannelSamples(0);
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(actual[i], expected[i]) << "sample " << i;
        }
    }

    TEST(SignalGeneratorTest, SeededUniformNoiseMatchesSplitMix64GoldenSamples)
    {
        SignalGeneratorConfig config;
        config.kind = SignalKind::UniformNoise;
        config.seed = 0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;

        auto block = generator->Generate(8, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;
        const std::array<double, 8> expected{
            0.7666216164272852,
            -0.13694400590298006,
            -0.9471324568148045,
            0.941763956307657,
            -0.7873066168655751,
            -0.3453484715637485,
            -0.6522642680806343,
            0.543093112663134};
        const auto actual = block->ChannelSamples(0);
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(actual[i], expected[i]) << "sample " << i;
        }
    }

    TEST(SignalGeneratorTest, SeededNoiseIsPartitionInvariantAndResetReplaysSequence)
    {
        std::string diagnostic;
        SignalGeneratorConfig config;
        config.kind = SignalKind::UniformNoise;
        config.seed = 0x123456789ABCDEF0ULL;
        config.amplitude = 2.0;
        config.offset = -0.25;
        auto contiguous_generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(contiguous_generator.has_value()) << diagnostic;
        auto partitioned_generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(partitioned_generator.has_value()) << diagnostic;

        auto contiguous = contiguous_generator->Generate(4'369, diagnostic);
        ASSERT_TRUE(contiguous.has_value()) << diagnostic;
        std::vector<double> partitioned_samples;
        for (const std::size_t count : std::array<std::size_t, 5>{1, 17, 255, 0, 4'096})
        {
            const std::uint64_t previous_index = partitioned_generator->NextSampleIndex();
            auto part = partitioned_generator->Generate(count, diagnostic);
            ASSERT_TRUE(part.has_value()) << diagnostic;
            if (count == 0)
            {
                EXPECT_EQ(partitioned_generator->NextSampleIndex(), previous_index);
            }
            const auto channel = part->ChannelSamples(0);
            partitioned_samples.insert(partitioned_samples.end(), channel.begin(), channel.end());
        }

        ASSERT_EQ(partitioned_samples.size(), contiguous->SampleCount());
        const auto expected = contiguous->ChannelSamples(0);
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(partitioned_samples[i], expected[i]);
        }
        EXPECT_EQ(partitioned_generator->NextSampleIndex(),
                  contiguous_generator->NextSampleIndex());

        ASSERT_TRUE(partitioned_generator->Reset(diagnostic)) << diagnostic;
        auto replay = partitioned_generator->Generate(8, diagnostic);
        ASSERT_TRUE(replay.has_value()) << diagnostic;
        EXPECT_EQ(replay->Epoch(), 1u);
        EXPECT_EQ(replay->FirstSampleIndex(), 0u);
        auto same_seed = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(same_seed.has_value()) << diagnostic;
        auto first_run = same_seed->Generate(8, diagnostic);
        ASSERT_TRUE(first_run.has_value()) << diagnostic;
        EXPECT_TRUE(std::equal(replay->ChannelSamples(0).begin(),
                               replay->ChannelSamples(0).end(),
                               first_run->ChannelSamples(0).begin()));
    }

    TEST(SignalGeneratorTest, InvalidGeneratorConfigurationIsRejected)
    {
        std::string diagnostic;
        SignalGeneratorConfig config;
        config.kind = static_cast<SignalKind>(255);
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        config.kind = SignalKind::UniformNoise;
        config.amplitude = -1.0;
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        config.amplitude = std::numeric_limits<double>::quiet_NaN();
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        config.amplitude = 1.0;
        config.sample_rate_hz = 0.0;
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        config.sample_rate_hz = 48'000.0;
        config.harmonic_distortion = 1.01;
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        config.harmonic_distortion = 0.0;
        config.noise_rms = -0.1;
        EXPECT_FALSE(SignalGenerator::Create(config, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(SignalStatisticsTest, RejectsEmptyAndNonFiniteInputs)
    {
        std::string diagnostic;
        EXPECT_FALSE(Measure({}, diagnostic));
        const std::array<double, 2> invalid{1.0, std::numeric_limits<double>::quiet_NaN()};
        EXPECT_FALSE(Measure(invalid, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(SignalDftTest, ImpulseHasFlatSpectrumAndConstantHasOnlyDc)
    {
        constexpr std::size_t count = 8;
        std::string diagnostic;
        std::array<std::complex<double>, count> impulse{};
        impulse[0] = {1.0, 0.0};
        auto impulse_bins = ForwardDft(impulse, diagnostic);
        ASSERT_TRUE(impulse_bins.has_value()) << diagnostic;
        for (const auto bin : *impulse_bins)
        {
            EXPECT_NEAR(bin.real(), 1.0, 1.0e-12);
            EXPECT_NEAR(bin.imag(), 0.0, 1.0e-12);
        }

        std::array<std::complex<double>, count> constant{};
        constant.fill({2.0, 0.0});
        auto constant_bins = ForwardDft(constant, diagnostic);
        ASSERT_TRUE(constant_bins.has_value()) << diagnostic;
        EXPECT_NEAR((*constant_bins)[0].real(), 16.0, 1.0e-12);
        EXPECT_NEAR((*constant_bins)[0].imag(), 0.0, 1.0e-12);
        for (std::size_t bin = 1; bin < count; ++bin)
        {
            EXPECT_NEAR(std::abs((*constant_bins)[bin]), 0.0, 1.0e-12);
        }
    }

    TEST(SignalDftTest, ForwardTransformUsesNegativeExponentAndNaturalBinOrder)
    {
        constexpr std::size_t count = 8;
        constexpr std::size_t expected_bin = 3;
        std::array<std::complex<double>, count> tone{};
        for (std::size_t sample = 0; sample < count; ++sample)
        {
            const double phase = 2.0 * std::numbers::pi_v<double> *
                static_cast<double>(expected_bin * sample) / static_cast<double>(count);
            tone[sample] = {std::cos(phase), std::sin(phase)};
        }

        std::string diagnostic;
        auto bins = ForwardDft(tone, diagnostic);
        ASSERT_TRUE(bins.has_value()) << diagnostic;
        EXPECT_NEAR((*bins)[expected_bin].real(), static_cast<double>(count), 1.0e-12);
        EXPECT_NEAR((*bins)[expected_bin].imag(), 0.0, 1.0e-12);
        for (std::size_t bin = 0; bin < count; ++bin)
        {
            if (bin != expected_bin)
            {
                EXPECT_NEAR(std::abs((*bins)[bin]), 0.0, 1.0e-12);
            }
        }
    }

    TEST(SignalDftTest, InverseUsesPositiveExponentAndOneOverNNormalization)
    {
        constexpr std::size_t count = 8;
        constexpr std::size_t tone_bin = 2;
        std::array<std::complex<double>, count> bins{};
        bins[tone_bin] = {static_cast<double>(count), 0.0};

        std::string diagnostic;
        auto samples = InverseDft(bins, diagnostic);
        ASSERT_TRUE(samples.has_value()) << diagnostic;
        for (std::size_t sample = 0; sample < count; ++sample)
        {
            const double phase = 2.0 * std::numbers::pi_v<double> *
                static_cast<double>(tone_bin * sample) / static_cast<double>(count);
            EXPECT_NEAR((*samples)[sample].real(), std::cos(phase), 1.0e-12);
            EXPECT_NEAR((*samples)[sample].imag(), std::sin(phase), 1.0e-12);
        }
    }

    TEST(SignalDftTest, ForwardAndInverseRoundTripComplexSamples)
    {
        const std::array<std::complex<double>, 5> original{{
            {0.25, -0.5}, {1.0, 0.125}, {-0.75, 0.5}, {0.0, -1.0}, {0.3, 0.7}}};
        std::string diagnostic;
        auto bins = ForwardDft(original, diagnostic);
        ASSERT_TRUE(bins.has_value()) << diagnostic;
        auto reconstructed = InverseDft(*bins, diagnostic);
        ASSERT_TRUE(reconstructed.has_value()) << diagnostic;
        for (std::size_t sample = 0; sample < original.size(); ++sample)
        {
            EXPECT_NEAR((*reconstructed)[sample].real(), original[sample].real(), 1.0e-12);
            EXPECT_NEAR((*reconstructed)[sample].imag(), original[sample].imag(), 1.0e-12);
        }
    }

    TEST(SignalDftTest, RejectsEmptyAndNonFiniteInputs)
    {
        std::string diagnostic;
        EXPECT_FALSE(ForwardDft({}, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        const std::array<std::complex<double>, 2> invalid{{
            {1.0, 0.0}, {0.0, std::numeric_limits<double>::infinity()}}};
        EXPECT_FALSE(InverseDft(invalid, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(SignalDftTest, RejectsInputsAboveTheDirectReferenceLimit)
    {
        const std::vector<std::complex<double>> oversized(
            kMaximumDirectDftSize + 1, {0.0, 0.0});
        std::string diagnostic;
        EXPECT_FALSE(ForwardDft(oversized, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(SignalDftTest, RejectsAccumulationOverflowFromFiniteSamples)
    {
        const std::array<std::complex<double>, 2> large{{
            {std::numeric_limits<double>::max(), 0.0},
            {std::numeric_limits<double>::max(), 0.0}}};
        std::string diagnostic;
        EXPECT_FALSE(ForwardDft(large, diagnostic));
        EXPECT_NE(diagnostic.find("overflowed"), std::string::npos);
    }

    TEST(SignalFftTest, MatchesDirectDftForSeededComplexInputs)
    {
        std::uint64_t state = 0xD1B54A32D192ED03ULL;
        const auto next_sample = [&state]()
        {
            state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
            return static_cast<double>(state >> 11) * 0x1.0p-53 - 0.5;
        };

        std::string diagnostic;
        for (const std::size_t count : std::array<std::size_t, 6>{8, 16, 32, 64, 128, 256})
        {
            std::vector<std::complex<double>> input(count);
            double maximum_input = 0.0;
            for (auto &sample : input)
            {
                sample = {next_sample(), next_sample()};
                maximum_input = std::max(maximum_input, std::abs(sample));
            }

            const auto reference = ForwardDft(input, diagnostic);
            ASSERT_TRUE(reference.has_value()) << diagnostic;
            const auto actual = ForwardFft(input, diagnostic);
            ASSERT_TRUE(actual.has_value()) << diagnostic;
            ASSERT_EQ(actual->size(), reference->size());

            double error_energy = 0.0;
            double reference_energy = 0.0;
            double maximum_error = 0.0;
            for (std::size_t bin = 0; bin < count; ++bin)
            {
                const double error = std::abs((*actual)[bin] - (*reference)[bin]);
                error_energy += error * error;
                reference_energy += std::norm((*reference)[bin]);
                maximum_error = std::max(maximum_error, error);
            }
            const double normalized_l2 = std::sqrt(error_energy) /
                std::max(std::sqrt(reference_energy), 1.0e-30);
            EXPECT_LE(normalized_l2, 1.0e-10) << "N=" << count;
            EXPECT_LE(maximum_error,
                      1.0e-10 * std::max(1.0, static_cast<double>(count) * maximum_input))
                << "N=" << count;
        }
    }

    TEST(SignalFftTest, InverseUsesPositiveExponentAndOneOverNNormalization)
    {
        constexpr std::size_t count = 8;
        constexpr std::size_t tone_bin = 2;
        std::array<std::complex<double>, count> bins{};
        bins[tone_bin] = {static_cast<double>(count), 0.0};

        std::string diagnostic;
        const auto samples = InverseFft(bins, diagnostic);
        ASSERT_TRUE(samples.has_value()) << diagnostic;
        for (std::size_t sample = 0; sample < count; ++sample)
        {
            const double phase = 2.0 * std::numbers::pi_v<double> *
                static_cast<double>(tone_bin * sample) / static_cast<double>(count);
            EXPECT_NEAR((*samples)[sample].real(), std::cos(phase), 1.0e-12);
            EXPECT_NEAR((*samples)[sample].imag(), std::sin(phase), 1.0e-12);
        }
    }

    TEST(SignalFftTest, RejectsInvalidSizesValuesAndOverflow)
    {
        std::string diagnostic;
        EXPECT_FALSE(ForwardFft({}, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        const std::array<std::complex<double>, 3> non_power_of_two{};
        EXPECT_FALSE(ForwardFft(non_power_of_two, diagnostic));
        EXPECT_NE(diagnostic.find("power of two"), std::string::npos);

        const std::array<std::complex<double>, 2> non_finite{{
            {0.0, 0.0}, {std::numeric_limits<double>::quiet_NaN(), 0.0}}};
        EXPECT_FALSE(InverseFft(non_finite, diagnostic));
        EXPECT_NE(diagnostic.find("finite"), std::string::npos);

        const std::array<std::complex<double>, 2> large{{
            {std::numeric_limits<double>::max(), 0.0},
            {std::numeric_limits<double>::max(), 0.0}}};
        EXPECT_FALSE(ForwardFft(large, diagnostic));
        EXPECT_NE(diagnostic.find("overflowed"), std::string::npos);
    }

    TEST(SignalFftTest, RejectsInputsAboveTheRadix2Limit)
    {
        const std::vector<std::complex<double>> oversized(
            kMaximumRadix2FftSize + 1, {0.0, 0.0});
        std::string diagnostic;
        EXPECT_FALSE(ForwardFft(oversized, diagnostic));
        EXPECT_NE(diagnostic.find("supported size limit"), std::string::npos);
    }

    TEST(SignalSpectrumTest, BuildsRectangularAndPeriodicHannWindows)
    {
        std::string diagnostic;
        const auto rectangular = BuildWindow(WindowKind::Rectangular, 8, diagnostic);
        ASSERT_TRUE(rectangular.has_value()) << diagnostic;
        for (const double coefficient : *rectangular)
        {
            EXPECT_DOUBLE_EQ(coefficient, 1.0);
        }

        const auto hann = BuildWindow(WindowKind::PeriodicHann, 8, diagnostic);
        ASSERT_TRUE(hann.has_value()) << diagnostic;
        EXPECT_NEAR((*hann)[0], 0.0, 1.0e-15);
        EXPECT_NEAR((*hann)[1], 0.5 * (1.0 - std::cos(std::numbers::pi_v<double> / 4.0)),
                    1.0e-15);
        EXPECT_NEAR(std::accumulate(hann->begin(), hann->end(), 0.0), 4.0, 1.0e-14);
        double hann_energy = 0.0;
        for (const double coefficient : *hann)
        {
            hann_energy += coefficient * coefficient;
        }
        EXPECT_NEAR(hann_energy, 3.0, 1.0e-14);

        EXPECT_FALSE(BuildWindow(WindowKind::PeriodicHann, 1, diagnostic));
        EXPECT_FALSE(BuildWindow(WindowKind::Rectangular, 0, diagnostic));
    }

    TEST(SignalSpectrumTest, CoherentGainCorrectsOnBinToneForBothWindows)
    {
        constexpr std::size_t count = 64;
        constexpr std::size_t tone_bin = 7;
        constexpr double sample_rate_hz = 48'000.0;
        constexpr double peak_amplitude = 1.25;
        std::array<double, count> samples{};
        for (std::size_t index = 0; index < count; ++index)
        {
            const double phase = 2.0 * std::numbers::pi_v<double> *
                static_cast<double>(tone_bin * index) / static_cast<double>(count) + 0.37;
            samples[index] = peak_amplitude * std::sin(phase);
        }

        std::string diagnostic;
        for (const WindowKind window : {WindowKind::Rectangular, WindowKind::PeriodicHann})
        {
            const auto spectrum = ComputeSpectrum(samples, sample_rate_hz, window,
                                                  count, diagnostic);
            ASSERT_TRUE(spectrum.has_value()) << diagnostic;
            ASSERT_EQ(spectrum->bins.size(), count / 2 + 1);
            EXPECT_NEAR(spectrum->bins[tone_bin].frequency_hz,
                        sample_rate_hz * static_cast<double>(tone_bin) / count, 1.0e-12);
            EXPECT_NEAR(spectrum->bins[tone_bin].peak_amplitude, peak_amplitude, 1.0e-12);
            EXPECT_NEAR(spectrum->coherent_gain,
                        window == WindowKind::Rectangular ? 1.0 : 0.5, 1.0e-14);
        }
    }

    TEST(SignalSpectrumTest, DoesNotDoubleDcOrNyquistEndpoints)
    {
        constexpr std::size_t count = 64;
        constexpr double sample_rate_hz = 48'000.0;
        constexpr double bin_width_hz = sample_rate_hz / count;
        std::array<double, count> samples{};
        std::string diagnostic;

        samples.fill(2.0);
        const auto dc = ComputeSpectrum(samples, sample_rate_hz,
                                        WindowKind::Rectangular, count, diagnostic);
        ASSERT_TRUE(dc.has_value()) << diagnostic;
        ASSERT_EQ(dc->bins.size(), count / 2 + 1);
        EXPECT_DOUBLE_EQ(dc->bins.front().frequency_hz, 0.0);
        EXPECT_DOUBLE_EQ(dc->bins.front().peak_amplitude, 2.0);
        EXPECT_NEAR(dc->bins.front().power_spectral_density * bin_width_hz, 4.0, 1.0e-12);

        for (std::size_t index = 0; index < count; ++index)
        {
            samples[index] = index % 2 == 0 ? 1.5 : -1.5;
        }
        const auto nyquist = ComputeSpectrum(samples, sample_rate_hz,
                                             WindowKind::Rectangular, count, diagnostic);
        ASSERT_TRUE(nyquist.has_value()) << diagnostic;
        EXPECT_DOUBLE_EQ(nyquist->bins.back().frequency_hz, sample_rate_hz / 2.0);
        EXPECT_NEAR(nyquist->bins.back().peak_amplitude, 1.5, 1.0e-12);
        EXPECT_NEAR(nyquist->bins.back().power_spectral_density * bin_width_hz,
                    1.5 * 1.5, 1.0e-12);
    }

    TEST(SignalSpectrumTest, OneSidedPsdIntegratesToWindowedMeanSquare)
    {
        constexpr std::size_t count = 128;
        constexpr std::size_t fft_size = 256;
        constexpr double sample_rate_hz = 32'000.0;
        std::array<double, count> samples{};
        for (std::size_t index = 0; index < count; ++index)
        {
            samples[index] = std::sin(0.41 * static_cast<double>(index)) +
                0.2 * std::cos(0.13 * static_cast<double>(index)) - 0.1;
        }

        std::string diagnostic;
        for (const WindowKind window : {WindowKind::Rectangular, WindowKind::PeriodicHann})
        {
            const auto coefficients = BuildWindow(window, count, diagnostic);
            ASSERT_TRUE(coefficients.has_value()) << diagnostic;
            double weighted_square_sum = 0.0;
            for (std::size_t index = 0; index < count; ++index)
            {
                const double weighted = samples[index] * (*coefficients)[index];
                weighted_square_sum += weighted * weighted;
            }
            double window_energy = 0.0;
            for (const double coefficient : *coefficients)
            {
                window_energy += coefficient * coefficient;
            }
            const double expected_mean_square = weighted_square_sum / window_energy;

            const auto spectrum = ComputeSpectrum(samples, sample_rate_hz, window,
                                                  fft_size, diagnostic);
            ASSERT_TRUE(spectrum.has_value()) << diagnostic;
            EXPECT_EQ(spectrum->observed_sample_count, count);
            EXPECT_EQ(spectrum->fft_size, fft_size);
            ASSERT_EQ(spectrum->bins.size(), fft_size / 2 + 1);
            double integrated_psd = 0.0;
            for (const SpectrumBin &bin : spectrum->bins)
            {
                integrated_psd += bin.power_spectral_density *
                    (sample_rate_hz / static_cast<double>(fft_size));
            }
            EXPECT_NEAR(integrated_psd, expected_mean_square, 1.0e-12);
        }
    }

    TEST(SignalSpectrumTest, OneVoltToneHasUnitPeakAndHalfVoltSquaredMeanSquare)
    {
        constexpr double sample_rate_hz = 48'000.0;
        constexpr double tone_hz = 1'000.0;
        constexpr double peak_amplitude = 1.0;

        // A one-cycle 48-sample record is the small direct-DFT oracle for the
        // 48 kHz / 1 kHz coherent tone convention.
        constexpr std::size_t oracle_count = 48;
        std::array<std::complex<double>, oracle_count> oracle_input{};
        for (std::size_t index = 0; index < oracle_count; ++index)
        {
            const double time = static_cast<double>(index) / sample_rate_hz;
            oracle_input[index] = {peak_amplitude * std::sin(
                2.0 * std::numbers::pi_v<double> * tone_hz * time), 0.0};
        }
        std::string diagnostic;
        const auto oracle = ForwardDft(oracle_input, diagnostic);
        ASSERT_TRUE(oracle.has_value()) << diagnostic;
        EXPECT_NEAR(2.0 * std::abs((*oracle)[1]) / oracle_count,
                    peak_amplitude, 1.0e-12);
        double oracle_integrated_psd = 0.0;
        const double oracle_bin_width = sample_rate_hz / oracle_count;
        for (std::size_t bin = 0; bin <= oracle_count / 2; ++bin)
        {
            const bool endpoint = bin == 0 || bin == oracle_count / 2;
            const double factor = endpoint ? 1.0 : 2.0;
            const double psd = factor * std::norm((*oracle)[bin]) /
                (sample_rate_hz * static_cast<double>(oracle_count));
            oracle_integrated_psd += psd * oracle_bin_width;
        }
        EXPECT_NEAR(oracle_integrated_psd, 0.5, 1.0e-12);

        // The FFT fixture uses a coherent power-of-two record as required by radix-2.
        constexpr std::size_t fft_count = 256;
        constexpr std::size_t tone_bin = 5;
        std::array<double, fft_count> fft_input{};
        for (std::size_t index = 0; index < fft_count; ++index)
        {
            fft_input[index] = peak_amplitude * std::sin(
                2.0 * std::numbers::pi_v<double> * static_cast<double>(tone_bin * index) /
                static_cast<double>(fft_count));
        }
        const auto fft_spectrum = ComputeSpectrum(fft_input, sample_rate_hz,
            WindowKind::Rectangular, fft_count, diagnostic);
        ASSERT_TRUE(fft_spectrum.has_value()) << diagnostic;
        EXPECT_NEAR(fft_spectrum->bins[tone_bin].peak_amplitude, peak_amplitude, 1.0e-12);
        double fft_integrated_psd = 0.0;
        for (const SpectrumBin &bin : fft_spectrum->bins)
        {
            fft_integrated_psd += bin.power_spectral_density *
                (sample_rate_hz / static_cast<double>(fft_count));
        }
        EXPECT_NEAR(fft_integrated_psd, 0.5, 1.0e-12);
    }

    TEST(SignalSpectrumTest, SevenKilohertzSampledAtEightKilohertzAliasesToOneKilohertz)
    {
        SignalGeneratorConfig config;
        config.sample_rate_hz = 8'000.0;
        config.frequency_hz = 7'000.0;
        std::string diagnostic;
        auto generator = SignalGenerator::Create(config, diagnostic);
        ASSERT_TRUE(generator.has_value()) << diagnostic;
        const auto block = generator->Generate(64, diagnostic);
        ASSERT_TRUE(block.has_value()) << diagnostic;

        const auto spectrum = ComputeSpectrum(block->ChannelSamples(0),
            config.sample_rate_hz, WindowKind::Rectangular, 64, diagnostic);
        ASSERT_TRUE(spectrum.has_value()) << diagnostic;
        ASSERT_EQ(spectrum->bins.size(), 33u);
        EXPECT_NEAR(spectrum->bins[8].frequency_hz, 1'000.0, 1.0e-12);
        EXPECT_NEAR(spectrum->bins[8].peak_amplitude, 1.0, 1.0e-12);
        for (std::size_t bin = 0; bin < spectrum->bins.size(); ++bin)
        {
            if (bin != 8)
            {
                EXPECT_NEAR(spectrum->bins[bin].peak_amplitude, 0.0, 1.0e-12);
            }
        }
    }

    TEST(SignalSpectrumTest, PeriodicHannReducesFarSidelobeLeakageForOffBinTone)
    {
        constexpr std::size_t count = 512;
        constexpr double sample_rate_hz = 48'000.0;
        constexpr double tone_frequency_hz = 7.5 * sample_rate_hz / count;
        std::array<double, count> samples{};
        for (std::size_t index = 0; index < count; ++index)
        {
            samples[index] = std::sin(2.0 * std::numbers::pi_v<double> *
                tone_frequency_hz * static_cast<double>(index) / sample_rate_hz + 0.23);
        }

        std::string diagnostic;
        const auto rectangular = ComputeSpectrum(samples, sample_rate_hz,
            WindowKind::Rectangular, count, diagnostic);
        ASSERT_TRUE(rectangular.has_value()) << diagnostic;
        const auto hann = ComputeSpectrum(samples, sample_rate_hz,
            WindowKind::PeriodicHann, count, diagnostic);
        ASSERT_TRUE(hann.has_value()) << diagnostic;

        const auto leakage_ratio = [](const Spectrum &spectrum)
        {
            const double bin_width = spectrum.sample_rate_hz /
                static_cast<double>(spectrum.fft_size);
            std::size_t peak_bin = 0;
            for (std::size_t bin = 1; bin < spectrum.bins.size(); ++bin)
            {
                if (spectrum.bins[bin].peak_amplitude >
                    spectrum.bins[peak_bin].peak_amplitude)
                {
                    peak_bin = bin;
                }
            }
            double total_energy = 0.0;
            double far_sidelobe_energy = 0.0;
            for (std::size_t bin = 0; bin < spectrum.bins.size(); ++bin)
            {
                const double energy = spectrum.bins[bin].power_spectral_density * bin_width;
                total_energy += energy;
                const std::size_t distance = bin > peak_bin ? bin - peak_bin : peak_bin - bin;
                if (distance > 2)
                {
                    far_sidelobe_energy += energy;
                }
            }
            return far_sidelobe_energy / total_energy;
        };

        const double rectangular_leakage = leakage_ratio(*rectangular);
        const double hann_leakage = leakage_ratio(*hann);
        EXPECT_GT(rectangular_leakage, 1.0e-3);
        EXPECT_LT(hann_leakage, rectangular_leakage * 0.25);
    }

    TEST(SignalSpectrumTest, NaiveSawAndTriangleExposeTheirExpectedHarmonics)
    {
        constexpr double sample_rate_hz = 256'000.0;
        constexpr double fundamental_hz = 1'000.0;
        constexpr std::size_t sample_count = 8'192;
        constexpr std::size_t fundamental_bin = 32;
        std::string diagnostic;

        for (const SignalKind kind : {SignalKind::Sawtooth, SignalKind::Triangle})
        {
            SignalGeneratorConfig config;
            config.kind = kind;
            config.sample_rate_hz = sample_rate_hz;
            config.frequency_hz = fundamental_hz;
            auto generator = SignalGenerator::Create(config, diagnostic);
            ASSERT_TRUE(generator.has_value()) << diagnostic;
            auto block = generator->Generate(sample_count, diagnostic);
            ASSERT_TRUE(block.has_value()) << diagnostic;
            const auto spectrum = ComputeSpectrum(block->ChannelSamples(0),
                sample_rate_hz, WindowKind::Rectangular, sample_count, diagnostic);
            ASSERT_TRUE(spectrum.has_value()) << diagnostic;

            if (kind == SignalKind::Sawtooth)
            {
                EXPECT_NEAR(spectrum->bins[fundamental_bin].peak_amplitude,
                            2.0 / std::numbers::pi_v<double>, 0.002);
                EXPECT_NEAR(spectrum->bins[2 * fundamental_bin].peak_amplitude,
                            1.0 / std::numbers::pi_v<double>, 0.002);
            }
            else
            {
                EXPECT_NEAR(spectrum->bins[fundamental_bin].peak_amplitude,
                            8.0 / (std::numbers::pi_v<double> *
                                   std::numbers::pi_v<double>), 0.002);
                EXPECT_NEAR(spectrum->bins[2 * fundamental_bin].peak_amplitude,
                            0.0, 1.0e-12);
                EXPECT_NEAR(spectrum->bins[3 * fundamental_bin].peak_amplitude,
                            8.0 / (9.0 * std::numbers::pi_v<double> *
                                   std::numbers::pi_v<double>), 0.002);
            }
        }
    }

    TEST(SignalSpectrumTest, NaiveWaveformHarmonicsAliasAboveNyquist)
    {
        constexpr double sample_rate_hz = 16'000.0;
        constexpr std::size_t sample_count = 512;
        const auto get_spectrum = [sample_rate_hz, sample_count](
            const SignalKind kind, const double frequency_hz,
            std::string &diagnostic) -> std::optional<Spectrum>
        {
            SignalGeneratorConfig config;
            config.kind = kind;
            config.sample_rate_hz = sample_rate_hz;
            config.frequency_hz = frequency_hz;
            auto generator = SignalGenerator::Create(config, diagnostic);
            if (!generator.has_value())
            {
                return std::nullopt;
            }
            auto block = generator->Generate(sample_count, diagnostic);
            if (!block.has_value())
            {
                return std::nullopt;
            }
            return ComputeSpectrum(block->ChannelSamples(0), sample_rate_hz,
                WindowKind::Rectangular, sample_count, diagnostic);
        };

        std::string diagnostic;
        const auto saw = get_spectrum(SignalKind::Sawtooth, 7'000.0, diagnostic);
        ASSERT_TRUE(saw.has_value()) << diagnostic;
        EXPECT_NEAR(saw->bins[64].frequency_hz, 2'000.0, 1.0e-12);
        EXPECT_NEAR(saw->bins[160].frequency_hz, 5'000.0, 1.0e-12);
        EXPECT_GT(saw->bins[64].peak_amplitude, 0.1);
        EXPECT_GT(saw->bins[160].peak_amplitude, 0.05);

        const auto triangle = get_spectrum(SignalKind::Triangle, 3'000.0, diagnostic);
        ASSERT_TRUE(triangle.has_value()) << diagnostic;
        EXPECT_NEAR(triangle->bins[32].frequency_hz, 1'000.0, 1.0e-12);
        EXPECT_NEAR(triangle->bins[224].frequency_hz, 7'000.0, 1.0e-12);
        EXPECT_GT(triangle->bins[32].peak_amplitude, 0.005);
        EXPECT_GT(triangle->bins[224].peak_amplitude, 0.01);
    }

    TEST(SignalSpectrumTest, RejectsInvalidInputsAndImplicitPadding)
    {
        const std::array<double, 4> samples{0.0, 1.0, 0.0, -1.0};
        std::string diagnostic;
        EXPECT_FALSE(ComputeSpectrum(samples, 0.0, WindowKind::Rectangular, 4, diagnostic));
        EXPECT_FALSE(ComputeSpectrum(samples, 48'000.0, WindowKind::Rectangular, 2, diagnostic));
        EXPECT_FALSE(ComputeSpectrum(samples, 48'000.0, WindowKind::Rectangular, 6, diagnostic));

        const std::array<double, 4> non_finite{
            0.0, 1.0, std::numeric_limits<double>::quiet_NaN(), -1.0};
        EXPECT_FALSE(ComputeSpectrum(non_finite, 48'000.0,
                                     WindowKind::Rectangular, 4, diagnostic));
    }
}
