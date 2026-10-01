#include "module/tts/tts_system.h"
#include "runtime/audio/audio_player.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include <chrono>
#include <thread>

namespace kpengine::example
{
    bool TTSExample(const std::string& reference_audio_path)
    {
        using namespace tts;

        ServerConfig config;
        config.host = "127.0.0.1";
        config.port = 9880;
        config.api_path = "/tts";
        config.timeout = 180;

        std::unique_ptr<TTSSystem> tts = std::make_unique<TTSSystem>();
        if (!tts->Initialize(TTSProviderType::GPT_SOVITS, config))
            return false;

        std::string prompt_text(reinterpret_cast<const char *>(u8"極端な管理社会全体主義まゆりがバナナを食べたいと思っても、今日がバナナを食べていい日でなければ食べることは許さ。"));
        std::string target_text(reinterpret_cast<const char *>(u8"あ、あの…！ ち、違うからね、別に私が言いたくて言ったわけじゃ…！ …でも、その…す、好き…なの。…もう！ 聞こえたでしょ！ 二回は言わないからね、バカ！"));
        audio::MiniAudioSystem audio_sys;
        if (!audio_sys.Initialize())
        {
            tts->ShutDown();
            return false;
        }
        tts->audio_system = &audio_sys;

        TTSRequest request;
        request.prompt_lang = "ja";
        request.prompt_text = prompt_text;
        request.ref_audio_path = reference_audio_path;
        request.text = target_text;
        request.text_lang = "ja";
        request.streaming = true;

        // tts->AsyncSynthesize(request, [&loader, player](const TTSResult & result){
        //     std::vector<uint8_t> raw_data = result.bytes;
        //     auto clip = loader.LoadFromMemory((char *)raw_data.data(), raw_data.size()).data;
        //     player->SetClip(clip);
        //     player->Play();
        // });

        const TTSResult result = tts->SyncSynthesize(request);
        if (!result.success)
        {
            tts->ShutDown();
            return false;
        }

        const auto player = audio_sys.GetAudioPlayer(result.player_handle);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
        while (player && player->GetCurrentState() != audio::AudioState::Finished &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const bool drained = player && player->GetCurrentState() == audio::AudioState::Finished;
        tts->ShutDown();
        audio_sys.DestroyAudioPlayer(result.player_handle);
        return drained;
    }
}
