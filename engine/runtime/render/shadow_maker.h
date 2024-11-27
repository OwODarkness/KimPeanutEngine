#ifndef KPENGINE_RUNTIME_SHADOW_MAKER_H
#define KPENGINE_RUNTIME_SHADOW_MAKER_H

#include <memory>

namespace kpengine{

    class RenderShader;
    
    class ShadowMaker{

    public:
        ShadowMaker(int width, int height);
        void Initialize();
        unsigned int GetShadowMap() const {return depth_texture_;}
        unsigned int GetShadowBuffer() const{return depth_buffer_;}
        std::shared_ptr<RenderShader> GetShader();
    private:
        unsigned int depth_texture_;
        unsigned int depth_buffer_;

        std::shared_ptr<RenderShader> shader;
    public:
        int width_{1024};
        int height_{1024};
    };
}

#endif
