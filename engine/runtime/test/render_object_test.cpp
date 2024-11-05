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
                {{0, 0.5, 0}, {}, {0.f, 1.f}}
                };
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
                {{-0.5, -0.5, -0.5}},
                {{0.5, -0.5, -0.5}},
                {{0.5, 0.5, -0.5}},
                {{-0.5, 0.5, -0.5}},

                {{-0.5, -0.5, 0.5}},
                {{0.5, -0.5, 0.5}},
                {{0.5, 0.5, 0.5}},
                {{-0.5, 0.5, 0.5}}};

            std::vector<unsigned int> indices = {
                0, 1, 2,
                2, 3, 0,
                0, 1, 5,
                5, 4, 0,
                1, 2, 6,
                6, 5, 1,
                2, 3, 7,
                7, 6, 2,
                3, 0, 4,
                4, 7, 3,
                4, 5, 6,
                6, 7, 4};

            return std::make_shared<RenderObject>(new kpengine::RenderMesh(verticles, indices), "normal.vs", "normal.fs");
        }
    }
}