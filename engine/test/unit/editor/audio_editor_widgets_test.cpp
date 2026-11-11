#include "module/audio/editor/audio_transport_controls.h"
#include "editor_ui/ui_recording_backend.h"
#include "editor_ui/ui_session.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

namespace kpengine::audio_editor
{
    namespace
    {
        using namespace editor::ui;

        class PointerBackend final : public IUiBackend
        {
        public:
            bool Initialize(const UiViewportInfo &) override { return true; }
            void BeginFrame(const UiFrameInfo &) override {}
            bool BeginWindow(const UiControl &) override { return true; }
            void EndWindow() override {}
            void SetHostContainer(const UiControl &) override {}
            UiEditResult Control(const UiControl &control) override { return {control.value}; }
            void Text(std::string_view) override {}
            UiVec2 AvailableContent() const noexcept override { return {800.0F, 400.0F}; }
            UiCanvasResult Canvas(const UiControl &control, UiVec2,
                                  std::span<const UiCanvasPrimitive>) override
            {
                enabled = control.enabled;
                return {.pressed = true, .normalized_pointer = pointer};
            }
            UiFrameResult EndFrame(const std::uint64_t frame) override { return {frame, true}; }
            void AbortFrame() noexcept override {}
            void Shutdown() noexcept override {}
            UiVec2 pointer{325.0F / 800.0F, 0.5F};
            bool enabled = true;
        };
    }

    TEST(AudioEditorWidgetsTest, SeparateInstancesReturnOnlyTheirOwnPlaybackIntents)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        auto *recording = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        auto &ui = session.Context();
        ui.BeginWindow({.id = 1, .label = "Audio"});
        recording->QueueInput(ui.SessionId(), ui.FrameId(), StableUiNodeId("music.toggle"),
                              false, UiActionKind::Activate);
        recording->QueueInput(ui.SessionId(), ui.FrameId(), StableUiNodeId("speech.cancel"),
                              false, UiActionKind::Activate);
        TransportStripState music{.instance_id = "music"};
        music.capabilities.toggle_play_pause = true;
        TransportStripState speech{.instance_id = "speech"};
        speech.capabilities.cancel_job = true;
        editor::EditorBuiltinIcons icons;
        const auto first = DrawTransportStrip(ui, music, icons);
        const auto second = DrawTransportStrip(ui, speech, icons);
        EXPECT_TRUE(first.toggle_play_pause);
        EXPECT_FALSE(first.cancel_job);
        EXPECT_TRUE(second.cancel_job);
        EXPECT_FALSE(second.toggle_play_pause);
        ui.EndWindow();
        EXPECT_TRUE(session.EndFrame().complete);
    }

    TEST(AudioEditorWidgetsTest, PointerSeekRequiresKnownDurationAndEnabledCapability)
    {
        auto backend = std::make_unique<PointerBackend>();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        auto &ui = session.Context();
        ui.BeginWindow({.id = 1, .label = "Audio"});
        TransportStripState state{.instance_id = "seekable", .duration_seconds = 100.0F};
        state.capabilities.seek = true;
        editor::EditorBuiltinIcons icons;
        const auto seek = DrawTransportStrip(ui, state, icons).seek_seconds;
        ASSERT_TRUE(seek.has_value());
        EXPECT_NEAR(*seek, 50.0F, 0.001F);
        state.instance_id = "disabled";
        state.controls_enabled = false;
        EXPECT_FALSE(DrawTransportStrip(ui, state, icons).seek_seconds.has_value());
        state.instance_id = "streaming";
        state.controls_enabled = true;
        state.duration_seconds.reset();
        EXPECT_FALSE(DrawTransportStrip(ui, state, icons).seek_seconds.has_value());
        ui.EndWindow();
        EXPECT_TRUE(session.EndFrame().complete);
    }

    TEST(AudioEditorWidgetsTest, VolumeClampsPointerAndSanitizesNonfiniteValues)
    {
        auto backend = std::make_unique<PointerBackend>();
        auto *pointer = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        auto &ui = session.Context();
        ui.BeginWindow({.id = 1, .label = "Audio"});
        pointer->pointer.x = 2.0F;
        const auto volume = DrawAudioVolumeProgressControl(ui, "volume.high",
            std::numeric_limits<float>::quiet_NaN(), 100.0F);
        ASSERT_TRUE(volume.has_value());
        EXPECT_FLOAT_EQ(*volume, 1.0F);
        pointer->pointer.x = -1.0F;
        EXPECT_FLOAT_EQ(*DrawAudioVolumeProgressControl(ui, "volume.low", 0.5F, 100.0F), 0.0F);
        ui.EndWindow();
        EXPECT_TRUE(session.EndFrame().complete);
    }
}
