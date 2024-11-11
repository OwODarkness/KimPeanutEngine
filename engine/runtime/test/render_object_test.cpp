#include "render_object_test.h"

#include <vector>

#include "runtime/render/render_mesh.h"
#include "runtime/render/render_object.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"
#include "runtime/render/model_loader.h"
#include "platform/path/path.h"

namespace kpengine
{
    namespace test
    {
        std::shared_ptr<RenderObject> GetRenderObjectTriangle()
        {
            std::vector<Vertex> verticles = {
                {{0.5, -0.5, 0}, {}, {1.f, 0.f}},
                {{-0.5, -0.5, 0}, {}, {0.f, 0.f}},
                {{0, 0.5, 0}, {}, {0.5f, 1.f}}};
            std::vector<unsigned int> indices = {0, 1, 2};
            std::vector<std::shared_ptr<RenderMesh>> meshes;
            
            std::shared_ptr<RenderMaterialStanard> material = std::make_shared<RenderMaterialStanard>();
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>("default.jpg");
            texture->Initialize();
            material->diffuse_textures_.push_back(texture);
            std::shared_ptr<RenderMesh> mesh = std::make_shared<RenderMeshStandard>(verticles, indices, material);
            
            meshes.push_back(mesh);
            const std::string shader_dir = kpengine::GetShaderDirectory();
            return std::make_shared<RenderObject>(meshes, shader_dir + "phong.vs", shader_dir + "phong.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectRectangle()
        {
            std::vector<Vertex> verticles = {
                {{0.5, -0.5, 0}},
                {{-0.5, -0.5, 0}},
                {{-0.5, 0.5, 0}},
                {{0.5, 0.5, 0}}};
            std::vector<unsigned int> indices = {0, 1, 2, 2, 3, 0};
            std::vector<std::shared_ptr<RenderMesh>> meshes;
            std::shared_ptr<RenderMaterialStanard> material = std::make_shared<RenderMaterialStanard>();
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>("default.jpg");
            texture->Initialize();
            material->diffuse_textures_.push_back(texture);
            std::shared_ptr<RenderMesh> mesh = std::make_shared<RenderMeshStandard>(verticles, indices, material);
            
            meshes.push_back(mesh);
            const std::string shader_dir = kpengine::GetShaderDirectory();

            return std::make_shared<RenderObject>(meshes, shader_dir + "phong.vs", shader_dir + "phong.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectCube()
        {
            std::vector<Vertex> verticles = {
                // Front face (-Z direction)
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
                {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
                {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
                {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
                {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},

                // Back face (+Z direction)
                {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},

                // Left face (-X direction)
                {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
                {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
                {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
                {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
                {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},

                // Right face (+X direction)
                {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
                {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
                {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
                {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
                {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},

                // Bottom face (-Y direction)
                {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
                {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
                {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
                {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
                {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
                {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},

                // Top face (+Y direction)
                {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
                {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
                {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
                {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}};

            std::vector<unsigned int> indices = {
                0, 1, 2, 3, 4, 5,       // Front face
                6, 7, 8, 9, 10, 11,     // Back face
                12, 13, 14, 15, 16, 17, // Left face
                18, 19, 20, 21, 22, 23, // Right face
                24, 25, 26, 27, 28, 29, // Bottom face
                30, 31, 32, 33, 34, 35  // Top face
            };
            std::vector<std::shared_ptr<RenderMesh>> meshes;
            std::shared_ptr<RenderMaterialStanard> material = std::make_shared<RenderMaterialStanard>();
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>("default.jpg");
            texture->Initialize();
            material->diffuse_textures_.push_back(texture);
            std::shared_ptr<RenderMesh> mesh = std::make_shared<RenderMeshStandard>(verticles, indices, material);
            
            meshes.push_back(mesh);
            const std::string shader_dir = kpengine::GetShaderDirectory();
            
            return std::make_shared<RenderObject>(meshes, shader_dir+"phong.vs", shader_dir+"phong.fs");
        }


        std::shared_ptr<RenderObject> GetRenderObjectSkyBox()
        {
            std::vector<std::shared_ptr<RenderMesh>> meshes;
            std::vector<std::string> faces{
                "right.jpg",
                "left.jpg",
                "top.jpg",
                "bottom.jpg",
                "front.jpg",
                "back.jpg"
            };
            std::shared_ptr<RenderTextureCubeMap> cube_map = std::make_shared<RenderTextureCubeMap>(GetTextureDirectory() + "skybox", faces);
            cube_map->Initialize();
            std::shared_ptr<RenderMaterialSkyBox> material = std::make_shared<RenderMaterialSkyBox>();
            material->cube_map_texture_ = cube_map;
            meshes.push_back(std::make_shared<SkyBox>(material));

            const std::string shader_dir = kpengine::GetShaderDirectory();
            return std::make_shared<RenderObject>(meshes, shader_dir+"skybox.vs", shader_dir+"skybox.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectModel(const std::string& model_dir)
        {
            std::vector<std::shared_ptr<RenderMesh>> meshes;
            ModelLoader* model_loader = new ModelLoader();
            meshes = model_loader->Load(model_dir);
            delete model_loader;
            const std::string shader_dir = kpengine::GetShaderDirectory();
            return std::make_shared<RenderObject>(meshes, shader_dir + "phong.vs", shader_dir + "phong.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectBunny()
        {
            std::shared_ptr<RenderObject> render_object = GetRenderObjectModel(GetModelDirectory() + "bunny/stanford-bunny.obj");
            render_object->SetScale({10.f, 10.f, 10.f});
            return render_object;
        } 


        std::shared_ptr<RenderObject> GetRenderObjectTeapot()
        {
            std::shared_ptr<RenderObject> render_object = GetRenderObjectModel(GetModelDirectory() + "bunny/teapot.obj");
            render_object->SetScale({0.1f, 0.1f, 0.1f});
            //render_object->SetLocation({13.f, 13.f, 13.f});

            return render_object;
        } 


                std::shared_ptr<RenderObject> GetRenderObjectVenusm()
        {
            std::shared_ptr<RenderObject> render_object = GetRenderObjectModel(GetModelDirectory() + "bunny/venusm.obj");
            render_object->SetScale({0.001f, 0.001f, 0.001f});
            return render_object;
        } 
    }
}