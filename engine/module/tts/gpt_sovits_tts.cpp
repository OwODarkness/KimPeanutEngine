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
            std::string msg = "HTTP request failed" + httplib::to_string(res.error()) + ".";
            KP_LOG("GPTSOVITSTTSLOG",
                   LOG_LEVEL_ERROR,
                    msg.c_str() );
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

        KP_LOG("GPTSOVITSTTSLOG",
               LOG_LEVEL_INFO,
               "receive data length: %d",
               response_data.size());


               std::ofstream file("tts_debug.wav", std::ios::binary);
file.write((char*)response_data.data(), response_data.size());
file.close();
    ma_engine engine;
    ma_engine_init(NULL, &engine);

    ma_decoder decoder;
    ma_decoder_init_memory(
        response_data.data(),
        response_data.size(),
        NULL,
        &decoder
    );

    ma_sound sound;
    ma_sound_init_from_data_source(
        &engine,
        &decoder,
        0,
        NULL,
        &sound
    );

    ma_sound_start(&sound);

    // TEMP: keep alive so audio can play
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // ma_sound_uninit(&sound);
    // ma_decoder_uninit(&decoder);
    // ma_engine_uninit(&engine);

        AudioClip clip;

        // TODO:
        // parse WAV data in response_data
        // fill clip.sample_rate
        // fill clip.channels
        // fill clip.samples

        return clip;
    }
}