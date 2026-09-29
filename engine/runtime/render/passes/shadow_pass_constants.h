#ifndef KPENGINE_RUNTIME_RENDER_PASSES_SHADOW_PASS_CONSTANTS_H
#define KPENGINE_RUNTIME_RENDER_PASSES_SHADOW_PASS_CONSTANTS_H

#include <cstdint>

namespace kpengine::render::shadow_pass_detail
{
    inline constexpr uint32_t kPointShadowFaceResolution = 512;
    inline constexpr uint64_t kDirectionalPerPassUniformKey = 0x534841444f575f44ull;
    inline constexpr uint64_t kSpotPerPassUniformKey = 0x534841444f575f53ull;
    inline constexpr uint64_t kPointPerPassUniformKey = 0x534841444f575f50ull;
}

#endif
