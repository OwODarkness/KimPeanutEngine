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

    std::vector<uint8_t> GPTSovitsTTS::Synthesis(const std::string &text)
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
        j["sample_steps"] = 48;

        std::string json_body = j.dump();



        auto res = client.Post(
            config_.api_path.c_str(), // "/tts"
            json_body,
            "application/json");

        if (!res)
        {
            std::string msg = httplib::to_string(res.error()) + "." ;
            KP_LOG("GPTSOVITSTTSLOG",
                   LOG_LEVEL_ERROR,
                   msg.c_str());
            return {};
        }

        if (res->status != 200)
        {
            KP_LOG("GPTSOVITSTTSLOG",
                   LOG_LEVEL_ERROR,
                   "HTTP status: %d %s",
                   res->status, json_body.c_str());
            return {};
        }

        std::vector<uint8_t> response_data(
            res->body.begin(),
            res->body.end());

        std::ofstream file("tts_debug.wav", std::ios::binary);
        file.write((char *)response_data.data(), response_data.size());
        file.close();

       return response_data;
    }
}