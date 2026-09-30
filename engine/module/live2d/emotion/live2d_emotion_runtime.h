#ifndef KPENGINE_LIVE2D_EMOTION_RUNTIME_H
#define KPENGINE_LIVE2D_EMOTION_RUNTIME_H

#include <string>
#include <string_view>

#include "live2d_emotion_controller.h"
#include "live2d_emotion_replay.h"

namespace kpengine::live2d
{
    // Runtime owner for semantic emotion state. The viewer/editor supplies
    // narrow actions; policy, orchestration, and replay evidence stay below the
    // UI boundary and can later be driven by a decision-model provider.
    class Live2DEmotionRuntime final
    {
    public:
        bool Apply(std::string_view intent, const Live2DEmotionActions &actions)
        {
            return orchestrator_.Apply(intent, actions);
        }

        bool RunReplay(const Live2DProductData &product, std::string &diagnostic);

        const std::string &Status() const noexcept { return orchestrator_.Status(); }
        const std::string &Diagnostic() const noexcept
        {
            return orchestrator_.Diagnostic();
        }
        const Live2DEmotionReplayResult &Replay() const noexcept { return replay_; }

    private:
        Live2DEmotionOrchestrator orchestrator_;
        Live2DEmotionReplayResult replay_{};
    };
}

#endif
