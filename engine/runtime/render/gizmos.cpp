#include "gizmos.h"
#include <iostream>
#include <glad/glad.h>
#include "runtime/core/utility/gl_vertex_array_guard.h"
#include "runtime/core/utility/gl_vertex_buffer_guard.h"
#include "runtime/core/utility/gl_element_buffer_guard.h"
#include "runtime/runtime_header.h"
#include "runtime/game_framework/actor.h"
#include "runtime/render/render_shader.h"
#include "runtime/component/mesh_component.h"

namespace kpengine
{
    void Gizmos::Initialize()
    {
        shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_AXIS);
        struct Vertex
        {
            Vector3f pos;
            Vector3f color;
        };
        Vertex axis_lines[] = {
            // X axis (red)
            {{0, 0, 0}, {1, 0, 0}},
            {{1, 0, 0}, {1, 0, 0}},

            // Y axis (green)
            {{0, 0, 0}, {0, 1, 0}},
            {{0, 1, 0}, {0, 1, 0}},

            // Z axis (blue)
            {{0, 0, 0}, {0, 0, 1}},
            {{0, 0, 1}, {0, 0, 1}},
        };

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        GlVertexArrayGuard vao_guard(vao_);

        GlVertexBufferGuard vbo_guard(vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(axis_lines), axis_lines, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(offsetof(Vertex, color)));
    }

    void Gizmos::SetActor(std::shared_ptr<Actor> actor)
    {
        actor_ = actor;
    }

    void Gizmos::Drag(const Vector3f &origin, const Vector3f &dir)
    {
        Vector3f current_hit_point = IntersectRayPlane(origin, dir, start_plane_origin, start_plane_normal);
        Vector3f raw_delta = current_hit_point - start_hit_point;

        Vector3f rotated_axis = GetRotateAxis();
        Vector3f projected = rotated_axis * raw_delta.DotProduct(rotated_axis);

        actor_->AddRelativeOffset(projected);
        start_hit_point = IntersectRayPlane(origin, dir, start_plane_origin, start_plane_normal);
    }

    void Gizmos::DragStart(const Vector3f &origin, const Vector3f &world_ray)
    {
        if (!actor_)
            return;
        MeshComponent *mesh = dynamic_cast<MeshComponent *>(actor_->GetRootComponent());
        if (!mesh)
            return;

        start_plane_origin = mesh->GetWorldAABB().Center();
        const Vector3f rotate_axis = start_plane_origin + scale * GetRotateAxis();
        const Vector3f view_dir = origin - mesh->GetWorldAABB().Center();
        start_plane_normal = ((rotate_axis.CrossProduct(view_dir)).CrossProduct(rotate_axis)).GetSafetyNormalize();
        start_hit_point = IntersectRayPlane(origin, world_ray, start_plane_origin, start_plane_normal);
    }

    Vector3f Gizmos::IntersectRayPlane(const Vector3f &ray_origin, const Vector3f &ray_dir, const Vector3f &plane_origin, const Vector3f &plane_normal)
    {
        float denom = ray_dir.DotProduct(plane_normal);
        if (std::fabs(denom) < 1e-6)
        {
            return {};
        }
        float t = (plane_origin - ray_origin).DotProduct(plane_normal) / denom;
        return ray_origin + t * ray_dir;
    }

    Vector3f Gizmos::GetRotateAxis() const
    {
        if (selected_axis == -1)
            return {};
        Vector3f axis{};
        if (selected_axis == 0)
        {
            axis = Vector3f(1.f, 0.f, 0.f);
        }
        else if (selected_axis == 1)
        {
            axis = Vector3f(0.f, 1.f, 0.f);
        }
        else if (selected_axis == 2)
        {
            axis = Vector3f(0.f, 0.f, -1.f);
        }
        const Rotatorf &rot = actor_->GetActorRotation();
        return rot.RotateVector(axis);
    }

    void Gizmos::Lock()
    {
        is_lock_ = true;
    }

    void Gizmos::UnLock()
    {
        is_lock_ = false;
    }

    bool Gizmos::HitAxis(const Vector3f &origin, const Vector3f &dir)
    {
        if (!actor_)
            return false;

        MeshComponent *mesh = dynamic_cast<MeshComponent *>(actor_->GetRootComponent());
        if (!mesh)
            return false;

        const Vector3f &gizmos_loc = mesh->GetWorldAABB().Center();
        float min_dist = std::numeric_limits<float>::max();

        Vector3f p1{}, p2{};
        float dist{};
        int pre_selected_axis = selected_axis;
        // test (1, 0, 0) axis
        p1 = gizmos_loc;
        p2 = gizmos_loc + Vector3f(1.f, 0.f, 0.f) * length_;
        dist = DistanceRayToSegment(origin, dir, p1, p2);
        if (dist < min_dist)
        {
            min_dist = dist;
            pre_selected_axis = 0;
        }

        p2 = gizmos_loc + Vector3f(0.f, 1.f, 0.f) * length_;
        dist = DistanceRayToSegment(origin, dir, p1, p2);
        if (dist < min_dist)
        {
            min_dist = dist;
            pre_selected_axis = 1;
        }

        p2 = gizmos_loc + Vector3f(0.f, 0.f, 1.f) * length_;
        dist = DistanceRayToSegment(origin, dir, p1, p2);
        if (dist < min_dist)
        {
            min_dist = dist;
            pre_selected_axis = 2;
        }

        if (pre_selected_axis == -1)
        {
            return false;
        }
        if (!is_lock_)
        {
            selected_axis = pre_selected_axis;
        }
        return true;
    }

    float Gizmos::DistanceRayToSegment(const Vector3f &ray_origin, const Vector3f &ray_dir, const Vector3f &seg_start, const Vector3f &seg_end)
    {
        Vector3f u = ray_dir;
        Vector3f v = seg_end - seg_start;
        Vector3f w = ray_origin - seg_start;

        float a = u.DotProduct(u); // always >= 0
        float b = u.DotProduct(v);
        float c = v.DotProduct(v); // always >= 0
        float d = u.DotProduct(w);
        float e = v.DotProduct(w);

        float denom = a * c - b * b;
        float s, t;

        if (denom < 1e-5f)
        {
            // Ray and segment are almost parallel
            s = 0.0f;
        }
        else
        {
            s = (b * e - c * d) / denom;
        }

        s = std::max(s, 0.0f); // clamp ray parameter to >= 0

        t = (b * s + e) / c;
        t = std::clamp(t, 0.0f, 1.0f); // clamp segment param to [0,1]

        Vector3f closest_ray = ray_origin + s * u;
        Vector3f closest_seg = seg_start + t * v;
        return (closest_ray - closest_seg).Norm();
    }

    void Gizmos::Draw()
    {
        if (!actor_)
            return;

        glDisable(GL_DEPTH_TEST);
        GlVertexArrayGuard vao_guard(vao_);
        shader_->UseProgram();
        MeshComponent *mesh = dynamic_cast<MeshComponent *>(actor_->GetRootComponent());
        if (!mesh)
            return;
        Transform3f transform;
        transform.position_ = mesh->GetWorldAABB().Center();
        transform.rotator_ = actor_->GetActorRotation();
        transform.scale_ = Vector3f(scale);
        shader_->SetInt("selected_axis", selected_axis);
        shader_->SetMat("model", Matrix4f::MakeTransformMatrix(transform).Transpose()[0]);
        glDrawArrays(GL_LINES, 0, 6);
        glEnable(GL_DEPTH_TEST);
    }

    Gizmos::~Gizmos()
    {
    }

}