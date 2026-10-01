#include "audio/buffer_audio_player.h"
#include "audio/miniaudio_audio_system.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

int main()
{
    using namespace kpengine;
    using namespace std::chrono_literals;

    audio::MiniAudioSystem system;
    if (!system.Initialize())
    {
        std::cerr << "Audio device initialization failed.\n";
        return 1;
    }

    const audio::AudioDeviceInfo device = system.GetDeviceInfo();
    std::cout << "Device: " << device.name << ", " << device.sample_rate << " Hz, "
              << device.channels << " channels, " << device.period_size_frames << " frames x "
              << device.period_count << " periods, estimated latency "
              << device.estimated_latency_ms << " ms\n";

    const audio::AudioHandle handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(system.GetAudioPlayer(handle));
    if (!handle.IsValid() || !player)
    {
        system.ShutDown();
        return 2;
    }

    auto clip = std::make_shared<data::AudioClip>();
    clip->format.channels = 1;
    clip->format.sample_rate = 48000;
    clip->frame_count = 24000;
    clip->pcm.resize(clip->frame_count);
    for (uint32_t frame = 0; frame < clip->frame_count; ++frame)
    {
        const float time = static_cast<float>(frame) / clip->format.sample_rate;
        clip->pcm[frame] = 0.12f * std::sin(2.0f * 3.14159265f * 440.0f * time);
    }
    player->SetClip(clip);
    player->Play();

    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (player->GetCurrentState() != audio::AudioState::Finished &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);

    const auto played_frames = player->GetPlayedFrameCursor();
    const auto telemetry = system.GetTelemetrySnapshot();
    const bool drained = player->GetCurrentState() == audio::AudioState::Finished &&
                         played_frames == clip->frame_count;
    std::cout << "Playback: " << played_frames << "/" << clip->frame_count
              << " frames; callbacks=" << telemetry.callback_count
              << ", max callback=" << telemetry.max_callback_duration_ns << " ns\n";

    const bool destroyed = system.DestroyAudioPlayer(handle);
    system.ShutDown();
    system.ShutDown();
    if (!destroyed || system.IsInitialized() || system.GetDeviceInfo().initialized)
    {
        std::cerr << "Audio device did not shut down cleanly.\n";
        return 3;
    }
    std::cout << "Device stopped and uninitialized cleanly.\n";
    return drained ? 0 : 4;
}
