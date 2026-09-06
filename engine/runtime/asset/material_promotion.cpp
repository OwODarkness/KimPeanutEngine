#include "material_promotion.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "model_archive.h"

namespace kpengine::asset
{
    namespace
    {
        [[noreturn]] void Fail(MaterialPromotionErrorCode code, const std::string &message)
        {
            throw MaterialPromotionError(code, message);
        }

        std::filesystem::path AbsoluteNormalized(const std::filesystem::path &path)
        {
            if (path.empty())
            {
                Fail(MaterialPromotionErrorCode::InvalidArgument,
                     "material promotion requires a non-empty path");
            }
            std::error_code error;
            const std::filesystem::path absolute = std::filesystem::absolute(path, error);
            if (error)
            {
                Fail(MaterialPromotionErrorCode::InvalidArgument,
                     "failed to resolve material promotion path: " + error.message());
            }
            return absolute.lexically_normal();
        }

        std::vector<std::byte> ReadBytes(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                Fail(MaterialPromotionErrorCode::IoError,
                     "failed to open generated Material: " + path.string());
            }
            const std::vector<char> characters{std::istreambuf_iterator<char>(file),
                                               std::istreambuf_iterator<char>()};
            std::vector<std::byte> bytes(characters.size());
            std::transform(characters.begin(), characters.end(), bytes.begin(),
                           [](char value)
                           {
                               return static_cast<std::byte>(
                                   static_cast<unsigned char>(value));
                           });
            return bytes;
        }

