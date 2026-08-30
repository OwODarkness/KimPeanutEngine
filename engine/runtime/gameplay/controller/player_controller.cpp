#include "gameplay/controller/player_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "gameplay/actor/actor.h"
#include "gameplay/component/camera_component.h"
#include "gameplay/world/gameplay_world.h"
#include "input/input_system.h"

namespace kpengine::gameplay
{
    namespace
    {
        constexpr char kMoveAction[] = "camera.move";
        constexpr char kLookAction[] = "camera.look";
        constexpr char kZoomAction[] = "camera.zoom";
        constexpr float kPitchMin = -89.0f;
        constexpr float kPitchMax = 89.0f;
        constexpr float kDegreesPerTurn = 360.0f;
        const Vector3f kWorldUp{0.0f, 1.0f, 0.0f};

        float WrapDegrees(float degrees)
        {
            degrees = std::fmod(degrees, kDegreesPerTurn);
            return degrees < 0.0f ? degrees + kDegreesPerTurn : degrees;
        }
    }

    PlayerController::PlayerController(GameplayWorld &world, input::InputSystem *input_system,
                                       std::string input_context_name)
        : world_(&world), input_system_(input_system),
          input_context_name_(std::move(input_context_name))
    {
    }

    PlayerController::~PlayerController()
    {
        Unpossess();
        UnbindInput();
    }

    bool PlayerController::BindInput()
    {
        if (input_bound_)
        {
            return true;
        }
        if (input_system_ == nullptr)
        {
            return false;
        }

        input_context_ = input_system_->GetInputContext(input_context_name_);
        if (input_context_ == nullptr)
        {
            return false;
        }

        const bool bound =
            BindMoveAction("camera.move.forward", {input::InputDevice::Keyboard,
                                                    static_cast<int>(input::KeyboardKeyCode::W)},
                           {1.0f, 0.0f, 0.0f}) &&
            BindMoveAction("camera.move.backward", {input::InputDevice::Keyboard,
                                                     static_cast<int>(input::KeyboardKeyCode::S)},
                           {-1.0f, 0.0f, 0.0f}) &&
            BindMoveAction("camera.move.right", {input::InputDevice::Keyboard,
                                                  static_cast<int>(input::KeyboardKeyCode::D)},
                           {0.0f, 1.0f, 0.0f}) &&
            BindMoveAction("camera.move.left", {input::InputDevice::Keyboard,
                                                 static_cast<int>(input::KeyboardKeyCode::A)},
                           {0.0f, -1.0f, 0.0f}) &&
            BindMoveAction("camera.move.up", {input::InputDevice::Keyboard,
                                               static_cast<int>(input::KeyboardKeyCode::E)},
                           {0.0f, 0.0f, 1.0f}) &&
            BindMoveAction("camera.move.down", {input::InputDevice::Keyboard,
                                                 static_cast<int>(input::KeyboardKeyCode::Q)},
                           {0.0f, 0.0f, -1.0f}) &&
            BindAxisAction("camera.look", {input::InputDevice::Mouse,
                                            input::kMouseCursorCode},
                           input::InputValueType::Axis2D, kLookAction,
                           input::InputActionSource::Mouse) &&
            BindAxisAction("camera.zoom", {input::InputDevice::Mouse,
                                            input::kMouseScrollCode},
                           input::InputValueType::Axis1D, kZoomAction,
                           input::InputActionSource::Mouse) &&
            BindGamepadStickAction(
                "camera.move.gamepad", {input::InputDevice::Gamepad,
                                         static_cast<int>(input::GamepadAxisCode::LeftStick)},
                true) &&
            BindGamepadStickAction(
                "camera.look.gamepad", {input::InputDevice::Gamepad,
                                         static_cast<int>(input::GamepadAxisCode::RightStick)},
                false) &&
            BindGamepadTriggerAction(
                "camera.move.gamepad.down", {input::InputDevice::Gamepad,
                                              static_cast<int>(input::GamepadAxisCode::LeftTrigger)},
                -1.0f) &&
            BindGamepadTriggerAction(
                "camera.move.gamepad.up", {input::InputDevice::Gamepad,
                                            static_cast<int>(input::GamepadAxisCode::RightTrigger)},
                1.0f);
        if (!bound)
        {
            UnbindInput();
            return false;
        }
        input_bound_ = true;
        return true;
    }

    void PlayerController::UnbindInput()
    {
        if (input_context_ != nullptr)
        {
            for (const input::InputHandle handle : input_bindings_)
            {
                input_context_->UnBind(handle);
            }
        }
        input_bindings_.clear();
        input_context_.reset();
        input_bound_ = false;
        movement_input_ = {};
        gamepad_move_input_ = {};
        look_input_ = {};
        gamepad_look_input_ = {};
        zoom_input_ = 0.0f;
    }

