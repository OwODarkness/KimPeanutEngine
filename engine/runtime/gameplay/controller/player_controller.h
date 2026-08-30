#ifndef KPENGINE_RUNTIME_GAMEPLAY_CONTROLLER_PLAYER_CONTROLLER_H
#define KPENGINE_RUNTIME_GAMEPLAY_CONTROLLER_PLAYER_CONTROLLER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gameplay/actor/actor_types.h"
#include "input/input_context.h"
#include "math/math_header.h"

namespace kpengine::input
{
    class InputSystem;
}

namespace kpengine::gameplay
{
    class CameraComponent;
    class GameplayWorld;

    // Local, non-replicated control bridge. It owns neither the possessed Actor
    // nor any render object; GameplayWorld remains the Actor owner.
    class PlayerController final
    {
    public:
        PlayerController(GameplayWorld &world, input::InputSystem *input_system,
                         std::string input_context_name);
        ~PlayerController();

        PlayerController(const PlayerController &) = delete;
        PlayerController &operator=(const PlayerController &) = delete;
        PlayerController(PlayerController &&) = delete;
        PlayerController &operator=(PlayerController &&) = delete;

        bool BindInput();
        void UnbindInput();

        bool Possess(ActorHandle actor_handle);
        void Unpossess();
        ActorHandle GetPossessedActor() const { return possessed_actor_; }
        const Rotatorf &GetControlRotation() const { return control_rotation_; }

        void SetMoveSpeed(float units_per_second) { move_speed_ = units_per_second; }
        void SetLookSensitivity(float degrees_per_pixel)
        {
            look_sensitivity_ = degrees_per_pixel;
        }
        void SetGamepadLookSensitivity(float degrees_per_second)
        {
            gamepad_look_sensitivity_ = degrees_per_second;
        }
        void SetZoomSensitivity(float degrees_per_scroll)
        {
            zoom_sensitivity_ = degrees_per_scroll;
        }

        void SetInputEnabled(bool enabled)
        {
            input_enabled_.store(enabled, std::memory_order_release);
        }

    private:
        void Tick(float delta_time);
        void QueueAction(const std::string &action_name, const input::InputState &state,
                         input::InputActionSource source,
                         uint8_t component_mask = input::kInputActionAllComponents);
        void ApplySnapshot(float delta_time);
        void ApplyLook(CameraComponent &camera, float delta_time);
        void ApplyMove(CameraComponent &camera, float delta_time);
        CameraComponent *FindPossessedCamera() const;
        bool BindMoveAction(const char *binding_name, input::InputKey key,
                            const Vector3f &value);
        bool BindAxisAction(const char *binding_name, input::InputKey key,
                            input::InputValueType value_type, const char *logical_name,
                            input::InputActionSource source);
        bool BindGamepadStickAction(const char *binding_name, input::InputKey key,
                                    bool is_move_action);
        bool BindGamepadTriggerAction(const char *binding_name, input::InputKey key,
                                      float direction);

        friend class GameplayWorld;

        GameplayWorld *world_ = nullptr;
        input::InputSystem *input_system_ = nullptr;
        std::string input_context_name_;
        std::shared_ptr<input::InputContext> input_context_;
        std::vector<input::InputHandle> input_bindings_;

        ActorHandle possessed_actor_;
        Rotatorf control_rotation_{};
        Vector3f movement_input_{};
        Vector3f gamepad_move_input_{};
        Vector2f look_input_{};
        Vector2f gamepad_look_input_{};
        float zoom_input_ = 0.0f;
        float move_speed_ = 100.0f;
        float look_sensitivity_ = 0.1f;
        float gamepad_look_sensitivity_ = 120.0f;
        float zoom_sensitivity_ = 2.0f;
        bool input_bound_ = false;
        std::atomic<bool> input_enabled_{true};
    };
}

#endif
