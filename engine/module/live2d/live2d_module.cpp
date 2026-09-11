#include "live2d_module.h"

#include "log/log_system.h"
#include "log/logger.h"
#include "asset/asset_manager.h"

#include "config/path.h"
#include "runtime/runtime_global_context.h"
#include "runtime/render/render_system.h"
#include "live2d_settings.h"
#include "module/live2d/runtime/live2d_model_resource.h"

namespace kpengine::live2d
{
    void Live2DModule::OnRegister(kpengine::runtime::Engine &engine)
    {
        engine_ = &engine;
        registration_succeeded_ =
            RegisterLive2DAssetTypes(asset::AssetManager::GetInstance(),
                                     registration_diagnostic_);
        if (!registration_succeeded_)
        {
            KP_LOG("Live2DModule", LOG_LEVEL_ERROR,
                   "Live2D Asset registration failed: %s",
                   registration_diagnostic_.c_str());
        }
    }

    bool Live2DModule::Initialize(kpengine::runtime::Engine &engine)
    {
        engine_ = &engine;
        if (!registration_succeeded_)
        {
            KP_LOG("Live2DModule", LOG_LEVEL_ERROR,
                   "Live2D module cannot initialize: %s",
                   registration_diagnostic_.c_str());
            return false;
        }
        if (system_.Initialize())
        {
            Live2DSettings settings{};
            try
            {
                settings = ReadLive2DSettings(
                    ComposePath(project_root, "config/live2d.json"));
            }
            catch (const std::exception &error)
            {
                registration_diagnostic_ = error.what();
                KP_LOG("Live2DModule", LOG_LEVEL_ERROR,
                       "Live2D settings failed to load: %s",
                       registration_diagnostic_.c_str());
                system_.Shutdown();
                return false;
            }
            if (!settings.enabled)
            {
                KP_LOG("Live2DModule", LOG_LEVEL_INFO,
                       "Live2D preview disabled by configuration");
                return true;
            }

            const std::string product_path = GetAssetDirectory() + settings.preview_asset;
            model_asset_ = asset::AssetManager::GetInstance().LoadSync(product_path);
            if (!model_asset_.IsValid() || model_asset_.type != kLive2DModelAssetType)
            {
                registration_diagnostic_ =
                    "Configured Live2D product could not be loaded: " + product_path;
                KP_LOG("Live2DModule", LOG_LEVEL_ERROR,
                       "Live2D preview asset failed to load: %s",
                       registration_diagnostic_.c_str());
                system_.Shutdown();
                return false;
            }
            renderer_ = std::make_unique<Live2DRenderer>(system_, model_asset_);
            if (runtime::global_runtime_context.render_system_ == nullptr ||
                !runtime::global_runtime_context.render_system_->RegisterRenderExtension(
                    renderer_.get()))
            {
                registration_diagnostic_ =
                    "RenderSystem rejected the Live2D renderer extension";
                renderer_.reset();
                model_asset_ = {};
                system_.Shutdown();
                return false;
            }
            return true;
        }

        KP_LOG("Live2DModule", LOG_LEVEL_ERROR,
               "Cubism framework initialization failed");
        return false;
    }

    void Live2DModule::Tick(float delta_time)
    {
        system_.Tick(delta_time);
    }

    void Live2DModule::Shutdown() noexcept
    {
        if (runtime::global_runtime_context.render_system_ != nullptr && renderer_)
        {
            runtime::global_runtime_context.render_system_->UnregisterRenderExtension(
                renderer_.get());
        }
        renderer_.reset();
        if (model_asset_.IsValid())
        {
            model_asset_ = {};
        }
        system_.Shutdown();
        engine_ = nullptr;
    }
}
