#ifndef KPENGINE_RUNTIME_HOST_APPLICATION_HOST_H
#define KPENGINE_RUNTIME_HOST_APPLICATION_HOST_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "asset/common.h"
#include "render/environment_source.h"

namespace kpengine
{
    class WindowSystem;
}

namespace kpengine::runtime
{
    class Engine;

    namespace command
    {
        class CommandRegistry;
    }

    enum class ApplicationHostLayout : std::uint8_t
    {
        SceneEditor,
        SharedWindowPresentation,
        OwnedWindow,
    };

    class IApplicationHost
    {
    public:
        virtual ~IApplicationHost() = default;

        virtual const char *Name() const noexcept = 0;
        virtual bool Initialize(Engine &engine, std::string &diagnostic) = 0;
        // Optional UI/presentation setup runs on Runtime's render thread after
        // the shared window and RenderSystem are ready.
        virtual bool InitializePresentation(Engine &engine,
                                            std::string &diagnostic)
        {
            (void)engine;
            (void)diagnostic;
            return true;
        }
        virtual bool Tick(float delta_time, std::string &diagnostic) = 0;
        virtual bool RecordFrame(std::string &diagnostic) = 0;
        // Runs inside Runtime's editor-composite pass when the host supplies UI.
        virtual bool RenderPresentation(std::string &diagnostic)
        {
            (void)diagnostic;
            return true;
        }
        virtual bool ShouldClose() const noexcept { return false; }
        // The window this host presents into, when it owns one. A host that
        // presents through Runtime's shared window returns nullptr, and callers
        // fall back to RuntimeContext::window_system_. The pointer is owned by
        // the host and may be null until the host has initialized on the render
        // thread, so resolve it at use rather than caching it.
        virtual WindowSystem *GetHostWindow() noexcept { return nullptr; }
        // Commands the host contributes beyond the ones Runtime itself
        // provides. Runtime cannot name the state a host owns -- the Live2D
        // viewer's loaded product, for one -- so the provider that reads that
        // state is registered from here. Called once while the registry is
        // created and before the agent transport starts, so a registration can
        // never race a dispatch. Returning false rejects the host's commands
        // without failing startup; the diagnostic is logged.
        virtual bool RegisterHostCommands(command::CommandRegistry &registry,
                                          std::string &diagnostic)
        {
            (void)registry;
            (void)diagnostic;
            return true;
        }
        // Roots for hosts that prepare renderable assets without a game Level.
        virtual std::vector<asset::AssetID> GetRenderAssetRoots() const { return {}; }
        virtual std::optional<render::EnvironmentSourceDesc>
        GetRenderEnvironmentSource() const { return std::nullopt; }
        // GPU/window teardown runs on the render thread. The default host has
        // no render-thread-only resources; Shutdown remains the final owner
        // teardown called after the Engine joins that thread.
        virtual void ShutdownRenderThread() noexcept {}
        virtual void Shutdown() noexcept = 0;
    };

    using ApplicationHostFactory =
        std::function<std::unique_ptr<IApplicationHost>(Engine &engine)>;

    struct ApplicationHostDescriptor
    {
        std::string id;
        ApplicationHostFactory factory;
        ApplicationHostLayout layout = ApplicationHostLayout::SceneEditor;
        bool ticks_gameplay_world = false;
    };

    class ApplicationHostRegistry final
    {
    public:
        ApplicationHostRegistry() = default;
        ApplicationHostRegistry(const ApplicationHostRegistry &) = delete;
        ApplicationHostRegistry &operator=(const ApplicationHostRegistry &) = delete;

        static ApplicationHostRegistry &Global();

        bool Register(ApplicationHostDescriptor descriptor,
                      std::string &diagnostic);
        bool Contains(std::string_view id) const;
        std::optional<ApplicationHostDescriptor> Find(std::string_view id) const;
        std::unique_ptr<IApplicationHost> Create(std::string_view id,
                                                  Engine &engine,
                                                  std::string &diagnostic) const;

    private:
        mutable std::mutex mutex_;
        std::map<std::string, ApplicationHostDescriptor, std::less<>> descriptors_;
    };

    class ApplicationHostRegistration final
    {
    public:
        explicit ApplicationHostRegistration(ApplicationHostDescriptor descriptor);
    };
}

#endif // KPENGINE_RUNTIME_HOST_APPLICATION_HOST_H
