#include "scene_system.h"
#include "runtime/render/frame_buffer.h"
namespace kpengine{
    void SceneSystem::Initialize()
    {
        scene_ = std::make_shared<FrameBuffer>(1280, 720);
        scene_->Initialize();
    }


}