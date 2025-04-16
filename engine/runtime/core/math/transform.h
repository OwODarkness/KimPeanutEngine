#ifndef KPENGINE_RUNTIME_MATH_TRANSFORM_H
#define KPENGINE_RUNTIME_MATH_TRANSFORM_H

#include "runtime/core/math/vector3.h"
#include "runtime/core/math/rotator.h"
namespace kpengine{
    //TODO position scale rotation
namespace math{
        
    template<typename T>
    class Transform{
    public:
        Transform():position_(Vector3<T>()), rotator_(Rotator3<T>()), scale_(Vector3<T>()){}
        Transform(const Vector3<T>& position, const Rotator<T>& rotator, const Vector3<T>& scale):position_(position), rotator_(rotator), scale_(scale){}
        Vector3<T> position_;
        Rotator<T> rotator_;
        Vector3<T> scale_;
    };
}
}

#endif //KPENGINE_RUNTIME_MATH_TRANSFORM_H