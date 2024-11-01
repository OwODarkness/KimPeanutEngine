#ifndef TEST_RENDER_OBJECT_H
#define TEST_RENDER_OBJECT_H

#include <memory>

namespace kpengine
{
    class RenderObject;
    namespace test
    {
        std::shared_ptr<RenderObject> GetRenderObjectTriangle();
        std::shared_ptr<RenderObject> GetRenderObjectRectangle();
    }
}

#endif