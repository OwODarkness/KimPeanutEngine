#include "render_object_test.h"

#include <vector>
#include "runtime/render/render_texture.h"
#include "runtime/render/model_loader.h"
#include "platform/path/path.h"
#include "runtime/render/skybox.h"
#include "runtime/render/render_shader.h"

#include "runtime/runtime_header.h"
#include "runtime/render/shader_pool.h"
namespace kpengine
{
    namespace test
    {

        std::shared_ptr<Skybox> GetRenderObjectSkybox()
        {
            std::vector<std::string> faces{
                "right.jpg",
                "left.jpg",
                "top.jpg",
                "bottom.jpg",
                "front.jpg",
                "back.jpg"
            };
            std::shared_ptr<RenderShader> shader = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_SKYBOX);
            const std::string shader_dir = kpengine::GetShaderDirectory();
            std::shared_ptr<RenderTextureCubeMap> cube_map = std::make_shared<RenderTextureCubeMap>( "texture/skybox/lake", faces);
            cube_map->Initialize();
            
            return std::make_shared<Skybox>(shader, cube_map);
        }

    }
}