    bool PlayerController::Possess(ActorHandle actor_handle)
    {
        Actor *const actor = world_ != nullptr ? world_->FindActor(actor_handle) : nullptr;
        CameraComponent *const camera = actor != nullptr ? actor->FindComponent<CameraComponent>()
                                                          : nullptr;
        if (actor == nullptr || camera == nullptr || actor->GetRootComponent() != camera)
        {
            return false;
        }
        if (actor->GetState() != ActorState::Active)
        {
            return false;
        }
        if (possessed_actor_ == actor_handle)
        {
            return input_system_ == nullptr || input_bound_ || BindInput();
        }

        Unpossess();
        if (input_system_ != nullptr && !BindInput())
        {
            return false;
        }
        possessed_actor_ = actor_handle;
        control_rotation_ = camera->GetWorldTransform().rotator_;
        control_rotation_.pitch_ = std::clamp(control_rotation_.pitch_, kPitchMin, kPitchMax);
        control_rotation_.yaw_ = WrapDegrees(control_rotation_.yaw_);
        control_rotation_.roll_ = 0.0f;
        return true;
    }

    void PlayerController::Unpossess()
    {
        UnbindInput();
        possessed_actor_ = {};
        movement_input_ = {};
        look_input_ = {};
        zoom_input_ = 0.0f;
    }

    void PlayerController::Tick(float delta_time)
    {
        ApplySnapshot(delta_time);
    }

    void PlayerController::QueueAction(const std::string &action_name,
                                       const input::InputState &state,
                                       input::InputActionSource source,
                                       uint8_t component_mask)
    {
        if (input_system_ != nullptr)
        {
            input_system_->EnqueueActionEvent(action_name, state, source, component_mask);
        }
    }

    void PlayerController::ApplySnapshot(float delta_time)
    {
        const bool input_enabled = input_enabled_.load(std::memory_order_acquire);
        if (input_system_ != nullptr)
        {
            const input::InputFrameSnapshot snapshot = input_system_->ConsumeFrameSnapshot();
            if (!input_enabled)
            {
                movement_input_ = {};
                gamepad_move_input_ = {};
                look_input_ = {};
                gamepad_look_input_ = {};
                zoom_input_ = 0.0f;
                return;
            }
            for (const input::InputActionEvent &event : snapshot.events)
            {
                if (event.action_name == kMoveAction &&
                    std::holds_alternative<Vector3f>(event.state.value))
                {
                    const Vector3f value = std::get<Vector3f>(event.state.value);
                    if (event.source == input::InputActionSource::Gamepad &&
                        event.state.triggle_type == input::InputTriggleType::Held)
                    {
                        if ((event.component_mask & input::kInputActionComponentX) != 0)
                        {
                            gamepad_move_input_.x_ = value.x_;
                        }
                        if ((event.component_mask & input::kInputActionComponentY) != 0)
                        {
                            gamepad_move_input_.y_ = value.y_;
                        }
                        if ((event.component_mask & input::kInputActionComponentZ) != 0)
                        {
                            gamepad_move_input_.z_ = value.z_;
                        }
                    }
                    else if (event.state.triggle_type == input::InputTriggleType::Pressed)
                    {
                        movement_input_ += value;
                    }
                    else if (event.state.triggle_type == input::InputTriggleType::Released)
                    {
                        movement_input_ -= value;
                    }
                }
                else if (event.action_name == kLookAction &&
                         std::holds_alternative<Vector2f>(event.state.value))
                {
                    if (event.source == input::InputActionSource::Gamepad)
                    {
                        gamepad_look_input_ = std::get<Vector2f>(event.state.value);
                    }
                    else
                    {
                        look_input_ += std::get<Vector2f>(event.state.value);
                    }
                }
                else if (event.action_name == kZoomAction &&
                         std::holds_alternative<float>(event.state.value))
                {
                    zoom_input_ += std::get<float>(event.state.value);
                }
            }
        }

        if (!input_enabled)
        {
            movement_input_ = {};
            gamepad_move_input_ = {};
            look_input_ = {};
            gamepad_look_input_ = {};
            zoom_input_ = 0.0f;
            return;
        }

        CameraComponent *const camera = FindPossessedCamera();
        if (camera == nullptr)
        {
            Unpossess();
            return;
        }
        ApplyLook(*camera, delta_time);
        ApplyMove(*camera, delta_time);
        if (zoom_input_ != 0.0f)
        {
            camera->SetFieldOfView(camera->GetFieldOfView() - zoom_input_ * zoom_sensitivity_);
        }
        look_input_ = {};
        zoom_input_ = 0.0f;
    }

    void PlayerController::ApplyLook(CameraComponent &camera, float delta_time)
    {
        if (look_input_.x_ == 0.0f && look_input_.y_ == 0.0f &&
            gamepad_look_input_.x_ == 0.0f && gamepad_look_input_.y_ == 0.0f)
        {
            return;
        }
        control_rotation_.yaw_ = WrapDegrees(
            control_rotation_.yaw_ + look_input_.x_ * look_sensitivity_ +
            gamepad_look_input_.x_ * gamepad_look_sensitivity_ * delta_time);
        control_rotation_.pitch_ = std::clamp(
            control_rotation_.pitch_ - look_input_.y_ * look_sensitivity_ -
                gamepad_look_input_.y_ * gamepad_look_sensitivity_ * delta_time,
            kPitchMin, kPitchMax);
        control_rotation_.roll_ = 0.0f;
        camera.SetLocalRotation(control_rotation_);
    }

