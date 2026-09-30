#include "live2d_emotion_runtime.h"

#include <utility>

namespace kpengine::live2d
{
    bool Live2DEmotionRuntime::RunReplay(const Live2DProductData &product,
                                         std::string &diagnostic)
    {
        return RunLive2DEmotionReplay(product, replay_, diagnostic);
    }
}
