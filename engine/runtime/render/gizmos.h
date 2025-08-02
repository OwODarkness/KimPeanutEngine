#ifndef KPENGINE_RUNTIME_GIZMOS_H
#define KPENGINE_RUNTIME_GIZMOS_H

#include <memory>
#include "runtime/core/math/math_header.h"

namespace kpengine{
    class RenderShader;
    class Actor;
    class Gizmos{
    public:
    void Initialize();
    void SetActor(std::shared_ptr<Actor> actor);
    bool HitAxis(const Vector3f& origin, const Vector3f& dir);
    void Drag(float x, float y);
    void Draw();
    ~Gizmos();
    private:
    float DistanceRayToSegment(const Vector3f& ray_origin, const Vector3f& ray_dir, const Vector3f& seg_start, const Vector3f& seg_end);

    private:
        std::shared_ptr<RenderShader> shader_;
        std::shared_ptr<Actor> actor_;
        unsigned int vao_;
        unsigned int vbo_;
        int selected_axis = -1;
        float length_ = 0.5f;
        
    };
}

#endif