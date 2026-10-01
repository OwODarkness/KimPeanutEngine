#include "tts_system.h"
#include "asset/miniaudio_audio_loader.h"
#include "log/logger.h"
#include "gpt_sovits_tts.h"
#include "audio/audio_stream_decoder.h"
#include "audio/audio_system.h"
#include "audio/buffer_audio_player.h"
#include "audio/stream_audio_player.h"
#include "audio/audio_stream.h"
namespace
{
    constexpr uint32_t kStreamBufferSeconds = 20;
}

namespace kpengine::tts
{
    namespace
    {
        constexpr size_t kMaxQueuedTtsTasks = 16;
    }
    TTSSystem::TTSSystem():
    audio_loader_(std::make_unique<kpengine::asset::MiniAudio_AudioLoader>())
    {
    }

    bool TTSSystem::Initialize(
        TTSProviderType type,
        const ServerConfig &config)
    {
        if (initialized || worker_.joinable())
        {
            return false;
        }

        switch (type)
        {
        case TTSProviderType::GPT_SOVITS:
            provider_ = std::make_unique<GPTSovitsTTS>();
            break;
        }

        if (!provider_)
        {
            return false;
        }

        if (!provider_->Initialize(config))
        {
            provider_->ShutDown();
            provider_.reset();
            return false;
        }

        {
            std::lock_guard lock(mutex_);
            initialized = true;
            running_ = true;
        }
        try
        {
            worker_ = std::thread(&TTSSystem::WorkerLoop, this);
        }
        catch (...)
        {
            {
                std::lock_guard lock(mutex_);
                initialized = false;
                running_ = false;
            }
            provider_->ShutDown();
            provider_.reset();
            return false;
        }
        return true;
    }

    void TTSSystem::ShutDown()
    {
        {
            std::lock_guard lock(mutex_);
            if (!initialized && !worker_.joinable())
            {
                return;
            }
            initialized = false;
            running_ = false;
        }
        cv_.notify_all();

        // Ask the provider to interrupt an in-flight request before joining
        // the worker, otherwise shutdown can wait for the full HTTP timeout.
        if (provider_)
        {
            provider_->ShutDown();
        }

        if (worker_.joinable())
        {
            worker_.join();
        }
        std::queue<TTSTask> abandoned;
        {
            std::lock_guard lock(mutex_);
            tasks_.swap(abandoned);
        }
        while (!abandoned.empty())
        {
            TTSResult result;
            result.error_code = -6;
            result.error_message = "TTS system shut down before the request started";
            if (abandoned.front().callback)
                abandoned.front().callback(result);
            abandoned.pop();
        }
    }

