#ifndef KPENGINE_RUNTIME_BOOTSTRAP_H
#define KPENGINE_RUNTIME_BOOTSTRAP_H

#include <array>
#include <string>
#include <vector>

#include "asset/asset_load_request.h"

namespace kpengine::bootstrap
{
    struct BootstrapSceneObject
    {
        std::string model;
        std::string material;
        std::array<float, 3> position{0.f, 0.f, 0.f};
        std::array<float, 3> rotation{0.f, 0.f, 0.f};
        std::array<float, 3> scale{1.f, 1.f, 1.f};

        bool IsComplete() const
        {
            return !model.empty() && !material.empty();
        }
    };

    struct BootstrapScene
    {
        std::string model;
        std::string material;
        std::array<float, 3> position{0.f, 0.f, 0.f};
        std::array<float, 3> rotation{0.f, 0.f, 0.f};
        std::array<float, 3> scale{1.f, 1.f, 1.f};
        std::vector<BootstrapSceneObject> objects;

        bool IsComplete() const
        {
            return !model.empty() && !material.empty();
        }
    };

    // The engine's startup need-list: the assets to preload before the main loop
    // (docs/status.md item 6). Mirrors config/bootstrap.json.
    //
    // This module is deliberately engine-scoped. Only RuntimeLib (the engine, via
    // Engine::PreloadBootstrap) links it — render, editor, graphics and the rest
    // of the engine do not, so the preload flow is not callable outside the
    // engine. It lives out of core/resource on purpose: the bootstrap is the
    // engine's startup concern, not a resource-processing API.
    struct BootstrapConfig
    {
        int version = 0;                 // bootstrap.json schema version
        std::vector<std::string> assets; // asset paths, e.g. "shader/simple_triangle.shader"
        BootstrapScene scene;
    };

    // Parses a bootstrap.json (GetBootstrapPath()) into a BootstrapConfig.
    // Throws std::runtime_error if the file is missing, and propagates
    // nlohmann::json::parse_error on malformed JSON.
    BootstrapConfig ReadBootstrap(const std::string &path);

    // The "load" leg of the bootstrap preload flow (docs/status.md item 6): one
    // Queued AssetLoadRequest per need-list entry, `type` sniffed from the file
    // extension via asset::ExtractAssetType. Entries whose extension maps to
    // AssetType::Undefined, and duplicate entries, are skipped (logged). Request
    // ids come from a session-wide counter so the render-side ready cache can key
    // on them. Only the engine calls this (Engine::PreloadBootstrap).
    std::vector<asset::AssetLoadRequest> BuildLoadRequests(const BootstrapConfig &config);
}

#endif // KPENGINE_RUNTIME_BOOTSTRAP_H
