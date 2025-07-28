#ifndef KPENGINE_RUNTIME_MATH_TRANSFORM_H
#define KPENGINE_RUNTIME_MATH_TRANSFORM_H

#include "runtime/core/math/vector3.h"
#include "runtime/core/math/rotator.h"
namespace kpengine::math
{

    template <typename T>
    class Transform
    {
        static_assert(std::is_floating_point_v<T>, "T must be floating point");

    public:
        Transform() : position_(Vector3<T>()), rotator_(Rotator<T>()), scale_(Vector3<T>(1.f)) {}
        Transform(const Vector3<T> &position, const Rotator<T> &rotator, const Vector3<T> &scale) : position_(position), rotator_(rotator), scale_(scale) {}
        Transform(const Transform<T> &transform) : position_(transform.position_), rotator_(transform.rotator_), scale_(transform.scale_) {}

        Transform &operator=(const Transform<T> &rhs)
        {
            position_ = rhs.position_;
            rotator_ = rhs.rotator_;
            scale_ = rhs.scale_;
            return *this;
        }

        Transform operator*(const Transform<T> &rhs)
        {
            Transform res;
        }

    public:
        Vector3<T> position_;
        Rotator<T> rotator_;
        Vector3<T> scale_;
    };

}

#endif // KPENGINE_RUNTIME_MATH_TRANSFORM_H