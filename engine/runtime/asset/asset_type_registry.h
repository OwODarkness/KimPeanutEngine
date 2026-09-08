#ifndef KPENGINE_RUNTIME_ASSET_ASSET_TYPE_REGISTRY_H
#define KPENGINE_RUNTIME_ASSET_ASSET_TYPE_REGISTRY_H

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "asset.h"

namespace kpengine::asset
{
    using AssetLoaderCallback =
        std::function<bool(const std::string &path, AssetRegisterInfo &info)>;

    struct AssetTypeDescriptor
    {
        AssetType type = AssetType::Undefined;
        std::string name;
        std::vector<std::string> extensions;
        AssetLoaderCallback loader;
    };

    class AssetTypeRegistry final
    {
    public:
        bool Register(AssetTypeDescriptor descriptor, std::string &diagnostic);
        bool Seal(std::string &diagnostic);

        bool IsSealed() const noexcept { return sealed_; }
        const AssetTypeDescriptor *FindByType(AssetType type) const noexcept;
        const AssetTypeDescriptor *FindByExtension(std::string_view extension) const noexcept;

    private:
        static std::string NormalizeExtension(std::string_view extension);
        static bool IsValidDescriptor(const AssetTypeDescriptor &descriptor,
                                      std::string &diagnostic);

        std::unordered_map<AssetType, AssetTypeDescriptor> descriptors_;
        std::unordered_map<std::string, AssetType> extension_index_;
        bool sealed_ = false;
    };
}

#endif
