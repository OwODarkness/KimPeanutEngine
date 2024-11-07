#ifndef KPENGINE_TEST_RENDER_OBJECT_H
#define KPENGINE_TEST_RENDER_OBJECT_H

#include <memory>

namespace kpengine
{
    class RenderObject;
    namespace test
    {
        std::shared_ptr<RenderObject> GetRenderObjectTriangle();
        std::shared_ptr<RenderObject> GetRenderObjectRectangle();
        std::shared_ptr<RenderObject> GetRenderObjectCube();
        std::shared_ptr<RenderObject> GetRenderObjectModel();
    }
}

#endif