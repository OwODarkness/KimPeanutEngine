#ifndef KPENGINE_RUNTIME_BOOTSTRAP_H
#define KPENGINE_RUNTIME_BOOTSTRAP_H

#include <string>

namespace kpengine::bootstrap
{
    // Bootstrap V2 selects one Asset-root-relative LevelResource. The selected
    // level owns the complete dependency closure; bootstrap owns no manifest or
    // scene authoring data.
    struct BootstrapConfig
    {
        int version = 0;
        std::string startup_level;
    };

    // Parses and validates bootstrap V2. The returned startup_level is a
    // normalized Asset-root-relative path, e.g. level/pbr_showcase.level.
    // Throws std::runtime_error for missing, malformed, or invalid config.
    BootstrapConfig ReadBootstrap(const std::string &path);
}

#endif // KPENGINE_RUNTIME_BOOTSTRAP_H