        void WriteBytes(const std::filesystem::path &path,
                        const std::vector<std::byte> &bytes)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                Fail(MaterialPromotionErrorCode::IoError,
                     "failed to create authored Material: " + path.string());
            }
            file.write(reinterpret_cast<const char *>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            if (!file.good())
            {
                Fail(MaterialPromotionErrorCode::IoError,
                     "failed to write authored Material: " + path.string());
            }
        }

        bool IsArchivePath(const std::string &normalized_path)
        {
            return normalized_path == ".archive" ||
                   normalized_path.rfind(".archive/", 0) == 0;
        }

        const ProductRecord *FindMaterialProduct(
            const SourceArchiveSnapshot &snapshot, std::uint32_t slot)
        {
            for (const SourceProductRecord &source_product : snapshot.source_products)
            {
                if (source_product.asset_type != ArchiveProductType::Material ||
                    source_product.slot != static_cast<std::int32_t>(slot))
                {
                    continue;
                }
                const auto product = std::find_if(
                    snapshot.products.begin(), snapshot.products.end(),
                    [&source_product](const ProductRecord &candidate)
                    {
                        return candidate.asset_type == ArchiveProductType::Material &&
                               candidate.content_hash == source_product.content_hash;
                    });
                if (product != snapshot.products.end())
                {
                    return &*product;
                }
            }
            return nullptr;
        }

        std::string RebaseMaterialPath(const std::string &value,
                                       const std::filesystem::path &source_directory,
                                       const std::filesystem::path &target_directory,
                                       const std::filesystem::path &asset_root)
        {
            const std::filesystem::path resolved =
                (source_directory / std::filesystem::path{value}).lexically_normal();
            const std::filesystem::path rebased = resolved.lexically_relative(target_directory);
            const std::string result = rebased.generic_string();
            const std::filesystem::path relative_to_asset = resolved.lexically_relative(asset_root);
            const std::string asset_relative = relative_to_asset.generic_string();
            if (rebased.empty() || rebased.is_absolute() || result == "." ||
                relative_to_asset.empty() || relative_to_asset.is_absolute() ||
                asset_relative == ".." || asset_relative.rfind("../", 0) == 0)
            {
                Fail(MaterialPromotionErrorCode::InvalidArgument,
                     "generated Material reference cannot be rebased: " + value);
            }
            return result;
        }

        std::vector<std::byte> RebaseMaterialBytes(
            const std::vector<std::byte> &bytes,
            const std::filesystem::path &source_directory,
            const std::filesystem::path &target_directory,
            const std::filesystem::path &asset_root)
        {
            try
            {
                const std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                nlohmann::json material = nlohmann::json::parse(text);
                material.at("shader") = RebaseMaterialPath(
                    material.at("shader").get<std::string>(), source_directory, target_directory,
                    asset_root);
                for (auto &[name, value] : material.at("parameters").items())
                {
                    (void)name;
                    if (value.is_string())
                    {
                        value = RebaseMaterialPath(value.get<std::string>(), source_directory,
                                                   target_directory, asset_root);
                    }
                    else if (value.is_object() && value.contains("path"))
                    {
                        value.at("path") = RebaseMaterialPath(
                            value.at("path").get<std::string>(), source_directory,
                            target_directory, asset_root);
                    }
                }
                const std::string rebased_text = material.dump();
                return {reinterpret_cast<const std::byte *>(rebased_text.data()),
                        reinterpret_cast<const std::byte *>(rebased_text.data() + rebased_text.size())};
            }
            catch (const nlohmann::json::exception &error)
            {
                Fail(MaterialPromotionErrorCode::ProductMissing,
                     std::string{"generated Material JSON is invalid: "} + error.what());
            }
        }
    }

    MaterialPromotionError::MaterialPromotionError(MaterialPromotionErrorCode code,
                                                     std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }

    MaterialPromotionErrorCode MaterialPromotionError::Code() const noexcept
    {
        return code_;
    }

    MaterialPromotionResult PromoteGeneratedMaterial(
        const MaterialPromotionRequest &request)
    {
        const std::filesystem::path asset_root = AbsoluteNormalized(request.asset_root);
        const std::filesystem::path archive_root = request.archive_root.empty()
                                                        ? asset_root / ".archive"
                                                        : AbsoluteNormalized(request.archive_root);
        if (request.logical_model_path.empty() || request.authored_material_path.empty())
        {
            Fail(MaterialPromotionErrorCode::InvalidArgument,
                 "material promotion requires a logical Model and authored Material path");
        }

        std::string normalized_authored_path;
        try
        {
            normalized_authored_path =
                NormalizeAssetRelativePath(request.authored_material_path.generic_string());
        }
        catch (const ModelArchiveError &error)
        {
            Fail(MaterialPromotionErrorCode::InvalidArgument, error.what());
        }
        if (IsArchivePath(normalized_authored_path) ||
            std::filesystem::path(normalized_authored_path).extension() != ".material")
        {
            Fail(MaterialPromotionErrorCode::InvalidArgument,
                 "authored Material must be a non-archive .material path");
        }

        ModelArchiveDatabase archive{archive_root / "archive.sqlite3"};
        const std::optional<SourceArchiveSnapshot> snapshot =
            archive.FindSourceByLogicalPath(request.logical_model_path);
        if (!snapshot.has_value())
        {
            Fail(MaterialPromotionErrorCode::SourceNotFound,
                 "logical Model is not present in the archive: " + request.logical_model_path);
        }
        const ProductRecord *product = FindMaterialProduct(*snapshot, request.material_slot);
        if (!product)
        {
            Fail(MaterialPromotionErrorCode::ProductMissing,
                 "no generated Material exists for slot " +
                     std::to_string(request.material_slot));
        }

        const std::filesystem::path product_path = archive_root / product->relative_path;
        const std::vector<std::byte> generated_bytes = ReadBytes(product_path);
        std::string diagnostic;
        if (!VerifyArchiveProduct(product_path, ArchiveProductType::Material, generated_bytes,
                                   diagnostic, archive_root))
        {
            Fail(MaterialPromotionErrorCode::ProductMissing, diagnostic);
        }

        const std::filesystem::path authored_path = asset_root / normalized_authored_path;
        const std::vector<std::byte> authored_bytes = RebaseMaterialBytes(
            generated_bytes, product_path.parent_path(), authored_path.parent_path(), asset_root);
        std::error_code error;
        const bool authored_exists = std::filesystem::is_regular_file(authored_path, error);
        if (error)
        {
            Fail(MaterialPromotionErrorCode::IoError,
                 "failed to inspect authored Material: " + error.message());
        }
        if (authored_exists)
        {
            const std::vector<std::byte> existing = ReadBytes(authored_path);
            if (existing != authored_bytes)
            {
                Fail(MaterialPromotionErrorCode::Collision,
                     "authored Material already exists with different bytes: " +
                         authored_path.string());
            }
        }
        else
        {
            std::filesystem::create_directories(authored_path.parent_path(), error);
            if (error)
            {
                Fail(MaterialPromotionErrorCode::IoError,
                     "failed to create authored Material directory: " + error.message());
            }
            const std::filesystem::path temporary_path = authored_path.string() + ".promotion.tmp";
            WriteBytes(temporary_path, authored_bytes);
            std::filesystem::rename(temporary_path, authored_path, error);
            if (error)
            {
                std::filesystem::remove(temporary_path);
                Fail(MaterialPromotionErrorCode::IoError,
                     "failed to publish authored Material: " + error.message());
            }
        }

        std::vector<MaterialOverrideRecord> overrides = snapshot->material_overrides;
        const auto existing_override = std::find_if(
            overrides.begin(), overrides.end(),
            [&request](const MaterialOverrideRecord &override_record)
            { return override_record.slot == static_cast<std::int32_t>(request.material_slot); });
        if (existing_override != overrides.end())
        {
            existing_override->authored_path = normalized_authored_path;
        }
        else
        {
            overrides.push_back({static_cast<std::int32_t>(request.material_slot),
                                 normalized_authored_path});
        }
        std::sort(overrides.begin(), overrides.end(),
                  [](const MaterialOverrideRecord &lhs, const MaterialOverrideRecord &rhs)
                  { return lhs.slot < rhs.slot; });

        try
        {
            archive.ReplaceSource(snapshot->source, snapshot->dependencies,
                                  snapshot->products, snapshot->source_products, overrides);
        }
        catch (const ModelArchiveError &archive_error)
        {
            if (!authored_exists)
            {
                std::error_code cleanup_error;
                std::filesystem::remove(authored_path, cleanup_error);
            }
            Fail(MaterialPromotionErrorCode::ArchiveCommitFailed, archive_error.what());
        }

        return {product->content_hash, authored_path, !authored_exists};
    }
}
