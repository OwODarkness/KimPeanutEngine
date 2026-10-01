#include "gpt_sovits_tts.h"
#include <nlohmann/json.hpp>
#include <httplib/httplib.h>
#include <algorithm>
#include <cctype>

namespace kpengine::tts
{
    namespace
    {
        template <typename Callback>
        class ScopeExit
        {
        public:
            explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
            ~ScopeExit() { callback_(); }
            ScopeExit(const ScopeExit&) = delete;
            ScopeExit& operator=(const ScopeExit&) = delete;

        private:
            Callback callback_;
        };

        template <typename Callback>
        ScopeExit(Callback) -> ScopeExit<Callback>;

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
        if (config.host.empty() || config.port == 0 || config.api_path.empty() || config.timeout == 0)
            return false;
        config_ = config;
        initialized_ = true;
        return true;
    }

    void GPTSovitsTTS::ShutDown()
    {
        std::shared_ptr<httplib::Client> client;
        {
            std::lock_guard lock(active_mutex_);
            initialized_ = false;
            client = active_client_;
        }
        if (client)
            client->stop();
    }

    void GPTSovitsTTS::Cancel(JobToken job)
    {
        std::shared_ptr<httplib::Client> client;
        {
            std::lock_guard lock(active_mutex_);
            if (active_job_ == job)
                client = active_client_;
        }
        if (client)
            client->stop();
    }

    std::shared_ptr<httplib::Client> GPTSovitsTTS::BeginRequest(JobToken job)
    {
        std::lock_guard lock(active_mutex_);
        if (!initialized_ || active_client_)
            return {};
        auto client = std::make_shared<httplib::Client>(config_.host, config_.port);
        client->set_connection_timeout(config_.timeout);
        client->set_read_timeout(config_.timeout);
        active_job_ = job;
        active_client_ = client;
        return client;
    }

    void GPTSovitsTTS::EndRequest(JobToken job, const std::shared_ptr<httplib::Client>& client)
    {
        std::lock_guard lock(active_mutex_);
        if (active_job_ == job && active_client_ == client)
        {
            active_job_ = {};
            active_client_.reset();
        }
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

    bool GPTSovitsTTS::SynthesizeBuffer(JobToken job, const TTSRequest &request,
                                       AudioDataCallback OnData, ErrorCallback OnError,
                                       CancellationCheck is_cancelled)
    {
        auto client = BeginRequest(job);
        if (!client)
        {
            OnError("TTS provider is unavailable or busy");
            return false;
        }
        const ScopeExit cleanup([this, job, client] { EndRequest(job, client); });
        std::string json_body = BuildRequest(request);
        std::vector<uint8_t> body;
        auto receiver = [&](const char* data, size_t size)
        {
            if (is_cancelled())
                return false;
            if (size > kMaxResponseBytes - body.size())
                return false;
            body.insert(body.end(), data, data + size);
            return true;
        };
        auto response = client->Post(config_.api_path, httplib::Headers{}, json_body,
                                      "application/json", receiver);

        if (is_cancelled())
            return false;

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

    bool GPTSovitsTTS::SynthesizeStream(JobToken job, const TTSRequest &request,
                                       AudioDataCallback OnData, FinishCallback OnFinish,
                                       ErrorCallback OnError, CancellationCheck is_cancelled)
    {
        auto client = BeginRequest(job);
        if (!client)
        {
            OnError("TTS provider is unavailable or busy");
            return false;
        }
        const ScopeExit cleanup([this, job, client] { EndRequest(job, client); });
        std::string json_body = BuildRequest(request);

        httplib::Request http_request;
        http_request.method = "POST";
        http_request.path = config_.api_path;
        http_request.body = std::move(json_body);
        http_request.headers.emplace("Content-Type", "application/json");
        std::string callback_error;
        uint64_t received_bytes = 0;
        http_request.response_handler = [&](const httplib::Response& response) {
            if (is_cancelled())
                return false;
            if (response.status != 200)
            {
                callback_error = "TTS server returned HTTP " + std::to_string(response.status);
                return false;
            }
            if (!IsSupportedAudioType(response.get_header_value("Content-Type")))
            {
                callback_error = "TTS server returned an unsupported Content-Type";
                return false;
            }
            return true;
        };
        http_request.content_receiver = [&](const char* data, size_t size,
                                            size_t offset, size_t total) {
            (void)offset;
            (void)total;
            if (is_cancelled())
                return false;
            if (size > kMaxResponseBytes - received_bytes)
            {
                callback_error = "TTS response exceeded the 32 MiB limit";
                return false;
            }
            received_bytes += size;
            if (!OnData(reinterpret_cast<const uint8_t*>(data), size))
            {
                callback_error = "TTS audio consumer rejected the response";
                return false;
            }
            return true;
        };
        auto response = client->send(http_request);

        if (is_cancelled())
            return false;

        if (!response)
        {
            OnError(callback_error.empty() ? httplib::to_string(response.error()) : callback_error);
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

        if (received_bytes == 0)
        {
            OnError("TTS server returned an empty audio response");
            return false;
        }
        OnFinish();
        return true;
    }

}
