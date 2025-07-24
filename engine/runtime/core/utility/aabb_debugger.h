#ifndef KPENGINE_RUNTIME_AABB_DEBUGGER_H
#define KEPNGINE_RUNTIME_AABB_DEBUGGER_H

#include <memory>

#include "runtime/render/aabb.h"
#include "runtime/core/math/math_header.h"

namespace kpengine{
class AABBDebugger{
public:
    AABBDebugger() = default;
    void Initialize(const AABB& aabb);
    void Debug(const Matrix4f& mat);
public:
    bool is_visiable_ = true;
private:
    unsigned int vao_;
    unsigned int vbo_;
    unsigned int ebo_;
    std::shared_ptr<class RenderShader> shader_;
};
}

#endif