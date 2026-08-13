#ifndef KPENGINE_RUNTIME_ASSET_ASSET_LOAD_REQUEST_H
#define KPENGINE_RUNTIME_ASSET_ASSET_LOAD_REQUEST_H

#include <cstdint>
#include <memory>
#include <string>
#include "asset/common.h"

namespace kpengine::asset
{
    using RequestID = uint64_t;

    // The lifecycle of an AssetLoadRequest as it travels between the loading
    // thread (producer) and the render thread (consumer). The expensive CPU
    // work (LoadSync + process) happens while a request is Processing; the
    // render thread only ever pops Ready items, so it never waits on a
    // compile or decode.
    enum class RequestState : uint8_t
    {
        Queued,     // enqueued, waiting for the loading thread
        Processing, // loading thread is running LoadSync + process
        Ready,      // CPU artifact done; safe for the render thread to bake
        Baked,      // render thread baked it; terminal, removed from the ready cache
        Failed,     // load or process failed; the drain skips it
    };

    // The unit of work the render module enqueues and the loading thread
    // fulfills. Lives in the Asset module because it is asset vocabulary
    // ("load this"), and the render module (the caller) already depends on
    // asset -- putting it here keeps core/async free of any asset dependency.
    // Deliberately type-agnostic: it carries a request, not a payload, so
    // adding an asset type (texture, mesh, ...) never changes this struct or
    // the queue contract. `payload` is attached only once Ready and pins the
    // artifact so it survives an asset unload between Ready and the drain.
    struct AssetLoadRequest
    {
        RequestID             request_id = 0;                            // caller's key into the render-side ready cache
        AssetType             type       = AssetType::Undefined;         // which processing/bake path applies
        std::string           path;                                      // load key (source path)
        AssetID               asset_id;                                  // resolved by the loading thread once loaded
        RequestState          state      = RequestState::Queued;         // guarded by the queue mutex while in flight
        std::shared_ptr<void> payload;                                   // pinned artifact once Ready; cast by `type`

        bool IsReady() const
        {
            return state == RequestState::Ready;
        }
    };
}

#endif
