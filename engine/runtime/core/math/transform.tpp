#include "transform.h"

#include "vector3.h"
#include "rotator.h"

namespace kpengine::math
{
    template <typename T>
    Transform<T>::Transform() : position_(Vector3<T>()), rotator_(Rotator<T>()), scale_(Vector3<T>(1.f)) {}

    template <typename T>
    Transform<T>::Transform(const Vector3<T> &position, const Rotator<T> &rotator, const Vector3<T> &scale) : position_(position), rotator_(rotator), scale_(scale) {}

    template <typename T>
    Transform<T>::Transform(const Transform<T> &transform) : position_(transform.position_), rotator_(transform.rotator_), scale_(transform.scale_) {}

    template <typename T>
    Transform<T> Transform<T>::operator*(const Transform &rhs) const
    {
        Transform res;
        res.scale_ = scale_ * rhs.scale_;
        res.rotator_ = rotator_ + rhs.rotator_;

        Vector3<T> scaled_pos = scale_ * rhs.position_;
        Vector3<T> rotated_pos = rotator_.RotateVector(scaled_pos);
        res.position_ = position_ + rotated_pos;
        return res;
    }
}