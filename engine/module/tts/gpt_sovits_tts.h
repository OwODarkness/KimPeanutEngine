#ifndef KPENGINE_MODULE_GPT_SOVITS_TTS_H
#define KPENGINE_MODULE_GPT_SOVITS_TTS_H


#include <memory>
#include <mutex>
#include "tts_provider.h"

namespace httplib{
    class Client;
}

namespace kpengine::tts
{

    class GPTSovitsTTS : public ITTSProvider
    {
    public:
        GPTSovitsTTS();
        virtual ~GPTSovitsTTS();
        virtual bool Initialize(const ServerConfig &config) override;
        virtual void ShutDown() override;


        void Cancel(JobToken job) override;
        bool SynthesizeBuffer(JobToken job, const TTSRequest& request, AudioDataCallback OnData,
                              ErrorCallback OnError, CancellationCheck is_cancelled) override;

        bool SynthesizeStream(JobToken job, const TTSRequest& request, AudioDataCallback OnData,
                              FinishCallback OnFinish, ErrorCallback OnError,
                              CancellationCheck is_cancelled) override;
        

    private:

        std::string BuildRequest(const TTSRequest &request) const;
        std::shared_ptr<httplib::Client> BeginRequest(JobToken job);
        void EndRequest(JobToken job, const std::shared_ptr<httplib::Client>& client);

    private:
        ServerConfig config_;
        std::mutex active_mutex_;
        JobToken active_job_{};
        std::shared_ptr<httplib::Client> active_client_;
        bool initialized_ = false;
    };
}

#endif
