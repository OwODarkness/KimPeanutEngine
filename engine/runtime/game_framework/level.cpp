#include "level.h"

#include "runtime/game_framework/actor.h"
#include "runtime/core/log/logger.h"
#include "runtime/game_framework/mesh_actor.h"

#include "runtime/render/render_mesh.h"
#include "runtime/render/render_mesh_resource.h"
#include "runtime/render/mesh_vertex.h"
#include "runtime/render/mesh_section.h"
namespace kpengine{

    Level::Level()
    {
        
    }
    void Level::Initialize()
    {
        KP_LOG("LevelLog", LOG_LEVEL_DISPLAY, "Level Init...");
        
        std::shared_ptr<MeshActor> actor = std::make_shared<MeshActor>("model/bunny/stanford-bunny.obj");
        actor->SetActorScale({10.f, 10.f, 10.f});
        AddActor(actor);

        // std::unique_ptr<RenderMeshResource> resource = std::make_unique<RenderMeshResource>();
        // std::vector<MeshVertex> vertex_buffer;
        // vertex_buffer.push_back(MeshVertex({0.5f, 0.5f, 0.0f}));
        // vertex_buffer.push_back(MeshVertex({0.5f, -0.5f, 0.0f}));
        // vertex_buffer.push_back(MeshVertex({-0.5f, 0.5f, 0.0f}));
        // vertex_buffer.push_back(MeshVertex({0.5f, -0.5f, 0.0f}));
        // vertex_buffer.push_back(MeshVertex({-0.5f, -0.5f, 0.0f}));
        // vertex_buffer.push_back(MeshVertex({-0.5f, 0.5f, 0.0f}));
        // resource->vertex_buffer_ = vertex_buffer;

        // std::vector<unsigned int> index_buffer = {
        //     0, 1, 3, // 第一个三角形
        //     1, 2, 3  // 第二个三角形
        // };
        // MeshSection section;
        // std::vector<MeshSection>

        // std::shared_ptr<RenderMesh_V2> mesh = std::make_shared<RenderMesh_V2>(resource);

        for(std::shared_ptr<Actor>& actor: actors_)
        {
            actor->Initialize();            
        }
    }

    void Level::Tick(float delta_time)
    {
        for(std::shared_ptr<Actor>& actor: actors_)
        {
            actor->Tick(delta_time);
        }
    }

    bool Level::AddActor(const std::shared_ptr<Actor>& actor)
    {
        if(actor == nullptr)
        {
            return false;
        }
        actors_.push_back(actor);
        return true;
    }
}