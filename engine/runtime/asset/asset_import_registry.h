#ifndef KPENGINE_RUNTIME_ASSET_ASSET_IMPORT_REGISTRY_H
#define KPENGINE_RUNTIME_ASSET_ASSET_IMPORT_REGISTRY_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kpengine::asset
{
    enum class ImportProviderKind : std::uint8_t
    {
        Model,
        Material,
        Texture,
        Custom,
    };

    // This is deliberately independent from AssetRuntime and the archive
    // database. A provider may use the paths in the request to publish a
    // product, but the registry itself only selects and invokes providers.
    struct ImportProviderRequest
    {
        std::filesystem::path asset_root;
        std::filesystem::path archive_root;
        std::filesystem::path source_path;
    };

    class IImportProduct
    {
    public:
        virtual ~IImportProduct() = default;
        virtual ImportProviderKind GetProviderKind() const noexcept = 0;
    };

    using ImportProduct = std::shared_ptr<IImportProduct>;

    template <typename T, ImportProviderKind Kind>
    class TypedImportProduct final : public IImportProduct
    {
    public:
        explicit TypedImportProduct(T value) : value(std::move(value))
        {
        }

        ImportProviderKind GetProviderKind() const noexcept override
        {
            return Kind;
        }

        T value;
    };

    enum class ImportProviderStatus : std::uint8_t
    {
        Imported,
        UpToDate,
        Cooked,
    };

    struct ImportProviderResult
    {
        ImportProviderStatus status{ImportProviderStatus::Imported};
        std::string provider_id;
        std::string diagnostic;
        ImportProduct product;
    };

    using ImportProviderCallback =
        std::function<ImportProviderResult(const ImportProviderRequest &request)>;

    struct ImportProviderDescriptor
    {
        std::string id;
        std::uint32_t version{};
        ImportProviderKind kind{ImportProviderKind::Custom};
        std::vector<std::string> source_suffixes;
        ImportProviderCallback callback;
    };

    class ImportProviderRegistry final
    {
    public:
        bool Register(ImportProviderDescriptor descriptor, std::string &diagnostic);
        bool Seal(std::string &diagnostic);

        bool IsSealed() const noexcept { return sealed_; }
        const ImportProviderDescriptor *FindById(std::string_view id) const noexcept;

        // An explicit provider id bypasses suffix matching. Automatic
        // selection chooses the longest matching suffix; equal-length matches
        // are rejected as ambiguous instead of depending on registration
        // order.
        const ImportProviderDescriptor *Resolve(
            const std::filesystem::path &source_path,
            std::string_view explicit_provider_id,
            std::string &diagnostic) const noexcept;

        ImportProviderResult Execute(const ImportProviderRequest &request,
                                     std::string_view explicit_provider_id,
                                     std::string &diagnostic) const;

    private:
        static std::string NormalizeSuffix(std::string_view suffix);
        static std::string NormalizeProviderId(std::string_view id);
        static bool MatchesSuffix(std::string_view filename, std::string_view suffix) noexcept;
        static bool IsValidDescriptor(const ImportProviderDescriptor &descriptor,
                                      std::string &diagnostic);

        std::unordered_map<std::string, ImportProviderDescriptor> descriptors_;
        bool sealed_{false};
    };
}

#endif
