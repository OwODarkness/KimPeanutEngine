#include <memory>
#include <iostream>
#include "runtime/engine.h"
#include "runtime/window/glfw_window_system.h"
#include "runtime/input/input_system.h"
#include "runtime/input/input_context.h"
#include "runtime/game_framework/camera.h"
#include "runtime/graphics/backend/vulkan/vulkan_backend.h"

#include "runtime/asset/asset_manager.h"
#include "runtime/core/config/path.h"
#include "runtime/asset/shader_program.h"
#include "runtime/core/resource/resource_pipeline.h"
#include "runtime/core/resource/spirv_compiler.h"
#include "module/tts/tts_system.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "runtime/audio/audio_player.h"
#include "runtime/audio/buffer_audio_player.h"
#include "runtime/asset/miniaudio_audio_loader.h"

#include "script/lua/lua_vm.h"

using namespace kpengine::runtime;
using namespace kpengine;


void tts_test()
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
    std::string target_text = u8"その現象は偶然じゃないと思う。データを見れば、ちゃんと理由があるはずよ。";

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

void audio_test()
{
    std::unique_ptr<WindowSystem> window = WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW);
    WindowCreateInfo window_create_info;
    window_create_info.graphics_api_type = GraphicsAPIType::GRAPHICS_API_VULKAN;
    // window_create_info.graphics_api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
    window_create_info.width = 1600;
    window_create_info.height = 1024;
    window_create_info.title = "Audio";
    window->Initialize(window_create_info);
    using namespace asset;

    auto &manager = AssetManager::GetInstance();

    // audio_paths.push_back("D:\\dataset\\voice\\kurisu\\voice3\\voice3.mp3");
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

    using namespace kpengine::script::lua;
    LuaVM vm;

    vm.Initialize();
    auto pause_func = [player]()
    { player->Pause(); };
    vm.RegisterFunction("pause_audio", pause_func);

    auto resume_func = [player]()
    { player->Play(); };
    vm.RegisterFunction("resume_audio", resume_func);

    auto restart_func = [player]()
    { player->Restart(); };
    vm.RegisterFunction("restart_audio", restart_func);

    std::string script_path = GetScriptDirectory() + "test.lua";
    vm.ExecuteFile(script_path);

    window->key_event_dispatcher_.Bind([&vm](const KeyEvent &event)
                                       {
        if (event.action == 1) {
            vm.CallFunction("on_key_press", event.key);
        } });
    while (!window->ShouldClose())
    {
        if (window_create_info.graphics_api_type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            window->SwapBuffers();
        }
        window->PollEvents();
    }
    window->Cleanup();
}

void lua_test()
{
    using namespace kpengine::script::lua;
    LuaVM vm;

    vm.Initialize();
    auto func = []()
    { audio_test(); };
    vm.RegisterFunction("audio_test", func);

    std::string script_path = GetScriptDirectory() + "test.lua";

    vm.ExecuteFile(script_path);
}

#include "example/audio/audio_example.h"
#include "example/graphics/graphics_example.h"
#include "example/tts/tts_example.h"
#include "example/asset/asset_example.h"
int main(int argc, char **argv)
{

    //rhi_test();
    // renderer_test();
    // foo_test();
    //tts_test();

    // audio_test();
    // lua_test();
    
    //audio_test();
    //example::ExampleAudioPlay();
    //example::RenderExample();
    //example::RHIExample();
    //example::TTSExample();
    //example::TextureLoadSync();
    //example::TextureLoadAsync();
    //example::ModelLoadSync();
    example::ShaderProgramLoad();
    return 0;
}
