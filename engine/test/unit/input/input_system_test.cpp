#include <memory>

#include <gtest/gtest.h>

#include "base/base.h"
#include "input/input_context.h"
#include "input/input_system.h"

namespace
{
    using kpengine::EventDispatcher;
    using kpengine::KeyEvent;
    using kpengine::input::InputDevice;
    using kpengine::input::InputSystem;

    constexpr int kKeyA = 65;
    constexpr int kPress = 1;
    constexpr int kRelease = 0;
}

TEST(InputSystemTest, InitializesAndDispatchesWindowKeysToListeners)
{
    InputSystem input_system;
    input_system.Initialize();
    EventDispatcher<KeyEvent> dispatcher;
    input_system.BindKeyEvent(dispatcher);

    int callback_count = 0;
    const InputSystem::KeyListenerHandle listener = input_system.AddKeyListener(
        [&callback_count](const KeyEvent &event)
        {
            EXPECT_EQ(event.key, kKeyA);
            ++callback_count;
        });
    ASSERT_NE(listener, 0U);

    dispatcher.Dispatch({kKeyA, 0, kPress, 0});
    EXPECT_EQ(callback_count, 1);

    input_system.RemoveKeyListener(listener);
    dispatcher.Dispatch({kKeyA, 0, kRelease, 0});
    EXPECT_EQ(callback_count, 1);
}

TEST(InputSystemTest, PreservesActiveContextProcessingAlongsideListeners)
{
    InputSystem input_system;
    input_system.Initialize();
    EventDispatcher<KeyEvent> dispatcher;
    input_system.BindKeyEvent(dispatcher);

    auto context = std::make_shared<kpengine::input::InputContext>();
    int action_count = 0;
    auto action = std::make_shared<kpengine::input::InputAction>();
    action->name_ = "test.action";
    action->value_type_ = kpengine::input::InputValueType::Bool;
    action->default_value = true;
    action->callback_ = [&action_count](const kpengine::input::InputState &state)
    {
        EXPECT_EQ(state.triggle_type, kpengine::input::InputTriggleType::Pressed);
        ++action_count;
    };
    context->Bind(action, {InputDevice::Keyboard, kKeyA});
    input_system.AddContext("test", context);
    input_system.SetActiveContext("test");

    int listener_count = 0;
    input_system.AddKeyListener([&listener_count](const KeyEvent &) { ++listener_count; });
    dispatcher.Dispatch({kKeyA, 0, kPress, 0});

    EXPECT_EQ(action_count, 1);
    EXPECT_EQ(listener_count, 1);
}

TEST(InputSystemTest, GatesActiveContextWithoutBlockingEditorKeyListeners)
{
    InputSystem input_system;
    input_system.Initialize();
    EventDispatcher<KeyEvent> dispatcher;
    input_system.BindKeyEvent(dispatcher);

    auto context = std::make_shared<kpengine::input::InputContext>();
    int action_count = 0;
    auto action = std::make_shared<kpengine::input::InputAction>();
    action->name_ = "camera.move";
    action->value_type_ = kpengine::input::InputValueType::Bool;
    action->default_value = true;
    action->callback_ = [&action_count](const kpengine::input::InputState &) { ++action_count; };
    context->Bind(action, {InputDevice::Keyboard, kKeyA});
    input_system.AddContext("Gameplay", context);
    input_system.SetActiveContext("Gameplay");

    int listener_count = 0;
    input_system.AddKeyListener([&listener_count](const KeyEvent &) { ++listener_count; });

    input_system.SetActiveContextEnabled(false);
    dispatcher.Dispatch({kKeyA, 0, kPress, 0});
    EXPECT_EQ(action_count, 0);
    EXPECT_EQ(listener_count, 1);

    input_system.SetActiveContextEnabled(true);
    dispatcher.Dispatch({kKeyA, 0, kPress, 0});
    EXPECT_EQ(action_count, 1);
    EXPECT_EQ(listener_count, 2);
}

TEST(InputSystemTest, ConsumesCopiedLogicalActionSnapshotOnce)
{
    InputSystem input_system;
    input_system.Initialize();

    kpengine::input::InputState look_state{
        kpengine::input::InputTriggleType::Held, kpengine::Vector2f{3.0f, -2.0f}};
    kpengine::input::InputState move_state{
        kpengine::input::InputTriggleType::Pressed, kpengine::Vector3f{1.0f, 0.0f, 0.0f}};
    input_system.EnqueueActionEvent("camera.look", look_state);
    input_system.EnqueueActionEvent("camera.move", move_state);

    const kpengine::input::InputFrameSnapshot snapshot = input_system.ConsumeFrameSnapshot();
    ASSERT_EQ(snapshot.events.size(), 2U);
    EXPECT_EQ(snapshot.events[0].action_name, "camera.look");
    EXPECT_EQ(snapshot.events[0].state.triggle_type,
              kpengine::input::InputTriggleType::Held);
    EXPECT_EQ(std::get<kpengine::Vector2f>(snapshot.events[0].state.value),
              (kpengine::Vector2f{3.0f, -2.0f}));
    EXPECT_EQ(snapshot.events[1].action_name, "camera.move");
    EXPECT_EQ(std::get<kpengine::Vector3f>(snapshot.events[1].state.value),
              (kpengine::Vector3f{1.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(input_system.ConsumeFrameSnapshot().events.empty());
}

TEST(InputSystemTest, TranslatesGamepadSamplesThroughRadialProcessing)
{
    InputSystem input_system;
    input_system.Initialize();
    EventDispatcher<kpengine::GamepadStateEvent> dispatcher;
    input_system.BindGamepadEvent(dispatcher);

    auto context = std::make_shared<kpengine::input::InputContext>();
    kpengine::Vector2f received{};
    auto action = std::make_shared<kpengine::input::InputAction>();
    action->name_ = "gamepad.look";
    action->value_type_ = kpengine::input::InputValueType::Axis2D;
    action->default_value = kpengine::Vector2f{};
    action->callback_ = [&received](const kpengine::input::InputState &state)
    {
        received = std::get<kpengine::Vector2f>(state.value);
    };
    context->Bind(action, {InputDevice::Gamepad,
                           static_cast<int>(kpengine::input::GamepadAxisCode::LeftStick)});
    input_system.AddContext("gameplay", context);
    input_system.SetActiveContext("gameplay");

    kpengine::GamepadStateEvent sample{};
    sample.gamepad_index = 0;
    sample.connected = true;
    sample.axes[0] = 0.1f;
    sample.axes[1] = 0.0f;
    dispatcher.Dispatch(sample);
    EXPECT_EQ(received, (kpengine::Vector2f{0.0f, 0.0f}));

    sample.axes[0] = 0.6f;
    dispatcher.Dispatch(sample);
    EXPECT_NEAR(received.x_, 0.5f, 0.0001f);
    EXPECT_FLOAT_EQ(received.y_, 0.0f);
}

TEST(InputSystemTest, AppliesGamepadStickDeadZoneInversionAndSensitivity)
{
    const kpengine::input::GamepadStickSettings settings{0.2f, 2.0f, true};
    EXPECT_EQ(kpengine::input::ApplyGamepadStickSettings({0.1f, 0.0f}, settings),
              (kpengine::Vector2f{0.0f, 0.0f}));
    const kpengine::Vector2f processed =
        kpengine::input::ApplyGamepadStickSettings({0.0f, 0.6f}, settings);
    EXPECT_NEAR(processed.x_, 0.0f, 0.0001f);
    EXPECT_NEAR(processed.y_, -1.0f, 0.0001f);
}
