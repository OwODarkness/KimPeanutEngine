#ifndef KPENGINE_RUNTIME_HOST_APPLICATION_HOST_H
#define KPENGINE_RUNTIME_HOST_APPLICATION_HOST_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

    enum class ApplicationMode : uint8_t
    {
        Scene3D,
        Live2DViewer,
    };

    const char *ApplicationModeName(ApplicationMode mode) noexcept;
    std::optional<ApplicationMode> ParseApplicationMode(std::string_view value) noexcept;

    class IApplicationHost
    {
    public:
        virtual ~IApplicationHost() = default;

        virtual const char *Name() const noexcept = 0;
        virtual bool Initialize(Engine &engine, std::string &diagnostic) = 0;
        virtual bool Tick(float delta_time, std::string &diagnostic) = 0;
        virtual bool RecordFrame(std::string &diagnostic) = 0;
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
        // GPU/window teardown runs on the render thread. The default host has
        // no render-thread-only resources; Shutdown remains the final owner
        // teardown called after the Engine joins that thread.
        virtual void ShutdownRenderThread() noexcept {}
        virtual void Shutdown() noexcept = 0;
    };

    using ApplicationHostFactory =
        std::function<std::unique_ptr<IApplicationHost>(Engine &engine)>;

    class ApplicationHostRegistry final
    {
    public:
        ApplicationHostRegistry() = default;

        ApplicationHostRegistry(const ApplicationHostRegistry &) = delete;
        ApplicationHostRegistry &operator=(const ApplicationHostRegistry &) = delete;
        ApplicationHostRegistry(ApplicationHostRegistry &&) noexcept = default;
        ApplicationHostRegistry &operator=(ApplicationHostRegistry &&) noexcept = default;

        bool Register(ApplicationMode mode,
                      ApplicationHostFactory factory,
                      std::string &diagnostic);
        bool Contains(ApplicationMode mode) const noexcept;
        std::unique_ptr<IApplicationHost> Create(ApplicationMode mode,
                                                  Engine &engine,
                                                  std::string &diagnostic) const;

    private:
        static constexpr std::size_t kModeCount = 2;

        static std::size_t ModeIndex(ApplicationMode mode) noexcept;

        std::array<ApplicationHostFactory, kModeCount> factories_{};
    };
}

#endif // KPENGINE_RUNTIME_HOST_APPLICATION_HOST_H
