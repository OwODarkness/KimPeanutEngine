#include "host/application_host.h"

#include <exception>
#include <utility>

namespace kpengine::runtime
{
    namespace
    {
        constexpr std::size_t InvalidModeIndex() noexcept
        {
            return 2;
        }
    }

    const char *ApplicationModeName(const ApplicationMode mode) noexcept
    {
        switch (mode)
        {
        case ApplicationMode::Scene3D:
            return "scene3d";
        case ApplicationMode::Live2DViewer:
            return "live2d-viewer";
        }
        return "unknown";
    }

    std::optional<ApplicationMode> ParseApplicationMode(
        const std::string_view value) noexcept
    {
        if (value == "scene3d")
        {
            return ApplicationMode::Scene3D;
        }
        if (value == "live2d-viewer")
        {
            return ApplicationMode::Live2DViewer;
        }
        return std::nullopt;
    }

    std::size_t ApplicationHostRegistry::ModeIndex(const ApplicationMode mode) noexcept
    {
        switch (mode)
        {
        case ApplicationMode::Scene3D:
            return 0;
        case ApplicationMode::Live2DViewer:
            return 1;
        }
        return InvalidModeIndex();
    }

    bool ApplicationHostRegistry::Register(const ApplicationMode mode,
                                            ApplicationHostFactory factory,
                                            std::string &diagnostic)
    {
        diagnostic.clear();
        const std::size_t index = ModeIndex(mode);
        if (index >= factories_.size())
        {
            diagnostic = "cannot register an unknown application mode";
            return false;
        }
        if (!factory)
        {
            diagnostic = std::string("application host provider for '") +
                         ApplicationModeName(mode) + "' is empty";
            return false;
        }
        if (factories_[index])
        {
            diagnostic = std::string("application host provider for '") +
                         ApplicationModeName(mode) + "' is already registered";
            return false;
        }

        factories_[index] = std::move(factory);
        return true;
    }

    bool ApplicationHostRegistry::Contains(const ApplicationMode mode) const noexcept
    {
        const std::size_t index = ModeIndex(mode);
        return index < factories_.size() && static_cast<bool>(factories_[index]);
    }

    std::unique_ptr<IApplicationHost> ApplicationHostRegistry::Create(
        const ApplicationMode mode,
        Engine &engine,
        std::string &diagnostic) const
    {
        diagnostic.clear();
        const std::size_t index = ModeIndex(mode);
        if (index >= factories_.size() || !factories_[index])
        {
            diagnostic = std::string("no application host provider is registered for '") +
                         ApplicationModeName(mode) + "'";
            return nullptr;
        }

        try
        {
            std::unique_ptr<IApplicationHost> host = factories_[index](engine);
            if (!host)
            {
                diagnostic = std::string("application host provider for '") +
                             ApplicationModeName(mode) + "' returned null";
            }
            return host;
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("application host provider for '") +
                         ApplicationModeName(mode) + "' threw: " + error.what();
            return nullptr;
        }
        catch (...)
        {
            diagnostic = std::string("application host provider for '") +
                         ApplicationModeName(mode) + "' threw an unknown exception";
            return nullptr;
        }
    }
}
