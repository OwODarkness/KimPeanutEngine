#ifndef KPENGINE_RUNTIME_MATH_ROTATOR_H
#define KPENGINE_RUNTIME_MATH_ROTATOR_H

namespace kpengine{
namespace math{
    template<typename T>
    class Rotator{
    public:
        Rotator():roll_(0), pitch_(0), yaw_(0){}
        Rotator(T roll, T pitch, T yaw):roll_(roll), pitch_(0), yaw_(yaw){}

        Rotator operator+(const Rotator& rot) const
        {
            return Rotator(roll_ + rot.roll_, pitch_ + rot.pitch_, yaw_ + rot.yaw_);
        }
    public:
        T roll_; //around z-axis
        T pitch_;//around x-axis
        T yaw_;  //around y-axis
    };
}
}

#endif