#ifndef KPENGINE_RUNTIME_MATH_TRANSFORM_H
#define KPENGINE_RUNTIME_MATH_TRANSFORM_H

#include <concepts>

namespace kpengine::math
{

    template <std::floating_point T>
    class Rotator;
    template <std::floating_point T>
    class Vector3;

    template <std::floating_point T>
    class Transform
    {
    public:
        Transform();
        Transform(const Vector3<T> &position, const Rotator<T> &rotator, const Vector3<T> &scale);
        Transform(const Transform<T> &transform);

        Transform& operator=(const Transform &rhs)
        {
            position_ = rhs.position_;
            rotator_ = rhs.rotator_;
            scale_ = rhs.scale_;
            return *this;
        }

        bool operator==(const Transform& rhs) const
        {
            return position_ == rhs.position_ && rotator_ == rhs.rotator_ && scale_ == rhs.scale_;
        }

        bool operator!=(const Transform& rhs) const
        {
            return !(*this == rhs);
        }

        Transform operator*(const Transform &rhs) const;

    public:
        Vector3<T> position_;
        Rotator<T> rotator_;
        Vector3<T> scale_;
    };

}
#include "transform.tpp"
#endif // KPENGINE_RUNTIME_MATH_TRANSFORM_H
