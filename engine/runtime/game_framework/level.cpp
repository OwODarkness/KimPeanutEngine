#include "level.h"


#include "runtime/game_framework/actor.h"
#include "runtime/core/log/logger.h"
#include "runtime/game_framework/mesh_actor.h"
#include "runtime/component/mesh_component.h"
#include "runtime/render/render_mesh.h"
#include "runtime/render/render_material.h"
#include "runtime/render/mesh_vertex.h"
#include "runtime/render/mesh_section.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/texture_pool.h"
#include "runtime/render/render_texture.h"
namespace kpengine{

    Level::Level()
    {
        
    }
    void Level::Initialize()
    {
        KP_LOG("LevelLog", LOG_LEVEL_DISPLAY, "Level Init...");

        std::shared_ptr<MeshActor> sphere = std::make_shared<MeshActor>("model/sphere/sphere.obj");
        AddActor(sphere);
        sphere->SetActorScale({0.6f, 0.6f, 0.6f});
        sphere->SetActorLocation({1.f, 1.f, 1.f});

        std::shared_ptr<MeshActor> bunny = std::make_shared<MeshActor>("model/bunny/stanford-bunny.obj");
        bunny->SetActorScale({5.f, 5.f, 5.f});
        bunny->SetActorLocation({0.f, -0.1f, 0.f});
        AddActor(bunny);

        std::shared_ptr<MeshActor> teapot = std::make_shared<MeshActor>("model/teapot/teapot.obj");
        teapot->SetActorScale({0.1f, 0.1f, 0.1f});
        teapot->SetActorLocation({0.5f, 0.f, 0.5f});
        AddActor(teapot);

        std::shared_ptr<MeshActor> nanosuit = std::make_shared<MeshActor>("model/nanosuit/nanosuit.obj");
        nanosuit->SetActorScale({0.1f, 0.1f, 0.1f});
        nanosuit->SetActorLocation({4.6f, 0.0f, -0.6f});
        AddActor(nanosuit);

        std::shared_ptr<PointCloudActor> dragon = std::make_shared<PointCloudActor>("model/dragon/dragon.obj");
        dragon->SetActorScale({0.1f, 0.1f, 0.1f});
        dragon->SetActorLocation({1.6f, 0.0f, -2.6f});
        AddActor(dragon);

        //floor
        std::shared_ptr<MeshActor> floor = std::make_shared<MeshActor>("model/brickwall/floor.obj");
        floor->SetActorScale({2.f, 1.f, 2.f});
        AddActor(floor);

        for(std::shared_ptr<Actor>& actor: actors_)
        {
            actor->Initialize();            
        }

        MeshComponent* floor_mesh_comp = dynamic_cast<MeshComponent*>(floor->GetRootComponent());
        std::shared_ptr<RenderMesh> floor_mesh = floor_mesh_comp->GetMesh();
        std::shared_ptr<RenderMaterial> material = RenderMaterial::CreatePhongMaterial({
            {material_map_type::DIFFUSE_MAP, "model/brickwall/brickwall_dif.jpg"},
            {material_map_type::NORMAL_MAP, "model/brickwall/brickwall_normal.jpg"}
        }, {},{});
        floor_mesh->SetMaterial(material, 0);

        MeshComponent* sphere_mesh_comp = dynamic_cast<MeshComponent*>(sphere->GetRootComponent());
        std::shared_ptr<RenderMesh> sphere_mesh = sphere_mesh_comp->GetMesh();
        std::shared_ptr<RenderMaterial> pbr_material = RenderMaterial::CreatePBRMaterial(
            {
                {material_map_type::ALBEDO_MAP, "texture/pbr/speckled-rust-bl/speckled-rust_albedo.png"},
                {material_map_type::ROUGHNESS_MAP, "texture/pbr/speckled-rust-bl/speckled-rust_roughness.png"},
                {material_map_type::METALLIC_MAP, "texture/pbr/speckled-rust-bl/speckled-rust_metallic.png"},
                {material_map_type::NORMAL_MAP, "texture/pbr/speckled-rust-bl/speckled-rust_normal-ogl.png"},

            }, {}, {});
        sphere_mesh->SetMaterial(pbr_material, 0); 

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