#ifndef KPENGINE_RUNTIME_MATH_TRANSFORM_H
#define KPENGINE_RUNTIME_MATH_TRANSFORM_H


namespace kpengine::math
{

    template <typename T>
    class Rotator;
    template <typename T>
    class Vector3;

    template <typename T>
    class Transform
    {
        static_assert(std::is_floating_point_v<T>, "T must be floating point");

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

#endif // KPENGINE_RUNTIME_MATH_TRANSFORM_H