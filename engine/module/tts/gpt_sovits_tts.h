#ifndef KPENGINE_MODULE_GPT_SOVITS_TTS_H
#define KPENGINE_MODULE_GPT_SOVITS_TTS_H


#include <memory>
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


        virtual bool SynthesizeBuffer(const TTSRequest& request, AudioDataCallback OnData, ErrorCallback OnError) override;

        virtual bool SynthesizeStream(const TTSRequest& request, AudioDataCallback OnData, FinishCallback OnFinish, ErrorCallback OnError) override;
        

    private:

        std::string BuildRequest(const TTSRequest &request) const;

    private:
        ServerConfig config_;
        std::unique_ptr<httplib::Client> client_;
        bool initialized_ = false;
    };
}

#endif