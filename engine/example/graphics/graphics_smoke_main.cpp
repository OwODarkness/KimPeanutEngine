#include <cstdint>
#include <cstdio>

#include "graphics_example.h"

int main()
{
    constexpr uint32_t kSmokeFramesPerAPI = 3;
    const bool succeeded = kpengine::example::RunGraphicsSmokeSuite(kSmokeFramesPerAPI);
    std::printf("Graphics smoke (%u frames/API): %s\n", kSmokeFramesPerAPI,
                succeeded ? "passed" : "failed");
    return succeeded ? 0 : 1;
}
