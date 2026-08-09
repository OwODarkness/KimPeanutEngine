#include <gtest/gtest.h>
#include "runtime/asset/asset_manager.h"
#include "runtime/audio/audio_system.h"
#include "runtime/audio/buffer_audio_player.h"

using namespace kpengine;

TEST(AudioDecodeTest, HelloWorld) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(true);
    std::cout << "Hello from Audio Decode Test!" << std::endl;
}

TEST(AudioDecodeTest, LoopingPlayback) {
    auto &manager = asset::AssetManager::GetInstance();

    //  auto audio_path = std::string("D:\\dataset\\voice\\kurisu\\voice1\\voice1.wav");
    //  asset::AssetID id = manager.LoadSync(audio_path);
    //  auto resource = manager.GetResource<asset::AudioResource>(id);
    //  ASSERT_NE(resource, nullptr) << "Resource not found after load";
    // auto clip = resource->data;
    // auto duration = clip->GetDuration();
    
    // auto handle = audio_sys_->CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    // audio::BufferAudioPlayer* player = 
    //     dynamic_cast<audio::BufferAudioPlayer*>(audio_sys_->GetAudioPlayer(handle));
    
    // player->SetShouldLoop(true);
    // player->SetClip(clip);
    // player->Play();
    
    // // 验证循环：播放超过一个完整周期
    // EXPECT_TRUE(player->IsPlaying());
    
    // // 等待一个完整循环 + 额外时间
    // std::this_thread::sleep_for(std::chrono::milliseconds(
    //     static_cast<int>(duration + 500)));
    
    // // 验证仍播放中（循环）
    // EXPECT_TRUE(player->IsPlaying());
    
    // // 验证位置已经重置（经历了循环）
    // auto current_pos = player->GetCurrentPosition();
    // EXPECT_LT(current_pos, duration); // 应该在循环内
    
    // player->Stop();
    // EXPECT_FALSE(player->IsPlaying());
}