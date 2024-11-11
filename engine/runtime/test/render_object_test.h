#ifndef KPENGINE_TEST_RENDER_OBJECT_H
#define KPENGINE_TEST_RENDER_OBJECT_H

#include <memory>
#include <string>
namespace kpengine
{
    class RenderObject;
    namespace test
    {
        std::shared_ptr<RenderObject> GetRenderObjectTriangle();
        std::shared_ptr<RenderObject> GetRenderObjectRectangle();
        std::shared_ptr<RenderObject> GetRenderObjectCube();
        std::shared_ptr<RenderObject> GetRenderObjectSkyBox();
        std::shared_ptr<RenderObject> GetRenderObjectModel(const std::string& model_dir);
        std::shared_ptr<RenderObject> GetRenderObjectBunny();
        std::shared_ptr<RenderObject> GetRenderObjectTeapot();
        std::shared_ptr<RenderObject> GetRenderObjectVenusm();
    
    }
}

#endif