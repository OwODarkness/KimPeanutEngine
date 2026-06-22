#include <vector>
#include <httplib/httplib.h>
#include <nlohmann/json.hpp>
#include <miniaudio/miniaudio.h>
#include "gpt_sovits_tts.h"
#include "log/logger.h"

namespace kpengine::tts
{

    void GPTSovitsTTS::LoadConfig(const GPTSovitsConfig &config)
    {
        config_ = config;
    }

    AudioClip GPTSovitsTTS::Synthesis(const std::string &text)
    {
        using json = nlohmann::json;

        httplib::Client client(config_.host, config_.port);

        client.set_connection_timeout(config_.timeout);
        client.set_read_timeout(config_.timeout);

        json j;
        j["text"] = text;
        j["text_lang"] = "ja";
        j["ref_audio_path"] = config_.ref_audio_path;
        j["prompt_lang"] = config_.prompt_lang;
        j["prompt_text"] = config_.prompt_text;
        j["text_split_method"] = "cut4";
        j["batch_size"] = 1;
        j["streaming_mode"] = false;

        std::string json_body = j.dump();

        auto res = client.Post(
            config_.api_path.c_str(), // "/tts"
            json_body,
            "application/json");

        if (!res)
        {
            std::string msg = httplib::to_string(res.error()) + ".";
            KP_LOG("GPTSOVITSTTSLOG",
                   LOG_LEVEL_ERROR,
                   msg.c_str());
            return {};
        }

        if (res->status != 200)
        {
            KP_LOG("GPTSOVITSTTSLOG",
                   LOG_LEVEL_ERROR,
                   "HTTP status: %d",
                   res->status);
            return {};
        }

        std::vector<uint8_t> response_data(
            res->body.begin(),
            res->body.end());

        std::ofstream file("tts_debug.wav", std::ios::binary);
        file.write((char *)response_data.data(), response_data.size());
        file.close();

        ma_decoder decoder;

        ma_decoder_config config =
            ma_decoder_config_init(ma_format_f32, 1, 0);

        if (ma_decoder_init_memory(
                response_data.data(),
                response_data.size(),
                &config,
                &decoder) != MA_SUCCESS)
        {
            return {};
        }

        printf("%d", decoder.outputFormat);
        ma_uint32 channels = decoder.outputChannels;
        ma_uint32 sampleRate = decoder.outputSampleRate;

        ma_uint64 totalFrames = 0;

        if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS)
        {
            ma_decoder_uninit(&decoder);
            return {};
        }

        // IMPORTANT: allocate max possible buffer
        std::vector<float> pcm(totalFrames * channels);

        ma_uint64 framesRead = 0;

        ma_result result = ma_decoder_read_pcm_frames(
            &decoder,
            pcm.data(),
            totalFrames,
            &framesRead);

        ma_decoder_uninit(&decoder);

        if (result != MA_SUCCESS || framesRead == 0)
        {
            return {};
        }

        // CRITICAL FIX: resize to actual decoded size
        pcm.resize(framesRead * channels);

        AudioClip clip;
        clip.pcm = std::move(pcm);
        clip.channels = channels;
        clip.sample_rate = sampleRate;
        clip.frame_count = framesRead;

        return clip;
    }
}