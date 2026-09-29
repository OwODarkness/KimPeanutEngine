#include "fullscreen_pass_resources.h"

#include "data/mesh.h"

namespace kpengine::render
{
    bool FullscreenPassResources::Initialize(graphics::RenderBackend &backend)
    {
        if (IsReady())
        {
            return true;
        }

        if (!mesh_.IsValid())
        {
            data::MeshData mesh_data{};
            data::Vertex first{};
            first.position = {-1.0f, -1.0f, 0.0f};
            first.tex_coord = {0.0f, 0.0f};
            data::Vertex second{};
            second.position = {3.0f, -1.0f, 0.0f};
            second.tex_coord = {2.0f, 0.0f};
            data::Vertex third{};
            third.position = {-1.0f, 3.0f, 0.0f};
            third.tex_coord = {0.0f, 2.0f};
            mesh_data.vertices = {first, second, third};
            mesh_data.indices = {0, 1, 2};
            mesh_data.sections = {{0, 3, 0}};
            mesh_ = backend.CreateMesh(mesh_data);
        }
        if (!linear_sampler_.IsValid())
        {
            linear_sampler_ = backend.CreateSampler(graphics::SamplerSettings{});
        }
        return IsReady();
    }

    void FullscreenPassResources::Cleanup(graphics::RenderBackend &backend)
    {
        if (mesh_.IsValid())
        {
            backend.DestroyMesh(mesh_);
            mesh_ = {};
        }
        if (linear_sampler_.IsValid())
        {
            backend.DestroySampler(linear_sampler_);
            linear_sampler_ = {};
        }
    }
}
