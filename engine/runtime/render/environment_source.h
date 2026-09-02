#ifndef KPENGINE_RUNTIME_RENDER_ENVIRONMENT_SOURCE_H
#define KPENGINE_RUNTIME_RENDER_ENVIRONMENT_SOURCE_H

#include <cstdint>
#include <optional>

#include "asset/common.h"
#include "base/handle.h"

namespace kpengine::render
{
    struct EnvironmentSourceTag
    {
    };

    // Opaque Runtime-owned registration identity. It never identifies a GPU
    // texture, derived IBL artifact, or backend object.
    using EnvironmentSourceHandle = Handle<EnvironmentSourceTag>;

    // Value-only level environment input. Render validates the referenced
    // AssetID and owns all panorama/IBL resolution after the frame boundary.
    struct EnvironmentSourceDesc
    {
        asset::AssetID texture_asset;
        float ibl_intensity = 0.25f;
    };

    bool IsEnvironmentSourceDescValid(const EnvironmentSourceDesc &source);

    class IEnvironmentSourceSink
    {
    public:
        virtual ~IEnvironmentSourceSink() = default;

        virtual EnvironmentSourceHandle EnqueueCreate(const EnvironmentSourceDesc &source) = 0;
        virtual bool EnqueueDestroy(EnvironmentSourceHandle handle) = 0;
    };
}

#endif
