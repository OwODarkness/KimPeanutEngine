#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "signal/sample_block.h"
#include "signal/signal_statistics.h"
#include "signal/signal_generator.h"
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
    }

    TEST(SignalStatisticsTest, RejectsEmptyAndNonFiniteInputs)
    {
        std::string diagnostic;
        EXPECT_FALSE(Measure({}, diagnostic));
        const std::array<double, 2> invalid{1.0, std::numeric_limits<double>::quiet_NaN()};
        EXPECT_FALSE(Measure(invalid, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }
}
