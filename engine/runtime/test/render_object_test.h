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

        std::shared_ptr<Skybox> GetRenderObjectSkybox();
    }
}

#endif