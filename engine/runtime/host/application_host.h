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

namespace kpengine::runtime
{
    class Engine;

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
