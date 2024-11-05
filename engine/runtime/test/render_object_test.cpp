#include "render_object_test.h"
#include "runtime/render/render_mesh.h"
#include "runtime/render/render_object.h"
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

            return std::make_shared<RenderObject>(new kpengine::RenderMesh(verticles, indices), "normal.vs", "normal.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectRectangle()
        {
            std::vector<Vertex> verticles = {
                {{0.5, -0.5, 0}},
                {{-0.5, -0.5, 0}},
                {{-0.5, 0.5, 0}},
                {{0.5, 0.5, 0}}};
            std::vector<unsigned int> indices = {0, 1, 2, 2, 3, 0};

            return std::make_shared<RenderObject>(new kpengine::RenderMesh(verticles, indices), "normal.vs", "normal.fs");
        }

        std::shared_ptr<RenderObject> GetRenderObjectCube()
        {
            std::vector<Vertex> verticles = {
                {{-0.5f, -0.5f, -0.5f}, {}, {0.0f, 0.0f}},
                {{0.5f, -0.5f, -0.5f}, {}, {1.0f, 0.0f}},
                {{0.5f, 0.5f, -0.5f}, {}, {1.0f, 1.0f}},
                {{0.5f, 0.5f, -0.5f}, {}, {1.0f, 1.0f}},
                {{-0.5f, 0.5f, -0.5f}, {}, {0.0f, 1.0f}},
                {{-0.5f, -0.5f, -0.5f}, {}, {0.0f, 0.0f}},

                {{-0.5f, -0.5f, 0.5f}, {}, {0.0f, 0.0f}},
                {{0.5f, -0.5f, 0.5f}, {}, {1.0f, 0.0f}},
                {{0.5f, 0.5f, 0.5f}, {}, {1.0f, 1.0f}},
                {{0.5f, 0.5f, 0.5f}, {}, {1.0f, 1.0f}},
                {{-0.5f, 0.5f, 0.5f}, {}, {0.0f, 1.0f}},
                {{-0.5f, -0.5f, 0.5f}, {}, {0.0f, 0.0f}},

                {{-0.5f, 0.5f, 0.5f}, {}, {1.0f, 0.0f}},
                {{-0.5f, 0.5f, -0.5f}, {}, {1.0f, 1.0f}},
                {{-0.5f, -0.5f, -0.5f}, {}, {0.0f, 1.0f}},
                {{-0.5f, -0.5f, -0.5f}, {}, {0.0f, 1.0f}},
                {{-0.5f, -0.5f, 0.5f}, {}, {0.0f, 0.0f}},
                {{-0.5f, 0.5f, 0.5f}, {}, {1.0f, 0.0f}},

                {{0.5f, 0.5f, 0.5f}, {}, {1.0f, 0.0f}},
                {{0.5f, 0.5f, -0.5f}, {}, {1.0f, 1.0f}},
                {{0.5f, -0.5f, -0.5f}, {}, {0.0f, 1.0f}},
                {{0.5f, -0.5f, -0.5f}, {}, {0.0f, 1.0f}},
                {{0.5f, -0.5f, 0.5f}, {}, {0.0f, 0.0f}},
                {{0.5f, 0.5f, 0.5f}, {}, {1.0f, 0.0f}},

                {{-0.5f, -0.5f, -0.5f}, {}, {0.0f, 1.0f}},
                {{0.5f, -0.5f, -0.5f}, {}, {1.0f, 1.0f}},
                {{0.5f, -0.5f, 0.5f}, {}, {1.0f, 0.0f}},
                {{0.5f, -0.5f, 0.5f}, {}, {1.0f, 0.0f}},
                {{-0.5f, -0.5f, 0.5f}, {}, {0.0f, 0.0f}},
                {{-0.5f, -0.5f, -0.5f}, {}, {0.0f, 1.0f}},

                {{-0.5f, 0.5f, -0.5f}, {}, {0.0f, 1.0f}},
                {{0.5f, 0.5f, -0.5f}, {}, {1.0f, 1.0f}},
                {{0.5f, 0.5f, 0.5f}, {}, {1.0f, 0.0f}},
                {{0.5f, 0.5f, 0.5f}, {}, {1.0f, 0.0f}},
                {{-0.5f, 0.5f, 0.5f}, {}, {0.0f, 0.0f}},
                {{-0.5f, 0.5f, -0.5f}, {}, {0.0f, 1.0f}}};

            std::vector<unsigned int> indices = {
                0, 1, 2, 3, 4, 5,       // Front face
                6, 7, 8, 9, 10, 11,     // Back face
                12, 13, 14, 15, 16, 17, // Left face
                18, 19, 20, 21, 22, 23, // Right face
                24, 25, 26, 27, 28, 29, // Bottom face
                30, 31, 32, 33, 34, 35  // Top face
                };

                return std::make_shared<RenderObject>(new kpengine::RenderMesh(verticles, indices), "normal.vs", "normal.fs");
        }
    }
}