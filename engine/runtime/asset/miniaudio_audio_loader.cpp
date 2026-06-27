#include "miniaudio_audio_loader.h"
#include <magic_enum/magic_enum.hpp>
#include <miniaudio/miniaudio.h>
#include "log/logger.h"
#include "utility.h"

namespace kpengine::asset
{

    AudioResource MiniAudio_AudioLoader::LoadFromMemory(char *src, size_t size)
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
            return {};
        }
        ma_uint32 channels = decoder.outputChannels;
        ma_uint32 sample_rate = decoder.outputSampleRate;

        ma_uint64 total_frames = 0;

        if (ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames) != MA_SUCCESS)
        {
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
            return {};
        }

        pcm.resize(frames_read * channels);

        std::shared_ptr<AudioClip> clip = std::make_shared<AudioClip>();
        clip->pcm = std::move(pcm);
        clip->channels = channels;
        clip->sample_rate = sample_rate;
        clip->frame_count = frames_read;

        AudioResource res;
        res.resource = clip;
        return res;
    }
    MiniAudio_AudioLoader::MiniAudio_AudioLoader()
    {
    }

    bool MiniAudio_AudioLoader::LoadFromFile(const std::string &path, AssetRegisterInfo &info)
    {
        ma_decoder decoder;

        ma_decoder_config config =
            ma_decoder_config_init(ma_format_f32, 2, 48000);

        if(ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS)
        {
            return false;
        }
 
        ma_uint32 channels = decoder.outputChannels;
        ma_uint32 sample_rate = decoder.outputSampleRate;

        ma_uint64 total_frames = 0;

        if (ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames) != MA_SUCCESS)
        {
            ma_decoder_uninit(&decoder);
            return false;
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
            return false;
        }

        pcm.resize(frames_read * channels);

        std::shared_ptr<AudioClip> clip = std::make_shared<AudioClip>();
        clip->pcm = std::move(pcm);
        clip->channels = channels;
        clip->sample_rate = sample_rate;
        clip->frame_count = frames_read;

        std::shared_ptr<AudioResource> wrapper= std::make_shared<AudioResource>();
        wrapper->resource = clip;
        info.resource =  wrapper;
        info.type = AssetType::KPAT_Audio;
        info.path = path;
        info.name = std::string(magic_enum::enum_name(info.type)) + ExtractNameFromPath(path);
        return true;
    }

    MiniAudio_AudioLoader::~MiniAudio_AudioLoader()
    {
    }
}