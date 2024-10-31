#ifndef RUNTIME_SCENE_SYSTEM_H
#define RUNTIME_SCENE_SYSTEM_H

#include <memory>

namespace kpengine{
    class FrameBuffer;
    
    class SceneSystem{
    public:
        SceneSystem() = default;


        void Initialize();

    public:
        std::shared_ptr<FrameBuffer> scene_;
    };
}

#endif