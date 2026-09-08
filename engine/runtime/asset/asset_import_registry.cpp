#include "asset_import_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace kpengine::asset
{
    namespace
    {
        std::string Lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });
            return value;
        }
    }

    std::string ImportProviderRegistry::NormalizeSuffix(std::string_view suffix)
    {
        std::string normalized(suffix);
        while (!normalized.empty() && normalized.front() == '.')
        {
            normalized.erase(normalized.begin());
        }
        normalized = Lowercase(std::move(normalized));
        if (normalized.empty() || normalized.front() == '.' || normalized.back() == '.' ||
            normalized.find_first_of("/\\") != std::string::npos)
        {
            return {};
        }
        return normalized;
    }

    std::string ImportProviderRegistry::NormalizeProviderId(std::string_view id)
    {
        std::string normalized(id);
        if (normalized.empty())
        {
            return {};
        }
        for (const unsigned char character : normalized)
        {
            if (std::isspace(character) != 0 || character == '/' || character == '\\')
            {
                return {};
            }
        }
        return normalized;
    }

    bool ImportProviderRegistry::MatchesSuffix(std::string_view filename,
                                               std::string_view suffix) noexcept
    {
        if (filename == suffix)
        {
            return true;
        }
        if (filename.size() <= suffix.size())
        {
            return false;
        }
        const std::size_t offset = filename.size() - suffix.size();
        return filename[offset - 1] == '.' && filename.substr(offset) == suffix;
    }

    bool ImportProviderRegistry::IsValidDescriptor(
        const ImportProviderDescriptor &descriptor, std::string &diagnostic)
    {
        if (NormalizeProviderId(descriptor.id).empty())
        {
            diagnostic = "import provider id is empty or contains whitespace/path separators";
            return false;
        }
        if (descriptor.version == 0)
        {
            diagnostic = "import provider version must be non-zero";
            return false;
        }
        if (!descriptor.callback)
        {
            diagnostic = "import provider callback is empty";
            return false;
        }

        std::vector<std::string> normalized_suffixes;
        normalized_suffixes.reserve(descriptor.source_suffixes.size());
        for (const std::string &suffix : descriptor.source_suffixes)
        {
            const std::string normalized = NormalizeSuffix(suffix);
            if (normalized.empty() ||
                std::find(normalized_suffixes.begin(), normalized_suffixes.end(), normalized) !=
                    normalized_suffixes.end())
            {
                diagnostic = "import provider contains an invalid or duplicate source suffix";
                return false;
            }
            normalized_suffixes.push_back(normalized);
        }
        return true;
    }

    bool ImportProviderRegistry::Register(ImportProviderDescriptor descriptor,
                                          std::string &diagnostic)
    {
        diagnostic.clear();
        if (sealed_)
        {
            diagnostic = "import provider registry is already sealed";
            return false;
        }
        if (!IsValidDescriptor(descriptor, diagnostic))
        {
            return false;
        }

        const std::string normalized_id = NormalizeProviderId(descriptor.id);
        if (descriptors_.find(normalized_id) != descriptors_.end())
        {
            diagnostic = "import provider id is already registered";
            return false;
        }

        for (std::string &suffix : descriptor.source_suffixes)
        {
            suffix = NormalizeSuffix(suffix);
        }
        descriptor.id = normalized_id;
        descriptors_.emplace(descriptor.id, std::move(descriptor));
        return true;
    }

    bool ImportProviderRegistry::Seal(std::string &diagnostic)
    {
        diagnostic.clear();
        if (sealed_)
        {
            return true;
        }
        if (descriptors_.empty())
        {
            diagnostic = "import provider registry has no providers";
            return false;
        }
        sealed_ = true;
        return true;
    }

    const ImportProviderDescriptor *ImportProviderRegistry::FindById(
        std::string_view id) const noexcept
    {
        const std::string normalized = NormalizeProviderId(id);
        if (normalized.empty())
        {
            return nullptr;
        }
        const auto iterator = descriptors_.find(normalized);
        return iterator == descriptors_.end() ? nullptr : &iterator->second;
    }

    const ImportProviderDescriptor *ImportProviderRegistry::Resolve(
        const std::filesystem::path &source_path,
        std::string_view explicit_provider_id,
        std::string &diagnostic) const noexcept
    {
        diagnostic.clear();
        if (!explicit_provider_id.empty())
        {
            const ImportProviderDescriptor *const descriptor = FindById(explicit_provider_id);
            if (descriptor == nullptr)
            {
                diagnostic = "explicit import provider is not registered";
            }
            return descriptor;
        }

        const std::string filename = Lowercase(source_path.filename().generic_string());
        if (filename.empty())
        {
            diagnostic = "source path has no filename for automatic provider selection";
            return nullptr;
        }

        const ImportProviderDescriptor *selected = nullptr;
        std::size_t selected_suffix_length = 0;
        for (const auto &[id, descriptor] : descriptors_)
        {
            (void)id;
            for (const std::string &suffix : descriptor.source_suffixes)
            {
                if (!MatchesSuffix(filename, suffix))
                {
                    continue;
                }
                if (suffix.size() > selected_suffix_length)
                {
                    selected = &descriptor;
                    selected_suffix_length = suffix.size();
                }
                else if (suffix.size() == selected_suffix_length && selected != &descriptor)
                {
                    diagnostic = "automatic import provider selection is ambiguous";
                    return nullptr;
                }
            }
        }

        if (selected == nullptr)
        {
            diagnostic = "no import provider matches the source path";
        }
        return selected;
    }

    ImportProviderResult ImportProviderRegistry::Execute(
        const ImportProviderRequest &request,
        std::string_view explicit_provider_id,
        std::string &diagnostic) const
    {
        diagnostic.clear();
        const ImportProviderDescriptor *const descriptor =
            Resolve(request.source_path, explicit_provider_id, diagnostic);
        if (descriptor == nullptr)
        {
            return {};
        }

        // Provider code is intentionally called after selection and outside
        // registry mutation. The registry has no database or Asset lock to
        // hold across a potentially long decode/publication operation.
        ImportProviderResult result = descriptor->callback(request);
        result.provider_id = descriptor->id;
        return result;
    }
}
