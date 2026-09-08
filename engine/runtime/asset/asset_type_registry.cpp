#include "asset_type_registry.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace kpengine::asset
{
    std::string AssetTypeRegistry::NormalizeExtension(std::string_view extension)
    {
        std::string normalized(extension);
        while (!normalized.empty() && normalized.front() == '.')
        {
            normalized.erase(normalized.begin());
        }
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        if (normalized.empty() || normalized.find_first_of("./\\") != std::string::npos)
        {
            return {};
        }
        return normalized;
    }

    bool AssetTypeRegistry::IsValidDescriptor(const AssetTypeDescriptor &descriptor,
                                              std::string &diagnostic)
    {
        if (!IsAssetTypeValueInExtensionRange(descriptor.type))
        {
            diagnostic = "asset type is outside the built-in and custom extension ranges";
            return false;
        }
        if (descriptor.name.empty())
        {
            diagnostic = "asset type descriptor name is empty";
            return false;
        }
        if (!descriptor.loader)
        {
            diagnostic = "asset type descriptor loader is empty";
            return false;
        }
        if (descriptor.extensions.empty() && !IsBuiltInAssetType(descriptor.type))
        {
            diagnostic = "custom asset type descriptor has no native extensions";
            return false;
        }

        std::unordered_set<std::string> extensions;
        for (const std::string &extension : descriptor.extensions)
        {
            const std::string normalized = NormalizeExtension(extension);
            if (normalized.empty() || !extensions.insert(normalized).second)
            {
                diagnostic = "asset type descriptor contains an invalid or duplicate extension";
                return false;
            }
        }
        return true;
    }

    bool AssetTypeRegistry::Register(AssetTypeDescriptor descriptor,
                                     std::string &diagnostic)
    {
        diagnostic.clear();
        if (sealed_)
        {
            diagnostic = "asset type registry is already sealed";
            return false;
        }
        if (!IsValidDescriptor(descriptor, diagnostic))
        {
            return false;
        }
        if (descriptors_.find(descriptor.type) != descriptors_.end())
        {
            diagnostic = "asset type is already registered";
            return false;
        }

        for (const auto &[registered_type, registered] : descriptors_)
        {
            (void)registered_type;
            if (registered.name == descriptor.name)
            {
                diagnostic = "asset type descriptor name is already registered";
                return false;
            }
        }

        std::vector<std::string> normalized_extensions;
        normalized_extensions.reserve(descriptor.extensions.size());
        for (const std::string &extension : descriptor.extensions)
        {
            const std::string normalized = NormalizeExtension(extension);
            if (extension_index_.find(normalized) != extension_index_.end())
            {
                diagnostic = "asset type descriptor extension is already registered";
                return false;
            }
            normalized_extensions.push_back(normalized);
        }

        descriptor.extensions = std::move(normalized_extensions);
        const AssetType type = descriptor.type;
        descriptors_.emplace(type, std::move(descriptor));
        for (const std::string &extension : descriptors_.at(type).extensions)
        {
            extension_index_.emplace(extension, type);
        }
        return true;
    }

    bool AssetTypeRegistry::Seal(std::string &diagnostic)
    {
        diagnostic.clear();
        if (sealed_)
        {
            return true;
        }
        if (descriptors_.empty())
        {
            diagnostic = "asset type registry has no descriptors";
            return false;
        }
        sealed_ = true;
        return true;
    }

    const AssetTypeDescriptor *AssetTypeRegistry::FindByType(AssetType type) const noexcept
    {
        const auto iterator = descriptors_.find(type);
        return iterator == descriptors_.end() ? nullptr : &iterator->second;
    }

    const AssetTypeDescriptor *AssetTypeRegistry::FindByExtension(
        std::string_view extension) const noexcept
    {
        const std::string normalized = NormalizeExtension(extension);
        const auto extension_iterator = extension_index_.find(normalized);
        if (extension_iterator == extension_index_.end())
        {
            return nullptr;
        }
        return FindByType(extension_iterator->second);
    }
}
