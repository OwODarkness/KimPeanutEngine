#include "tts_system.h"
#include "asset/miniaudio_audio_loader.h"
#include "log/logger.h"
#include "gpt_sovits_tts.h"
#include "audio/audio_stream_decoder.h"
#include "audio/audio_system.h"
#include "audio/buffer_audio_player.h"
#include "audio/stream_audio_player.h"
#include "audio/audio_stream.h"
namespace kpengine::tts
{
    TTSSystem::TTSSystem():
    audio_loader_(std::make_unique<kpengine::asset::MiniAudio_AudioLoader>())
    {
    }

    bool TTSSystem::Initialize(
        TTSProviderType type,
        const ServerConfig &config)
    {
        if (initialized)
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

        initialized = true;
        running_ = true;
        worker_ = std::thread(&TTSSystem::WorkerLoop, this);
        return provider_->Initialize(config);
    }

    void TTSSystem::ShutDown()
    {
        if (initialized == false)
        {
            return;
        }
        initialized = false;

        {
            std::lock_guard lock(mutex_);
            running_ = false;
        }
        cv_.notify_one();

        if (worker_.joinable())
        {
            worker_.join();
        }
        if (provider_)
        {
            provider_->ShutDown();
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

                if (running_ == false && tasks_.empty())
                {
                    break;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            TTSResult result = SyncSynthesize(task.request);
            task.callback(result);
        }
    }

    TTSResult TTSSystem::SyncSynthesize(const TTSRequest &request)
    {
        TTSResult result;
        if (request.streaming)
        {
            audio::AudioHandle audio_handle = audio_system->CreateAudioPlayer(audio::AudioPlayerType::Stream);
            audio::StreamAudioPlayer* player = dynamic_cast<audio::StreamAudioPlayer *>(audio_system->GetAudioPlayer(audio_handle));
            data::AudioFormat audio_format;
            audio_format.channels = 1;
            audio_format.sample_rate = 48000;
            std::shared_ptr<audio::AudioStream> stream = std::make_shared<audio::AudioStream>(audio_format);
            audio::AudioStreamDecoder decoder(stream);
            player->SetStream(stream);
           
           
            auto on_data_callback = [&](const uint8_t* data, size_t size) -> bool
            {
                decoder.Feed(data, size);
                player->Play();
                return true;
            };

            auto on_finish_callback = [&](){
                decoder.Finish();
                KP_LOG("LogTTSSystem", LOG_LEVEL_DEBUG, "Finish");
            };

            auto on_error_callback = [&](const std::string& msg)
            {
                KP_LOG("LogTTSSystem", LOG_LEVEL_ERROR, msg);
            };

            bool succeed = provider_->SynthesizeStream(request, on_data_callback, on_finish_callback, on_error_callback);
            result.success = succeed;
            result.player_handle = audio_handle;
            
        }
        else
        {
            audio::AudioHandle audio_handle = audio_system->CreateAudioPlayer(audio::AudioPlayerType::Buffer);
            audio::BufferAudioPlayer *player = dynamic_cast<audio::BufferAudioPlayer *>(audio_system->GetAudioPlayer(audio_handle));
            auto on_data_callback = [&](const uint8_t* data, size_t size) -> bool
            {
                  player->SetClip(audio_loader_->LoadFromMemory(
                        reinterpret_cast<const char*>(data),
                        size).data);
                    player->Play();
                result.player_handle = audio_handle;
                    return true;
            };

            auto on_error_callback = [&](const std::string& msg)
            {
                KP_LOG("LogTTSSystem", LOG_LEVEL_ERROR, msg);
            };

            bool succeed = provider_->SynthesizeBuffer(request, on_data_callback, on_error_callback);
            result.success = succeed;
        }
        return result;
    }

    void TTSSystem::AsyncSynthesize(
        const TTSRequest &request,
        std::function<void(const TTSResult &)> callback)
    {
        {
            std::lock_guard lock(mutex_);

            tasks_.push({request,
                         std::move(callback)});
        }

        cv_.notify_one();
    }

    TTSSystem::~TTSSystem()
    {
    }
}