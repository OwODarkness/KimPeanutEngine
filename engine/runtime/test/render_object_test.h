#ifndef KPENGINE_TEST_RENDER_OBJECT_H
#define KPENGINE_TEST_RENDER_OBJECT_H

#include <memory>
#include <string>
namespace kpengine
{
    class RenderObject;
    class Skybox;
    namespace test
    {
        std::shared_ptr<RenderObject> GetRenderObjectTriangle();
        std::shared_ptr<RenderObject> GetRenderObjectRectangle();
        std::shared_ptr<RenderObject> GetRenderObjectFloor();
        std::shared_ptr<RenderObject> GetRenderObjectCube();
        std::shared_ptr<Skybox> GetRenderObjectSkybox();

        std::shared_ptr<RenderObject> GetRenderObjectModel(const std::string& model_dir);
        std::shared_ptr<RenderObject> GetRenderObjectBunny();
        std::shared_ptr<RenderObject> GetRenderObjectTeapot();
        std::shared_ptr<RenderObject> GetRenderObjectVenusm();
        std::shared_ptr<RenderObject> GetRenderObjectPlanet();
        std::shared_ptr<RenderObject> GetRenderObjectRock();
        std::shared_ptr<RenderObject> GetRenderObjectSphere();
        std::shared_ptr<RenderObject> GetRenderObjectNanosuit();
    }
}

#endif