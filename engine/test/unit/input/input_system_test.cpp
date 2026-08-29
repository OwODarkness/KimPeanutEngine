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
