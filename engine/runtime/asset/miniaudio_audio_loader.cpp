#include "miniaudio_audio_loader.h"
#include <magic_enum/magic_enum.hpp>
#include <miniaudio/miniaudio.h>
#include "log/logger.h"
#include "utility.h"

namespace kpengine::asset
{
    static const char *LogName = "MiniAudio_AudioLoader";

    // Holds the reusable file decoder so miniaudio never leaks into the header.
    struct MiniAudio_AudioLoader::Impl
    {
        Impl() : config(ma_decoder_config_init(ma_format_f32, 2, 48000)) {}

        ~Impl()
        {
            if (decoder_initialized)
            {
                ma_decoder_uninit(&decoder);
            }
        }

        ma_decoder decoder{};
        ma_decoder_config config;
        std::string path;                 // path the open decoder is bound to
        bool decoder_initialized = false;
    };

    AudioResource MiniAudio_AudioLoader::LoadFromMemory(const char *src, size_t size)
    {
        ma_decoder decoder;

        ma_decoder_config config =
            ma_decoder_config_init(ma_format_f32, 2, 48000);

        if (ma_decoder_init_memory(
                src,
                size,
                &config,
                &decoder) != MA_SUCCESS)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "Failed to init decoder");
            return {};
        }
        ma_uint32 channels = decoder.outputChannels;
        ma_uint32 sample_rate = decoder.outputSampleRate;


        ma_uint64 total_frames = 0;

        if (ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames) != MA_SUCCESS)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "Failed to get length from decoder");
            ma_decoder_uninit(&decoder);
            return {};
        }


        std::vector<float> pcm(total_frames * channels);

        ma_uint64 frames_read = 0;

        ma_result result = ma_decoder_read_pcm_frames(
            &decoder,
            pcm.data(),
            total_frames,
            &frames_read);

        ma_decoder_uninit(&decoder);

        if (result != MA_SUCCESS || frames_read == 0)
        {
            KP_LOG(
                LogName,
                LOG_LEVEL_ERROR,
                "Failed to read audio: %s",
                ma_result_description(result));
            return {};
        }

        pcm.resize(frames_read * channels);

        std::shared_ptr<AudioClip> clip = std::make_shared<AudioClip>();
        clip->pcm = std::move(pcm);
        clip->format.channels = channels;
        clip->format.sample_rate = sample_rate;
        clip->frame_count = frames_read;

        AudioResource res;
        res.data = clip;
        return res;
    }
    MiniAudio_AudioLoader::MiniAudio_AudioLoader() : impl_(std::make_unique<Impl>())
    {
    }

    bool MiniAudio_AudioLoader::LoadFromFile(const std::string &path, AssetRegisterInfo &info)
    {
        Impl &impl = *impl_;

        // Reuse the open decoder for the same file; only re-init when the file changes.
        if (impl.path != path)
        {
            if (impl.decoder_initialized)
            {
                ma_decoder_uninit(&impl.decoder);
                impl.decoder_initialized = false;
            }

            if (ma_decoder_init_file(path.c_str(), &impl.config, &impl.decoder) != MA_SUCCESS)
            {
                impl.path.clear();
                KP_LOG(LogName, LOG_LEVEL_ERROR, "Failed to init decoder for %s", path.c_str());
                return false;
            }

            impl.path = path;
            impl.decoder_initialized = true;
        }
        else
        {
            // Same file again: rewind and re-decode instead of rebuilding the chain.
            if (ma_decoder_seek_to_pcm_frame(&impl.decoder, 0) != MA_SUCCESS)
            {
                KP_LOG(LogName, LOG_LEVEL_ERROR, "Failed to rewind decoder for %s", path.c_str());
                return false;
            }
        }

        ma_uint32 channels = impl.decoder.outputChannels;
        ma_uint32 sample_rate = impl.decoder.outputSampleRate;

        ma_uint64 total_frames = 0;

        if (ma_decoder_get_length_in_pcm_frames(&impl.decoder, &total_frames) != MA_SUCCESS)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "Failed to get length from decoder");
            return false;
        }

        std::vector<float> pcm(total_frames * channels);

        ma_uint64 frames_read = 0;

        ma_result result = ma_decoder_read_pcm_frames(
            &impl.decoder,
            pcm.data(),
            total_frames,
            &frames_read);

        if (result != MA_SUCCESS || frames_read == 0)
        {
            KP_LOG(LogName, LOG_LEVEL_ERROR, "Failed to read audio: %s", ma_result_description(result));
            return false;
        }

        pcm.resize(frames_read * channels);

        std::shared_ptr<AudioClip> clip = std::make_shared<AudioClip>();
        clip->pcm = std::move(pcm);
        clip->format.channels = channels;
        clip->format.sample_rate = sample_rate;
        clip->frame_count = frames_read;

        std::shared_ptr<AudioResource> wrapper = std::make_shared<AudioResource>();
        wrapper->data = clip;
        info.resource = wrapper;
        info.type = AssetType::KPAT_Audio;
        info.path = path;
        info.name = std::string(magic_enum::enum_name(info.type)) + ExtractNameFromPath(path);
        return true;
    }

    MiniAudio_AudioLoader::~MiniAudio_AudioLoader()
    {
    }
}