#include "frustum.h"

namespace kpengine
{
Frustum ExtractFrustumFromVPMat(const Matrix4f& m)
{
    Frustum frustum;

    // Row-major: extract planes using row operations
    // Left: row4 + row1
    frustum.planes[0] = { 
        m[3][0] + m[0][0], 
        m[3][1] + m[0][1], 
        m[3][2] + m[0][2], 
        m[3][3] + m[0][3] 
    };

    // Right: row4 - row1
    frustum.planes[1] = { 
        m[3][0] - m[0][0], 
        m[3][1] - m[0][1], 
        m[3][2] - m[0][2], 
        m[3][3] - m[0][3] 
    };

    // Bottom: row4 + row2
    frustum.planes[2] = { 
        m[3][0] + m[1][0], 
        m[3][1] + m[1][1], 
        m[3][2] + m[1][2], 
        m[3][3] + m[1][3] 
    };

    // Top: row4 - row2
    frustum.planes[3] = { 
        m[3][0] - m[1][0], 
        m[3][1] - m[1][1], 
        m[3][2] - m[1][2], 
        m[3][3] - m[1][3] 
    };

    // Near: row4 + row3
    frustum.planes[4] = { 
        m[3][0] + m[2][0], 
        m[3][1] + m[2][1], 
        m[3][2] + m[2][2], 
        m[3][3] + m[2][3] 
    };

    // Far: row4 - row3
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