    void PlayerController::ApplyMove(CameraComponent &camera, float delta_time)
    {
        Vector3f combined_input = movement_input_ + gamepad_move_input_;
        if (delta_time <= 0.0f || combined_input.SquareLength() == 0.0f)
        {
            return;
        }
        if (combined_input.SquareLength() > 1.0f)
        {
            combined_input = combined_input.GetSafetyNormalize();
        }
        const Vector3f direction = camera.GetForward() * combined_input.x_ +
                                   camera.GetRight() * combined_input.y_ +
                                   kWorldUp * combined_input.z_;
        camera.SetLocalLocation(camera.GetLocalLocation() +
                                direction * (move_speed_ * delta_time));
    }

    CameraComponent *PlayerController::FindPossessedCamera() const
    {
        Actor *const actor = world_ != nullptr ? world_->FindActor(possessed_actor_) : nullptr;
        return actor != nullptr ? actor->FindComponent<CameraComponent>() : nullptr;
    }

    bool PlayerController::BindMoveAction(const char *binding_name, input::InputKey key,
                                          const Vector3f &value)
    {
        auto action = std::make_shared<input::InputAction>();
        action->name_ = binding_name;
        action->value_type_ = input::InputValueType::Axis3D;
        action->default_value = value;
        action->callback_ = [this](const input::InputState &state)
        { QueueAction(kMoveAction, state, input::InputActionSource::Keyboard); };
        const input::InputHandle handle = input_context_->Bind(std::move(action), key);
        if (!handle.IsValid())
        {
            return false;
        }
        input_bindings_.push_back(handle);
        return true;
    }

    bool PlayerController::BindAxisAction(const char *binding_name, input::InputKey key,
                                          input::InputValueType value_type,
                                          const char *logical_name,
                                          input::InputActionSource source)
    {
        auto action = std::make_shared<input::InputAction>();
        action->name_ = binding_name;
        action->value_type_ = value_type;
        if (value_type == input::InputValueType::Axis2D)
        {
            action->default_value = Vector2f{};
        }
        else
        {
            action->default_value = 0.0f;
        }
        action->callback_ = [this, logical_name, source](const input::InputState &state)
        { QueueAction(logical_name, state, source); };
        const input::InputHandle handle = input_context_->Bind(std::move(action), key);
        if (!handle.IsValid())
        {
            return false;
        }
        input_bindings_.push_back(handle);
        return true;
    }

    bool PlayerController::BindGamepadStickAction(const char *binding_name, input::InputKey key,
                                                  bool is_move_action)
    {
        auto action = std::make_shared<input::InputAction>();
        action->name_ = binding_name;
        action->value_type_ = input::InputValueType::Axis2D;
        action->default_value = Vector2f{};
        action->callback_ = [this, is_move_action](const input::InputState &state)
        {
            if (!std::holds_alternative<Vector2f>(state.value))
            {
                return;
            }
            const Vector2f value = std::get<Vector2f>(state.value);
            if (is_move_action)
            {
                // GLFW's stick Y points up with a negative value. Convert it
                // to the camera's +forward convention; X remains strafe.
                QueueAction(kMoveAction,
                            {input::InputTriggleType::Held,
                             Vector3f{-value.y_, value.x_, 0.0f}},
                            input::InputActionSource::Gamepad,
                            input::kInputActionComponentX | input::kInputActionComponentY);
            }
            else
            {
                QueueAction(kLookAction, state, input::InputActionSource::Gamepad);
            }
        };
        const input::InputHandle handle = input_context_->Bind(std::move(action), key);
        if (!handle.IsValid())
        {
            return false;
        }
        input_bindings_.push_back(handle);
        return true;
    }

    bool PlayerController::BindGamepadTriggerAction(const char *binding_name, input::InputKey key,
                                                    float direction)
    {
        auto action = std::make_shared<input::InputAction>();
        action->name_ = binding_name;
        action->value_type_ = input::InputValueType::Axis1D;
        action->default_value = 0.0f;
        action->callback_ = [this, direction](const input::InputState &state)
        {
            if (!std::holds_alternative<float>(state.value))
            {
                return;
            }
            QueueAction(kMoveAction,
                        {input::InputTriggleType::Held,
                         Vector3f{0.0f, 0.0f, direction * std::get<float>(state.value)}},
                        input::InputActionSource::Gamepad,
                        input::kInputActionComponentZ);
        };
        const input::InputHandle handle = input_context_->Bind(std::move(action), key);
        if (!handle.IsValid())
        {
            return false;
        }
        input_bindings_.push_back(handle);
        return true;
    }
}
