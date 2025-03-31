#include "render_object_test.h"

#include <vector>
#include <cmath>
#include <iostream>
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
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>("texture/default.jpg");
            texture->Initialize();
            material->diffuse_textures_.push_back(texture);
            std::shared_ptr<RenderMesh> mesh = std::make_shared<RenderMeshStandard>(verticles, indices, material);
            
            meshes.push_back(mesh);
            const std::string shader_dir = kpengine::GetShaderDirectory();
            return std::make_shared<RenderSingleObject>(meshes, shader_dir + "phong.vs", shader_dir + "phong.fs");
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
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>("texture/default.jpg");
            texture->Initialize();
            material->diffuse_textures_.push_back(texture);
            std::shared_ptr<RenderMesh> mesh = std::make_shared<RenderMeshStandard>(verticles, indices, material);
            
            meshes.push_back(mesh);
            const std::string shader_dir = kpengine::GetShaderDirectory();

            return std::make_shared<RenderSingleObject>(meshes, shader_dir + "phong.vs", shader_dir + "phong.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectFloor()
        {   
            glm::vec3 pos1{10.0f, -0.5f,  10.0f};
            glm::vec3 pos2{-10.0f, -0.5f,  10.0f};
            glm::vec3 pos3{-10.0f, -0.5f, -10.0f};
            glm::vec3 pos4{10.0f, -0.5f,  -10.0f};

            glm::vec3 normal{0.0f, 1.0f, 0.0f};

            glm::vec2 uv1{10.f, 0.f};
            glm::vec2 uv2{0.f, 0.f};
            glm::vec2 uv3{0.f, 10.f};
            glm::vec2 uv4{10.f, 10.f};


            glm::vec3 edge1 = pos2 - pos1;
            glm::vec3 edge2 = pos3 - pos1;
            glm::vec2 delta_uv1 = uv2 - uv1;
            glm::vec2 delta_uv2 = uv3 - uv1;

            glm::vec3 tangent1, tangent2;

            float f = 1.f / (delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x);
            tangent1.x = f * (delta_uv2.y * edge1.x - delta_uv1.y * edge2.x);
            tangent1.y = f * (delta_uv2.y * edge1.y - delta_uv1.y * edge2.y);
            tangent1.z = f * (delta_uv2.y * edge1.z - delta_uv1.y * edge2.z);

            tangent1 = glm::normalize(tangent1);

            edge1 = pos3 - pos1;
            edge2 = pos4 - pos1;
            delta_uv1 = uv3 - uv1;
            delta_uv2 = uv4 - uv1;
            f = 1.f / (delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x);
            tangent2.x = f * (delta_uv2.y * edge1.x - delta_uv1.y * edge2.x);
            tangent2.y = f * (delta_uv2.y * edge1.y - delta_uv1.y * edge2.y);
            tangent2.z = f * (delta_uv2.y * edge1.z - delta_uv1.y * edge2.z);
            tangent2 = glm::normalize(tangent2);

            std::vector<Vertex> verticles = {
                {pos1, normal, uv1, tangent1},
                {pos2, normal, uv2, tangent1},
                {pos3, normal, uv3, tangent1},
                {pos1, normal, uv1, tangent2},
                {pos3, normal, uv3, tangent2},
                {pos4, normal, uv4, tangent2}
            };
            std::vector<unsigned int> indices = {0, 1, 2, 3, 4, 5};

            std::vector<std::shared_ptr<RenderMesh>> meshes;
            
            std::shared_ptr<RenderMaterialStanard> material = std::make_shared<RenderMaterialStanard>();
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>( "texture/brickwall.jpg");
            texture->Initialize();

            std::shared_ptr<RenderTexture> normal_texture = std::make_shared<RenderTexture2D>("texture/brickwall_normal.jpg");
            normal_texture->Initialize();

            material->diffuse_textures_.push_back(texture);
            material->normal_texture_enable_ = true;
            material->normal_texture_ = normal_texture;
            std::shared_ptr<RenderMesh> mesh = std::make_shared<RenderMeshStandard>(verticles, indices, material);
            
            meshes.push_back(mesh);
            const std::string shader_dir = kpengine::GetShaderDirectory();
            return std::make_shared<RenderSingleObject>(meshes, shader_dir + "phong.vs", shader_dir + "phong.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectSphere()
        {
                        std::shared_ptr<RenderObject> render_object = GetRenderObjectModel("model/sphere.obj");
            //render_object->SetScale({0.1f, 0.1f, 0.1f});

            return render_object;
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
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>( "texture/container.png");
            texture->Initialize();
            std::shared_ptr<RenderTexture> texture_specular = std::make_shared<RenderTexture2D>( "texture/container2_specular.png");
            texture_specular->Initialize();
            std::shared_ptr<RenderTexture> texture_emmision = std::make_shared<RenderTexture2D>( "texture/matrix.jpg");
            texture_emmision->Initialize();
            material->diffuse_textures_.push_back(texture);
            material->specular_textures_.push_back(texture_specular);
            material->emmision_texture = texture_emmision;
            std::shared_ptr<RenderMesh> mesh = std::make_shared<RenderMeshStandard>(verticles, indices, material);
            
            meshes.push_back(mesh);
            const std::string shader_dir = kpengine::GetShaderDirectory();
            
            return std::make_shared<RenderSingleObject>(meshes, shader_dir+"phong.vs", shader_dir+"phong.fs");
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
            std::shared_ptr<RenderTextureCubeMap> cube_map = std::make_shared<RenderTextureCubeMap>( "texture/skybox/lake", faces);
            cube_map->Initialize();
            std::shared_ptr<RenderMaterialSkyBox> material = std::make_shared<RenderMaterialSkyBox>();
            material->cube_map_texture_ = cube_map;
            meshes.push_back(std::make_shared<SkyBox>(material));

            const std::string shader_dir = kpengine::GetShaderDirectory();
            return std::make_shared<RenderSingleObject>(meshes, shader_dir+"skybox.vs", shader_dir+"skybox.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectModel(const std::string& model_dir)
        {
            std::vector<std::shared_ptr<RenderMesh>> meshes;
            ModelLoader* model_loader = new ModelLoader();
            meshes = model_loader->Load(model_dir);
            delete model_loader;
            const std::string shader_dir = kpengine::GetShaderDirectory();
            return std::make_shared<RenderSingleObject>(meshes, shader_dir + "phong.vs", shader_dir + "phong.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectBunny()
        {
            std::shared_ptr<RenderObject> render_object = GetRenderObjectModel( "model/bunny/stanford-bunny.obj");
            render_object->SetScale({10.f, 10.f, 10.f});
            return render_object;
        } 


        std::shared_ptr<RenderObject> GetRenderObjectTeapot()
        {
            std::shared_ptr<RenderObject> render_object = GetRenderObjectModel( "model/bunny/teapot.obj");
            render_object->SetScale({0.1f, 0.1f, 0.1f});
            //render_object->SetLocation({13.f, 13.f, 13.f});

            return render_object;
        } 


        std::shared_ptr<RenderObject> GetRenderObjectVenusm()
        {
            std::shared_ptr<RenderObject> render_object = GetRenderObjectModel( "model/bunny/venusm.obj");
            render_object->SetScale({0.001f, 0.001f, 0.001f});
            return render_object;
        }

        std::shared_ptr<RenderObject> GetRenderObjectPlanet()
        {
            std::shared_ptr<RenderObject> render_object = GetRenderObjectModel( "model/planet/planet.obj");
            render_object->SetScale({0.5, 0.5, 0.5});
            return render_object;
        } 

        std::shared_ptr<RenderObject> GetRenderObjectNanosuit()
        {
            std::shared_ptr<RenderObject> render_object = GetRenderObjectModel( "model/nanosuit/nanosuit.obj");
            render_object->SetScale({0.1, 0.1, 0.1});
            return render_object;
        }

        std::shared_ptr<RenderObject> GetRenderObjectRock()
        {
            int amount = 100;
            std::vector<Transformation> transformation;
            transformation.reserve(amount);
            srand(1000);
            float radius = 50.f;
            float offset = 2.5f;

            for(int i = 0;i<amount ;i++)
            {
                Transformation tra;
                float angle = (float)i / (float)amount * 2.f * 3.1415926f;
                float displacement = (rand() % (int)( 2 * offset * 100))/ 100.f - offset;
                float x = std::sin(angle) * radius + displacement;
                displacement = (rand() % (int)( 2 * offset * 100))/ 100.f - offset;
                float y = displacement * 0.4f;
                displacement = (rand() % (int)( 2 * offset * 100))/ 100.f - offset;
                float z = std::cos(angle) * radius + displacement;

                tra.location = glm::vec3(x, y, z);
                float scale = (rand() % 20) / 100.f + 0.05f;
                tra.scale = glm::vec3(scale * 0.5f);

                float roll = (float)(rand() % 360);
                float pitch = (float)(rand() % 360);
                float yaw = (float)(rand() % 360);
                tra.rotation = glm::vec3(roll, pitch, yaw);

                transformation.push_back(tra);

            }
            
            std::vector<std::shared_ptr<RenderMesh>> meshes;
            ModelLoader* model_loader = new ModelLoader();
            meshes = model_loader->Load( "model/rock/rock.obj");
            delete model_loader;
            const std::string shader_dir = kpengine::GetShaderDirectory();
            std::shared_ptr<RenderMultipleObject> render_object = std::make_shared<RenderMultipleObject>(meshes, shader_dir + "cluster.vs", shader_dir + "phong.fs");
            render_object->transformations_ = transformation;
            return  render_object;

        }
    }
}