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
        
        std::shared_ptr<MeshActor> bunny = std::make_shared<MeshActor>("model/bunny/stanford-bunny.obj");
        bunny->SetActorScale({5.f, 5.f, 5.f});
        bunny->SetActorLocation({0.f, -0.1f, 0.f});
        AddActor(bunny);

        std::shared_ptr<MeshActor> teapot = std::make_shared<MeshActor>("model/bunny/teapot.obj");
        teapot->SetActorScale({0.1f, 0.1f, 0.1f});
        teapot->SetActorLocation({0.5f, 0.f, 0.5f});
        AddActor(teapot);

        std::shared_ptr<MeshActor> actor2 = std::make_shared<MeshActor>("model/bunny/floor.obj");
        AddActor(actor2);

        std::shared_ptr<MeshActor> nanosuit = std::make_shared<MeshActor>("model/nanosuit/nanosuit.obj");
        nanosuit->SetActorScale({0.1f, 0.1f, 0.1f});
        nanosuit->SetActorLocation({-0.6f, 0.0f, -0.6f});
        AddActor(nanosuit);



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