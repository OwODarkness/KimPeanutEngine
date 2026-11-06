#ifndef KPENGINE_RUNTIME_RENDER_TEMPORAL_AA_SETTINGS_H
#define KPENGINE_RUNTIME_RENDER_TEMPORAL_AA_SETTINGS_H

namespace kpengine::render
{
    struct TemporalAaSettings
    {
        bool enabled = true;
        float history_weight = 0.9f;
    };
}

#endif
