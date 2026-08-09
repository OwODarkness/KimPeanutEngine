#include "audio_example.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "runtime/audio/audio_player.h"
#include "runtime/audio/buffer_audio_player.h"
#include "runtime/asset/miniaudio_audio_loader.h"

namespace kpengine::example
{

    void ExampleAudioPlay()
    {
        using namespace asset;

        auto &manager = AssetManager::GetInstance();

        auto audio_path = std::string("D:\\dataset\\voice\\kurisu\\voice1\\voice1.wav");

        audio::MiniAudioSystem sys;

        asset::AssetID id = manager.LoadSync(audio_path);

        auto resource = manager.GetResource<asset::AudioResource>(id);

        auto clip = resource->data;

        audio::MiniAudioSystem audio_sys;
        audio_sys.Initialize();
        auto handle = audio_sys.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
        audio::BufferAudioPlayer *player = dynamic_cast<audio::BufferAudioPlayer *>(audio_sys.GetAudioPlayer(handle));
        player->SetShouldLoop(true);
        player->SetClip(clip);
        player->Play();

        sys.Initialize();
        while (1)
        {
            ;
        }
    }

}