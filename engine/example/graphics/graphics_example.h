#ifndef KPENGINE_EXAMPLE_GRAPHICS_EXAMPLE_H
#define KPENGINE_EXAMPLE_GRAPHICS_EXAMPLE_H

#include <cstdint>


namespace kpengine::example{
    void RenderExample();
    void RHIExample();
    bool RunGraphicsSmokeSuite(uint32_t frames_per_api);
}


#endif
