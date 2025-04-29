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

        std::shared_ptr<MeshActor> bunny = std::make_shared<MeshActor>("model/bunny/stanford-bunny.obj");
        bunny->SetActorScale({5.f, 5.f, 5.f});
        bunny->SetActorLocation({0.f, -0.1f, 0.f});
        AddActor(bunny);

        std::shared_ptr<MeshActor> teapot = std::make_shared<MeshActor>("model/bunny/teapot.obj");
        teapot->SetActorScale({0.1f, 0.1f, 0.1f});
        teapot->SetActorLocation({0.5f, 0.f, 0.5f});
        AddActor(teapot);

        std::shared_ptr<MeshActor> nanosuit = std::make_shared<MeshActor>("model/nanosuit/nanosuit.obj");
        nanosuit->SetActorScale({0.1f, 0.1f, 0.1f});
        nanosuit->SetActorLocation({-0.6f, 0.0f, -0.6f});
        AddActor(nanosuit);


        //floor
        std::shared_ptr<MeshActor> floor = std::make_shared<MeshActor>("model/brickwall/floor.obj");
        AddActor(floor);

        for(std::shared_ptr<Actor>& actor: actors_)
        {
            actor->Initialize();            
        }

        MeshComponent* floor_mesh_comp = dynamic_cast<MeshComponent*>(floor->GetRootComponent());
        std::shared_ptr<RenderMesh> floor_mesh = floor_mesh_comp->GetMesh();
        std::shared_ptr<RenderMaterial> material = std::make_shared<RenderMaterial>();
        std::vector<std::shared_ptr<RenderTexture>> diffs;
        std::string diff_key = "model/brickwall/brickwall_dif.jpg";
        TexturePool* tex_pool = runtime::global_runtime_context.render_system_->GetTexturePool();
        bool is_diff_cached = tex_pool->IsTextureCached(diff_key);
        if(is_diff_cached)
        {
            diffs.push_back(tex_pool->FindTextureByKey(diff_key));
        }
        else
        {
            std::shared_ptr<RenderTexture2D> tex_diff = std::make_shared<RenderTexture2D>(diff_key);
            tex_diff->Initialize();
            tex_pool->AddTexture(tex_diff);
            diffs.push_back(tex_diff);
        }
        std::string normal_key = "model/brickwall/brickwall_normal.jpg";
        std::shared_ptr<RenderTexture2D> tex_normal = std::make_shared<RenderTexture2D>(normal_key);
        tex_normal->Initialize();
        material->diffuse_textures_ = diffs;
        material->normal_texture_ = tex_normal;
        material->normal_texture_enable_ = false;
        material->shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_PHONG);
        
        floor_mesh->SetMaterial(material, 0);
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