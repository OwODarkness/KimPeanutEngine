#ifndef KPENGINE_RUNTIME_OUTLINE_EFFECT_H
#define KPENGINE_RUNTIME_OUTLINE_EFFECT_H

#include <memory>
#include "postprocess_effect.h"
#include "runtime/core/math/math_header.h"
namespace kpengine{
    class RenderShader;

    class OutlineEffect: public PostProcessEffect{
    public:
        OutlineEffect();
        void Initialize() override;
        void Execute(const PostProcessContext& context) override;
    private:
        unsigned int vao_;
        unsigned int vbo_;
        Vector3f border_color_;
        std::shared_ptr<RenderShader> shader_;
    };
}

#endif