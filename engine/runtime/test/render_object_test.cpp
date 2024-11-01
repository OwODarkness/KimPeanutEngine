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
                {{0.5, -0.5, 0}},
                {{-0.5, -0.5, 0}},
                {{0, 0.5, 0}}};
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
    }
}