    void TTSSystem::WorkerLoop()
    {
        while (true)
        {
            TTSTask task;
            {
                std::unique_lock lock(mutex_);

                cv_.wait(lock, [this]
                         { return !running_ || !tasks_.empty(); });

                if (running_ == false)
                {
                    break;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            TTSResult result = SyncSynthesize(task.request);
            if (task.callback)
            {
                task.callback(result);
            }
        }
    }

    TTSResult TTSSystem::SyncSynthesize(const TTSRequest &request)
    {
        TTSResult result;
        if (!initialized || !provider_)
        {
            result.error_code = -1;
            result.error_message = "TTS system is not initialized";
            return result;
        }
        if (!audio_system)
        {
            result.error_code = -2;
            result.error_message = "Audio system is not configured";
            return result;
        }
        if (!audio_system->IsInitialized())
        {
            result.error_code = -2;
            result.error_message = "Audio system is not initialized";
            return result;
        }
        if (request.streaming)
        {
            audio::AudioHandle audio_handle = audio_system->CreateAudioPlayer(audio::AudioPlayerType::Stream);
            auto player = std::dynamic_pointer_cast<audio::StreamAudioPlayer>(audio_system->GetAudioPlayer(audio_handle));
            data::AudioFormat audio_format;
            audio_format.channels = 1;
            audio_format.sample_rate = 48000;
            std::shared_ptr<audio::AudioStream> stream =
                std::make_shared<audio::AudioStream>(audio_format, kStreamBufferSeconds);
            audio::AudioStreamDecoder decoder(stream);
            if (!player)
            {
                audio_system->DestroyAudioPlayer(audio_handle);
                result.error_code = -3;
                result.error_message = "Failed to create streaming audio player";
                return result;
            }
            player->SetStream(stream);
            bool decoder_finished = false;
           
           
            auto on_data_callback = [&](const uint8_t* data, size_t size) -> bool
            {
                if (decoder.Feed(data, size) == audio::AudioDecodeResult::InvalidData)
                {
                    return false;
                }
                player->Play();
                return true;
            };

            auto on_finish_callback = [&](){
                decoder_finished = decoder.Finish();
            };

            auto on_error_callback = [&](const std::string& msg)
            {
                result.error_code = -4;
                result.error_message = msg.substr(0, 512);
            };

            bool succeed = provider_->SynthesizeStream(request, on_data_callback, on_finish_callback, on_error_callback);
            result.success = succeed && decoder_finished;
            if (succeed && !decoder_finished)
            {
                result.error_code = -5;
                result.error_message = "TTS audio ended with an incomplete or invalid WAV stream";
            }
            if (result.success)
            {
                result.player_handle = audio_handle;
            }
            else
            {
                stream->Finish();
                audio_system->DestroyAudioPlayer(audio_handle);
            }
            
        }
        else
        {
            audio::AudioHandle audio_handle = audio_system->CreateAudioPlayer(audio::AudioPlayerType::Buffer);
            auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(audio_system->GetAudioPlayer(audio_handle));
            if (!player)
            {
                audio_system->DestroyAudioPlayer(audio_handle);
                result.error_code = -3;
                result.error_message = "Failed to create buffered audio player";
                return result;
            }
            auto on_data_callback = [&](const uint8_t* data, size_t size) -> bool
            {
                  auto clip = audio_loader_->LoadFromMemory(
                        reinterpret_cast<const char*>(data), size).data;
                  if (!clip || clip->frame_count == 0 || clip->format.channels == 0 ||
                      clip->format.sample_rate == 0 || clip->pcm.empty())
                  {
                      result.error_code = -5;
                      result.error_message = "TTS returned an invalid or empty audio clip";
                      return false;
                  }
                  player->SetClip(std::move(clip));
                  player->Play();
                result.player_handle = audio_handle;
                    return true;
            };

            auto on_error_callback = [&](const std::string& msg)
            {
                result.error_code = -4;
                result.error_message = msg.substr(0, 512);
            };

            bool succeed = provider_->SynthesizeBuffer(request, on_data_callback, on_error_callback);
            result.success = succeed && result.player_handle.IsValid();
            if (!result.success)
            {
                audio_system->DestroyAudioPlayer(audio_handle);
                if (result.error_message.empty())
                {
                    result.error_code = -4;
                    result.error_message = "TTS provider returned no playable audio";
                }
            }
        }
        return result;
    }

    void TTSSystem::AsyncSynthesize(
        const TTSRequest &request,
        std::function<void(const TTSResult &)> callback)
    {
        bool rejected = false;
        {
            std::lock_guard lock(mutex_);
            if (!callback)
            {
                return;
            }
            if (!initialized || !running_ || tasks_.size() >= kMaxQueuedTtsTasks)
                rejected = true;
            else
                tasks_.push({request, callback});
        }

        if (rejected)
        {
            TTSResult result;
            result.error_code = -7;
            result.error_message = "TTS system is unavailable or its request queue is full";
            callback(result);
            return;
        }

        cv_.notify_one();
    }

    TTSSystem::~TTSSystem()
    {
        ShutDown();
    }
}
