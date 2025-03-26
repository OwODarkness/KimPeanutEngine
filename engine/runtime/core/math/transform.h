#ifndef KPENGINE_RUNTIME_MATH_TRANSFORM_H
#define KPENGINE_RUNTIME_MATH_TRANSFORM_H

#include "math_header.h"
namespace kpengine{
    //TODO position scale rotation
    
    class Transform{
    public:
        Vector3f position;
        Rotatorf rotator;
        Vector3f scale;
    };
}

#endif //KPENGINE_RUNTIME_MATH_TRANSFORM_H