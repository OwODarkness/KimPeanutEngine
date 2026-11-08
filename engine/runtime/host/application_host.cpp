#include "host/application_host.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace kpengine::runtime::application
{
    void RegisterBuiltInHosts(ApplicationHostRegistry &registry);
}

namespace kpengine::runtime
{
    ApplicationHostRegistry &ApplicationHostRegistry::Global()
    {
        static ApplicationHostRegistry registry;
        static const bool built_ins_registered = []
        {
            application::RegisterBuiltInHosts(registry);
            return true;
        }();
        (void)built_ins_registered;
        return registry;
    }

    bool ApplicationHostRegistry::Register(ApplicationHostDescriptor descriptor,
                                           std::string &diagnostic)
    {
        diagnostic.clear();
        if (descriptor.id.empty())
        {
            diagnostic = "application host id cannot be empty";
            return false;
        }
        if (!descriptor.factory)
        {
            diagnostic = "application host factory for '" + descriptor.id + "' is empty";
            return false;
        }

        const std::lock_guard lock(mutex_);
        const std::string id = descriptor.id;
        const auto [entry, inserted] = descriptors_.emplace(id, std::move(descriptor));
        if (!inserted)
        {
            diagnostic = "application host for '" + entry->first + "' is already registered";
            return false;
        }
        return true;
    }

    bool ApplicationHostRegistry::Contains(const std::string_view id) const
    {
        const std::lock_guard lock(mutex_);
        return descriptors_.find(id) != descriptors_.end();
    }

    std::optional<ApplicationHostDescriptor> ApplicationHostRegistry::Find(
        const std::string_view id) const
    {
        const std::lock_guard lock(mutex_);
        const auto entry = descriptors_.find(id);
        if (entry == descriptors_.end())
        {
            return std::nullopt;
        }
        return entry->second;
    }

    std::unique_ptr<IApplicationHost> ApplicationHostRegistry::Create(
        const std::string_view id,
        Engine &engine,
        std::string &diagnostic) const
    {
        diagnostic.clear();
        const std::optional<ApplicationHostDescriptor> descriptor = Find(id);
        if (!descriptor.has_value())
        {
            diagnostic = "no application host is registered for '" + std::string{id} + "'";
            return nullptr;
        }

        try
        {
            std::unique_ptr<IApplicationHost> host = descriptor->factory(engine);
            if (!host)
            {
                diagnostic = "application host factory for '" + descriptor->id +
                             "' returned null";
            }
            return host;
        }
        catch (const std::exception &error)
        {
            diagnostic = "application host factory for '" + descriptor->id +
                         "' threw: " + error.what();
            return nullptr;
        }
        catch (...)
        {
            diagnostic = "application host factory for '" + descriptor->id +
                         "' threw an unknown exception";
            return nullptr;
        }
    }

    ApplicationHostRegistration::ApplicationHostRegistration(
        ApplicationHostDescriptor descriptor)
    {
        std::string diagnostic;
        if (!ApplicationHostRegistry::Global().Register(std::move(descriptor), diagnostic))
        {
            throw std::runtime_error("Application host registration failed: " + diagnostic);
        }
    }

}
