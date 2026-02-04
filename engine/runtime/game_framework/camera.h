#ifndef KPENGINE_RUNTIME_GAME_FRAMEWORK_CAMERA_H
#define KPENGINE_RUNTIME_GAME_FRAMEWORK_CAMERA_H

#include <memory>
#include "input/input_action.h"
#include "input/input_context.h"
#include "component/camera_component.h"
#include "actor.h"

namespace kpengine{
    constexpr float K_PITCH_MIN = -89.f;
    constexpr float K_PITCH_MAX = 89.f; 

    class Camera: public Actor{
    public:
        void Initialize(class input::InputContext* context);
        void MoveForward(float delta);
        void MoveRight(float delta);
        void MoveUp(float delta);
        void Rotate(float theta, float phi);
        CameraData GetCameraData() const;
    private:
        void OnCameraRotateCallback(const input::InputState& state);
        void OnCameraMoveCallback(const input::InputState& state);
        void OnCameraScrollCallback(const input::InputState& state);

    private:
        float move_sensitivity_ = 0.05f;
        float rotate_sensitivity_ = 0.05f;
        float move_speed_ = 1.f;
        float rotate_speed_ = 1.f; 
        class CameraComponent* camera_comp = nullptr;

        std::shared_ptr<input::InputAction> rotate_action;
        std::shared_ptr<input::InputAction> forward_action;
        std::shared_ptr<input::InputAction> backward_action;
        std::shared_ptr<input::InputAction> right_action;
        std::shared_ptr<input::InputAction> left_action;
        std::shared_ptr<input::InputAction> up_action;
        std::shared_ptr<input::InputAction> down_action;
        std::shared_ptr<input::InputAction> scroll_action;
    };
}

#endif