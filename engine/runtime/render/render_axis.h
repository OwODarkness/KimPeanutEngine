#ifndef KPENGINE_RUNTIME_RENDER_AXIS_H
#define KPENGINE_RUNTIME_RENDER_AXIS_H

#include "runtime/core/math/math_header.h"
#include <memory>

namespace kpengine{
    class RenderShader;

    class RenderAxis{
    public:
    void Initialize();
    void SetModelTransform(const Matrix4f& model);
    void Draw();
    private:
        std::shared_ptr<RenderShader> shader_;
        Matrix4f model_mat_;
        unsigned int vao_;
        unsigned int vbo_;
        
    };
}

#endif