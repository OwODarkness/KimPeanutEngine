#ifndef KPENGINE_RUNTIME_RENDER_POINTCLOUD_RESOURCE_H
#define KPENGINE_RUNTIME_RENDER_POINTCLOUD_RESOURCE_H

#include <vector>

#include "math/math_header.h"

namespace kpengine{
class RenderMaterial;

class RenderPointCloudResource{
    public:
        std::vector<Vector3f> vertex_buffer_;
        std::shared_ptr<RenderMaterial> material_;
    };
}

#endif