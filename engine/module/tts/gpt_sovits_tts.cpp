#include "gpt_sovits_tts.h"
#include "log/logger.h"
#include <nlohmann/json.hpp>
#include <httplib/httplib.h>

namespace kpengine::tts
{
    GPTSovitsTTS::GPTSovitsTTS()
    {
    }

    GPTSovitsTTS::~GPTSovitsTTS()
    {
    }

    bool GPTSovitsTTS::Initialize(const ServerConfig &config)
    {
        config_ = config;
        client_ = std::make_unique<httplib::Client>(config.host, config.port);
        client_->set_connection_timeout(config_.timeout);
        client_->set_read_timeout(config_.timeout);
        initialized_ = true;
        return true;
    }

    void GPTSovitsTTS::ShutDown()
    {
        if (initialized_)
        {
            client_->stop();
        }
        initialized_ = false;
    }

    size_t total = 0;

    std::string GPTSovitsTTS::BuildRequest(const TTSRequest &request) const
    {
        nlohmann::json j;
        j["text"] = request.text;
        j["text_lang"] = request.text_lang;
        j["ref_audio_path"] = request.ref_audio_path;
        j["prompt_lang"] = request.prompt_lang;
        j["prompt_text"] = request.prompt_text;
        j["text_split_method"] = "cut4";
        j["batch_size"] = 1;
        j["streaming_mode"] = request.streaming;
        j["sample_steps"] = 16;
        j["overlap_length"] = 2;
        j["min_chunk_length"] = 16;

        return j.dump();
    }

    bool GPTSovitsTTS::SynthesizeBuffer(const TTSRequest &request, AudioDataCallback OnData, ErrorCallback OnError)
    {
        std::string json_body = BuildRequest(request);

        auto response = client_->Post(
            config_.api_path.c_str(), // "/tts"
            json_body,
            "application/json");

        if (!response)
        {
            OnError(httplib::to_string(response.error()));
            return false;
        }

        if (response->status != 200)
        {
            OnError(httplib::to_string(response.error()));
            return false;
        }

        std::vector<uint8_t> data(
            response->body.begin(),
            response->body.end());

        OnData(reinterpret_cast<const uint8_t*>(response->body.data()), response->body.size());

        return true;
    }

    bool GPTSovitsTTS::SynthesizeStream(const TTSRequest &request, AudioDataCallback OnData, FinishCallback OnFinish, ErrorCallback OnError)
    {
        std::string json_body = BuildRequest(request);

        httplib::Headers headers;


        auto receiver = [&](const char *data, size_t len)
        {

            OnData(reinterpret_cast<const uint8_t *>(data), len);
            return true; 
        };

        auto progress = [&](uint64_t current, uint64_t total)
        {

            return true;
        };
        auto response = client_->Post( config_.api_path, headers, json_body, "application/json", receiver, progress);

        if (response)
        {
            OnFinish();
            return true;
        }
        if (!response)
        {
            OnError(httplib::to_string(response.error()));
            return false;
        }

        if (response->status != 200)
        {
            OnError(httplib::to_string(response.error()));
            return false;
        }

        return false;
    }

}
