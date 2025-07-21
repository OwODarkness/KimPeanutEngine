#include "frustum.h"

namespace kpengine
{
Frustum ExtractFrustumFromVPMat(const Matrix4f& m)
{
    Frustum frustum;
    // Left
    frustum.planes[0] = { 
        m[3][0] + m[0][0], 
        m[3][1] + m[0][1], 
        m[3][2] + m[0][2], 
        m[3][3] + m[0][3] 
    };

    // Right
    frustum.planes[1] = { 
        m[3][0] - m[0][0], 
        m[3][1] - m[0][1], 
        m[3][2] - m[0][2], 
        m[3][3] - m[0][3] 
    };

    // Bottom
    frustum.planes[2] = { 
        m[3][0] + m[1][0], 
        m[3][1] + m[1][1], 
        m[3][2] + m[1][2], 
        m[3][3] + m[1][3] 
    };

    // Top
    frustum.planes[3] = { 
        m[3][0] - m[1][0], 
        m[3][1] - m[1][1], 
        m[3][2] - m[1][2], 
        m[3][3] - m[1][3] 
    };

    // Near
    frustum.planes[4] = { 
        m[3][0] + m[2][0], 
        m[3][1] + m[2][1], 
        m[3][2] + m[2][2], 
        m[3][3] + m[2][3] 
    };

    // Far
    frustum.planes[5] = { 
        m[3][0] - m[2][0], 
        m[3][1] - m[2][1], 
        m[3][2] - m[2][2], 
        m[3][3] - m[2][3] 
    };

    // Normalize planes
    for (int i = 0; i < 6; ++i)
    {
        frustum.planes[i].Normalize();
    }

    return frustum;
}
}