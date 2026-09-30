#ifndef KPENGINE_LIVE2D_EMOTION_REPLAY_H
#define KPENGINE_LIVE2D_EMOTION_REPLAY_H

#include <string>
#include <vector>

#include "live2d_emotion_resolver.h"

namespace kpengine::live2d
{
    struct Live2DEmotionReplayResult final
    {
        bool available = false;
        bool passed = false;
        bool deterministic = false;
        std::string diagnostic;
        Live2DBehaviorSnapshot final_snapshot;
        std::vector<Live2DBehaviorSnapshot> step_snapshots;
        std::vector<Live2DBehaviorTransitionRecord> transition_history;
    };

    // Runs the viewer's fixed semantic replay against immutable Product data.
    // The result contains copied values only, so a viewer can inspect it without
    // retaining a resolver or any Cubism object.
    bool RunLive2DEmotionReplay(const Live2DProductData &product,
                                Live2DEmotionReplayResult &result,
                                std::string &diagnostic);
}

#endif
