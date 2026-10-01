#include "gpt_sovits_tts.h"
#include <nlohmann/json.hpp>
#include <httplib/httplib.h>
#include <algorithm>
#include <cctype>

namespace kpengine::tts
{
    namespace
    {
        constexpr size_t kMaxResponseBytes = 32u * 1024u * 1024u;

        bool IsSupportedAudioType(std::string content_type)
        {
            const auto semicolon = content_type.find(';');
            if (semicolon != std::string::npos)
                content_type.resize(semicolon);
            std::transform(content_type.begin(), content_type.end(), content_type.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return content_type == "audio/wav" || content_type == "audio/x-wav" ||
                   content_type == "audio/wave" || content_type == "application/octet-stream";
        }
    }
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
        std::vector<uint8_t> body;
        auto receiver = [&](const char* data, size_t size)
        {
            if (size > kMaxResponseBytes - body.size())
                return false;
            body.insert(body.end(), data, data + size);
            return true;
        };
        auto response = client_->Post(config_.api_path, httplib::Headers{}, json_body,
                                      "application/json", receiver);

        if (!response)
        {
            OnError(httplib::to_string(response.error()));
            return false;
        }

        if (response->status != 200)
        {
            OnError("TTS server returned HTTP " + std::to_string(response->status));
            return false;
        }

        if (!IsSupportedAudioType(response->get_header_value("Content-Type")))
        {
            OnError("TTS server returned an unsupported Content-Type");
            return false;
        }
        if (body.empty())
        {
            OnError("TTS response body is empty");
            return false;
        }
        if (!OnData(body.data(), body.size()))
        {
            OnError("TTS audio consumer rejected the response");
            return false;
        }
        return true;
    }

    bool GPTSovitsTTS::SynthesizeStream(const TTSRequest &request, AudioDataCallback OnData, FinishCallback OnFinish, ErrorCallback OnError)
    {
        std::string json_body = BuildRequest(request);

        httplib::Headers headers;


        std::vector<uint8_t> quarantined;
        auto receiver = [&](const char *data, size_t len)
        {
            if (len > kMaxResponseBytes - quarantined.size())
                return false;
            quarantined.insert(quarantined.end(), data, data + len);
            return true; 
        };

        auto progress = [&](uint64_t current, uint64_t total)
        {

            return true;
        };
        auto response = client_->Post( config_.api_path, headers, json_body, "application/json", receiver, progress);

        if (!response)
        {
            OnError(httplib::to_string(response.error()));
            return false;
        }
        if (response->status != 200)
        {
            OnError("TTS server returned HTTP " + std::to_string(response->status));
            return false;
        }

        if (!IsSupportedAudioType(response->get_header_value("Content-Type")))
        {
            OnError("TTS server returned an unsupported Content-Type");
            return false;
        }

        if (quarantined.empty())
        {
            OnError("TTS server returned an empty audio response");
            return false;
        }

        constexpr size_t kDeliveryChunkBytes = 64u * 1024u;
        for (size_t offset = 0; offset < quarantined.size(); offset += kDeliveryChunkBytes)
        {
            const size_t count = std::min(kDeliveryChunkBytes, quarantined.size() - offset);
            if (!OnData(quarantined.data() + offset, count))
            {
                OnError("TTS audio consumer rejected the response");
                return false;
            }
        }
        OnFinish();
        return true;
    }

}
