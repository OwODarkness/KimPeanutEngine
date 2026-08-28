#include "bootstrap/bootstrap.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <nlohmann/json.hpp>
#include "asset/utility.h"
#include "log/logger.h"
#include "config/path.h"

namespace kpengine::bootstrap
{
    namespace
    {
        constexpr int kBootstrapVersion = 1;

        // Session-wide request id counter. The bootstrap flow itself runs exactly
        // once (guarded by Engine::PreloadBootstrap), but the ready cache is keyed
        // by request_id and will later also be fed by the render module's runtime
        // requests, so ids must stay unique across the whole run — not just within
        // one bootstrap batch. BuildLoadRequests is called from the engine at boot
        // and from unit tests (different processes), so a plain counter is enough.
        std::uint64_t g_next_request_id = 1;

        // Minimal whole-file read. Throws std::runtime_error if the file is missing
        // so ReadBootstrap can fail fast at boot (surfaced by main's try/catch).
        std::string ReadText(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw std::runtime_error("bootstrap: failed to open " + path);
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    BootstrapConfig ReadBootstrap(const std::string &path)
    {
        // ReadText throws std::runtime_error if the file is missing.
        nlohmann::json json = nlohmann::json::parse(ReadText(path));

        BootstrapConfig config;
        config.version = json.value("version", kBootstrapVersion);
        if (config.version != kBootstrapVersion)
        {
            KP_LOG("BootstrapLog", LOG_LEVEL_WARNING,
                   "%s uses bootstrap version %d, engine supports %d",
                   path.c_str(), config.version, kBootstrapVersion);
        }

        if (!json.contains("assets"))
        {
            return config;
        }
        if (!json["assets"].is_array())
        {
            KP_LOG("BootstrapLog", LOG_LEVEL_WARNING,
                   "%s: \"assets\" is not an array, ignoring", path.c_str());
            return config;
        }

        for (const auto &item : json["assets"])
        {
            if (item.is_string())
            {
                config.assets.push_back(item.get<std::string>());
            }
            else
            {
                KP_LOG("BootstrapLog", LOG_LEVEL_WARNING,
                       "%s: ignoring non-string bootstrap asset entry", path.c_str());
            }
        }

        if (json.contains("scene") && json["scene"].is_object())
        {
            const auto &scene = json["scene"];
            config.scene.model = scene.value("model", std::string{});
            config.scene.material = scene.value("material", std::string{});
            if (!config.scene.IsComplete())
            {
                KP_LOG("BootstrapLog", LOG_LEVEL_WARNING,
                       "%s: ignoring incomplete bootstrap scene", path.c_str());
                config.scene = {};
            }
        }

        return config;
    }

    std::vector<asset::AssetLoadRequest> BuildLoadRequests(const BootstrapConfig &config)
    {
        std::vector<asset::AssetLoadRequest> requests;
        requests.reserve(config.assets.size());

        // Dedup within the batch: a need-list entry listed twice must not enqueue
        // two requests for the same asset (the loading thread would load it twice,
        // even though AssetManager's path_index would dedup the actual disk work).
        std::unordered_set<std::string> seen;
        seen.reserve(config.assets.size());

        for (const auto &path : config.assets)
        {
            const asset::AssetType type = asset::ExtractAssetType(asset::GetFileExtension(path));
            if (type == asset::AssetType::Undefined)
            {
                KP_LOG("BootstrapLog", LOG_LEVEL_WARNING,
                       "bootstrap: skipping \"%s\" (unknown asset extension)", path.c_str());
                continue;
            }
            if (!seen.insert(path).second)
            {

                KP_LOG("BootstrapLog", LOG_LEVEL_WARNING,
                       "bootstrap: skipping \"%s\" (duplicate need-list entry)", path.c_str());
                continue;
            }

            std::string abs_path = GetAssetDirectory() + path;

            asset::AssetLoadRequest request;
            request.request_id = g_next_request_id++;
            request.type = type;
            request.path = abs_path;
            request.state = asset::RequestState::Queued;
            requests.push_back(std::move(request));
        }

        return requests;
    }
}
