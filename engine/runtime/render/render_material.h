#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_H

#include <memory>

namespace kpengine{

    class RenderTexture; 
    class RenderMaterial{
        
    public:
        std::shared_ptr<RenderTexture> base_color_;
    };
}

#endif