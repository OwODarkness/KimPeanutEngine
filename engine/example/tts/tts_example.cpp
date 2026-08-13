#include "module/tts/tts_system.h"
#include "runtime/audio/miniaudio_audio_system.h"

namespace kpengine::example
{
    void TTSExample()
    {
        using namespace tts;

        ServerConfig config;
        config.host = "127.0.0.1";
        config.port = 9880;
        config.api_path = "/tts";
        config.timeout = 180;

        std::unique_ptr<TTSSystem> tts = std::make_unique<TTSSystem>();
        tts->Initialize(TTSProviderType::GPT_SOVITS, config);

        std::string prompt_text = u8"極端な管理社会全体主義まゆりがバナナを食べたいと思っても、今日がバナナを食べていい日でなければ食べることは許さ。";
        std::string ref_audio_path = "D:\\dataset\\voice\\kurisu\\voice1\\voice1.wav";
        std::string target_text = u8"あ、あの…！ ち、違うからね、別に私が言いたくて言ったわけじゃ…！ …でも、その…す、好き…なの。…もう！ 聞こえたでしょ！ 二回は言わないからね、バカ！";
        audio::MiniAudioSystem audio_sys;
        audio_sys.Initialize();
        tts->audio_system = &audio_sys;

        TTSRequest request;
        request.prompt_lang = "ja";
        request.prompt_text = prompt_text;
        request.ref_audio_path = ref_audio_path;
        request.text = target_text;
        request.text_lang = "ja";
        request.streaming = true;

        // tts->AsyncSynthesize(request, [&loader, player](const TTSResult & result){
        //     std::vector<uint8_t> raw_data = result.bytes;
        //     auto clip = loader.LoadFromMemory((char *)raw_data.data(), raw_data.size()).data;
        //     player->SetClip(clip);
        //     player->Play();
        // });

        TTSResult result = tts->SyncSynthesize(request);

        while (1)
        {
            ;
        }
    }
}