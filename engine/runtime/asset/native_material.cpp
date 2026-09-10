#include "native_material.h"

#include "texture_importer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>
#include <cctype>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include <nlohmann/json.hpp>

namespace kpengine::asset
{
    namespace
    {
        using image_io::ImageBuffer;

        [[noreturn]] void Fail(NativeMaterialErrorCode code, const std::string &message)
        {
            throw NativeMaterialConversionError(code, message);
        }

        bool IsFiniteNonNegative(float value)
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        bool IsUnit(float value)
        {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        }

        std::string FloatText(float value)
        {
            if (!std::isfinite(value))
            {
                Fail(NativeMaterialErrorCode::InvalidValue, "material contains a non-finite number");
            }
            if (value == 0.0f)
            {
                value = 0.0f;
            }
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream << std::setprecision(9) << std::defaultfloat << value;
            return stream.str();
        }

        std::string JsonString(std::string_view value)
        {
            std::string result;
            result.push_back('"');
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (character < 0x20U)
                    {
                        std::ostringstream escaped;
                        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                << static_cast<unsigned int>(character);
                        result += escaped.str();
                    }
                    else
                    {
                        result.push_back(static_cast<char>(character));
                    }
                    break;
                }
            }
            result.push_back('"');
            return result;
        }

        std::string VectorText(const Vector4f &value)
        {
            std::string result{"["};
            for (std::size_t index = 0; index < 4; ++index)
            {
                if (index != 0) result += ",";
                result += FloatText(value[index]);
            }
            result += "]";
            return result;
        }

        const ImportedImageSource *FindImage(const ImportedModelDocument &document,
                                             const std::string &reference)
        {
            for (const ImportedImageSource &image : document.images)
            {
                if (image.path == reference)
                {
                    return &image;
                }
            }
            return nullptr;
        }

        struct ImageReference
        {
            std::string path;
            std::string block_compressed_path;
            std::string extension;
        };

        struct TextureCookKey
        {
            std::string source_identity;
            ContentHash source_hash{};
            data::TextureSemantic semantic{data::TextureSemantic::Generic};
            std::uint32_t max_dimension{};
            std::uint32_t max_levels{};
            TextureCompressionPolicy compression{TextureCompressionPolicy::Portable};
            TextureBcEncoder bc_encoder{TextureBcEncoder::ReferenceV1};
            TextureBcQuality bc_quality{TextureBcQuality::Balanced};
            bool emit_texture_profile_variants{false};

            friend bool operator<(const TextureCookKey &lhs, const TextureCookKey &rhs) noexcept
            {
                return std::tie(lhs.source_identity, lhs.source_hash, lhs.semantic,
                                 lhs.max_dimension, lhs.max_levels, lhs.compression,
                                 lhs.bc_encoder, lhs.bc_quality,
                                 lhs.emit_texture_profile_variants) <
                       std::tie(rhs.source_identity, rhs.source_hash, rhs.semantic,
                                rhs.max_dimension, rhs.max_levels, rhs.compression,
                                rhs.bc_encoder, rhs.bc_quality,
                                rhs.emit_texture_profile_variants);
            }
        };

        bool IsZeroHash(const ContentHash &hash)
        {
            return hash.bytes == ModelArchiveHashBytes{};
        }

        std::string TextureSourceIdentity(const ImportedImageSource &image)
        {
            if (image.storage == ImportedImageStorage::ExternalFile)
            {
                return image.resolved_path.lexically_normal().generic_string();
            }
            return "embedded:" + image.path + ":" + image.format_hint + ":" +
                   std::to_string(image.embedded_width) + "x" +
                   std::to_string(image.embedded_height) + ":raw=" +
                   std::to_string(image.embedded_is_raw_rgba8 ? 1 : 0);
        }

        TextureCookKey MakeTextureCookKey(const ImportedImageSource &image,
                                          const NativeMaterialConversionSettings &settings,
                                          data::TextureSemantic semantic)
        {
            TextureCookKey key{};
            key.source_identity = TextureSourceIdentity(image);
            key.source_hash = image.source_hash;
            key.semantic = semantic;
            key.max_dimension = settings.texture_settings.max_dimension;
            key.max_levels = settings.texture_settings.max_levels;
            key.compression = settings.texture_settings.compression;
            key.bc_encoder = settings.texture_settings.bc_encoder;
            key.bc_quality = settings.texture_settings.bc_quality;
            key.emit_texture_profile_variants = settings.emit_texture_profile_variants;
            if (image.storage == ImportedImageStorage::EmbeddedBytes && IsZeroHash(key.source_hash))
            {
                key.source_hash = Sha256(image.embedded_bytes);
            }
            return key;
        }

        void ValidateMaterialSource(const ImportedMaterialSource &source)
        {
            if (!IsUnit(source.base_color[0]) || !IsUnit(source.base_color[1]) ||
                !IsUnit(source.base_color[2]) || !IsUnit(source.base_color[3]) ||
                !IsUnit(source.metallic) || !IsUnit(source.roughness) ||
                !IsFiniteNonNegative(source.normal_scale) || !IsUnit(source.occlusion_strength) ||
                !IsFiniteNonNegative(source.alpha_cutoff) || source.alpha_cutoff > 1.0f)
            {
                Fail(NativeMaterialErrorCode::InvalidValue, "imported material contains an invalid value");
            }
            if (!source.emissive_texture.empty())
            {
                Fail(NativeMaterialErrorCode::UnsupportedSemantics,
                     "emissive textures are unsupported until the G-buffer carries emissive output");
            }
        }

        ImportedTexture DecodeTextureSource(const ImportedImageSource &image,
                                            const NativeMaterialConversionSettings &settings,
                                            data::TextureSemantic semantic,
                                            NativeMaterialConversionMetrics &metrics)
        {
            ++metrics.texture_decode_count;
            if (image.storage == ImportedImageStorage::ExternalFile)
            {
                if (image.resolved_path.empty())
                {
                    Fail(NativeMaterialErrorCode::MissingImage,
                         "external material image has no resolved path: " + image.path);
                }
                const TextureCompressionPolicy import_compression =
                    settings.emit_texture_profile_variants
                        ? TextureCompressionPolicy::Portable
                        : settings.texture_settings.compression;
                try
                {
                    return TextureImporter{}.Import(
                        {image.resolved_path,
                         {semantic, settings.texture_settings.max_dimension,
                          settings.texture_settings.max_levels, import_compression}});
                }
                catch (const TextureCookError &error)
                {
                    Fail(NativeMaterialErrorCode::MalformedImage,
                         "external image could not be cooked: " + image.resolved_path.string() + ": " +
                             error.what());
                }
            }

            ImageBuffer decoded;
            if (image.embedded_is_raw_rgba8)
            {
                decoded.width = image.embedded_width;
                decoded.height = image.embedded_height;
                decoded.format = image_io::ImagePixelFormat::Rgba8;
                decoded.pixels.assign(image.embedded_bytes.size(), 0);
                if (decoded.ExpectedByteCount() == 0 ||
                    decoded.ExpectedByteCount() != image.embedded_bytes.size())
                {
                    Fail(NativeMaterialErrorCode::MalformedImage,
                         "embedded raw image has an invalid extent or byte count: " + image.path);
                }
                std::copy(image.embedded_bytes.begin(), image.embedded_bytes.end(),
                          reinterpret_cast<std::byte *>(decoded.pixels.data()));
            }
            else
            {
                const image_io::ImageDecodeResult decoded_result =
                    image_io::DecodeImageMemory(image.embedded_bytes);
                if (!decoded_result.result.success)
                {
                    Fail(NativeMaterialErrorCode::MalformedImage,
                         "embedded image could not be decoded: " + image.path + ": " +
                             decoded_result.result.diagnostic);
                }
                decoded = decoded_result.image;
            }

            ImportedTexture imported{};
            imported.image = std::move(decoded);
            imported.source_hash = image.source_hash;
            imported.settings = settings.texture_settings;
            imported.settings.semantic = semantic;
            return imported;
        }

        std::string PublishTextureProduct(CookedTexture cooked,
                                          NativeMaterialConversionResult &result)
        {
            const auto existing = std::find_if(
                result.embedded_images.begin(), result.embedded_images.end(),
                [&cooked](const NativeImageProduct &product)
                {
                    return product.content_hash == cooked.product_hash;
                });
            if (existing == result.embedded_images.end())
            {
                const std::size_t product_bytes = cooked.bytes.size();
                result.embedded_images.push_back(
                    {cooked.product_hash, "texture", std::move(cooked.bytes)});
                ++result.metrics.unique_texture_product_count;
                result.metrics.texture_product_bytes += product_bytes;
            }
            return "../" + ProductRelativePath(ArchiveProductType::Texture,
                                                 cooked.product_hash, "texture");
        }

        std::pair<CookedTexture, std::optional<CookedTexture>> CookTextureProfiles(
            const data::TextureData &prepared,
            const NativeMaterialConversionSettings &settings,
            NativeMaterialConversionMetrics &metrics)
        {
            TextureCooker cooker;
            const auto cook_profile = [&](TextureCompressionPolicy compression)
            {
                ++metrics.texture_cook_count;
                if (compression == TextureCompressionPolicy::Portable)
                {
                    ++metrics.portable_encode_count;
                }
                else
                {
                    ++metrics.block_encode_count;
                }
                return cooker.CookPrepared(prepared, compression,
                                           settings.texture_settings.bc_encoder,
                                           settings.texture_settings.bc_quality,
                                           &metrics.bc_encoding,
                                           settings.texture_cancellation_query);
            };
            if (!settings.emit_texture_profile_variants)
            {
                return {cook_profile(settings.texture_settings.compression), std::nullopt};
            }

            CookedTexture portable = cook_profile(TextureCompressionPolicy::Portable);
            // Native material publication needs serialized bytes and hashes,
            // not the duplicate CPU mip chain retained by CookedTexture.
            portable.data = {};
            CookedTexture block = cook_profile(TextureCompressionPolicy::PreferBlockCompression);
            block.data = {};
            if (block.product_hash == portable.product_hash)
            {
                return {std::move(portable), std::nullopt};
            }
            return {std::move(portable), std::move(block)};
        }

        ImageReference CookPreparedImage(
            ImportedTexture imported, TextureCookKey key,
            const NativeMaterialConversionSettings &settings,
            NativeMaterialConversionResult &result,
            std::map<TextureCookKey, ImageReference> &cooked_images)
        {
            try
            {
                const auto cooked = [&]
                {
                    const data::TextureData prepared =
                        TextureCooker{}.Prepare(std::move(imported));
                    ++result.metrics.texture_prepare_count;
                    return CookTextureProfiles(prepared, settings, result.metrics);
                }();
                const std::string portable_path =
                    PublishTextureProduct(std::move(cooked.first), result);
                const std::string block_path = cooked.second.has_value()
                                                   ? PublishTextureProduct(
                                                         std::move(*cooked.second), result)
                                                   : std::string{};
                const ImageReference reference{portable_path, block_path, "texture"};
                cooked_images.emplace(std::move(key), reference);
                return reference;
            }
            catch (const TextureCookError &error)
            {
                if (error.Code() == TextureCookErrorCode::Cancelled)
                {
                    Fail(NativeMaterialErrorCode::Cancelled, error.what());
                }
                Fail(NativeMaterialErrorCode::MalformedImage,
                     "image could not be cooked: " + std::string{error.what()});
            }
            return {};
        }

        ImageReference PrepareImage(const ImportedImageSource &image,
                                    const NativeMaterialConversionSettings &settings,
                                    data::TextureSemantic semantic,
                                    NativeMaterialConversionResult &result,
                                    std::map<TextureCookKey, ImageReference> &cooked_images)
        {
            TextureCookKey key{};
            key.source_identity = TextureSourceIdentity(image);
            key.source_hash = image.source_hash;
            key.semantic = semantic;
            key.max_dimension = settings.texture_settings.max_dimension;
            key.max_levels = settings.texture_settings.max_levels;
            key.compression = settings.texture_settings.compression;
            key.bc_encoder = settings.texture_settings.bc_encoder;
            key.bc_quality = settings.texture_settings.bc_quality;
            key.emit_texture_profile_variants = settings.emit_texture_profile_variants;
            if (image.storage == ImportedImageStorage::EmbeddedBytes && IsZeroHash(key.source_hash))
            {
                key.source_hash = Sha256(image.embedded_bytes);
            }
            if (const auto existing = cooked_images.find(key); existing != cooked_images.end())
            {
                return existing->second;
            }
            if (settings.texture_progress_callback)
            {
                settings.texture_progress_callback(image.path);
            }
            ImageBuffer decoded;
            ++result.metrics.texture_decode_count;
            if (image.storage == ImportedImageStorage::EmbeddedBytes)
            {
                if (image.embedded_is_raw_rgba8)
                {
                    decoded.width = image.embedded_width;
                    decoded.height = image.embedded_height;
                    decoded.format = image_io::ImagePixelFormat::Rgba8;
                    decoded.pixels.assign(image.embedded_bytes.size(), 0);
                    if (decoded.ExpectedByteCount() == 0 ||
                        decoded.ExpectedByteCount() != image.embedded_bytes.size())
                    {
                        Fail(NativeMaterialErrorCode::MalformedImage,
                             "embedded raw image has an invalid extent or byte count: " + image.path);
                    }
                    std::copy(image.embedded_bytes.begin(), image.embedded_bytes.end(),
                              reinterpret_cast<std::byte *>(decoded.pixels.data()));
                }
                else
                {
                    const image_io::ImageDecodeResult decoded_result =
                        image_io::DecodeImageMemory(image.embedded_bytes);
                    if (!decoded_result.result.success)
                    {
                        Fail(NativeMaterialErrorCode::MalformedImage,
                             "embedded image could not be decoded: " + image.path + ": " +
                                 decoded_result.result.diagnostic);
                    }
                    decoded = decoded_result.image;
                }
            }
            else
            {
                if (image.resolved_path.empty())
                {
                    Fail(NativeMaterialErrorCode::MissingImage,
                         "external material image has no resolved path: " + image.path);
                }
                const TextureCompressionPolicy import_compression =
                    settings.emit_texture_profile_variants
                        ? TextureCompressionPolicy::Portable
                        : settings.texture_settings.compression;
                ImportedTexture imported{};
                try
                {
                    imported = TextureImporter{}.Import(
                        {image.resolved_path,
                         {semantic, settings.texture_settings.max_dimension,
                          settings.texture_settings.max_levels, import_compression}});
                }
                catch (const TextureCookError &error)
                {
                    Fail(NativeMaterialErrorCode::MalformedImage,
                         "external image could not be cooked: " + image.resolved_path.string() + ": " +
                             error.what());
                }
                return CookPreparedImage(std::move(imported), key, settings, result, cooked_images);
            }

            ImportedTexture imported{};
            imported.image = std::move(decoded);
            imported.settings = settings.texture_settings;
            imported.settings.semantic = semantic;
            return CookPreparedImage(std::move(imported), key, settings, result, cooked_images);
        }

        MaterialTextureChannel ChannelFor(const std::string &name)
        {
            if (name == "metallic_texture") return MaterialTextureChannel::Blue;
            if (name == "roughness_texture") return MaterialTextureChannel::Green;
            return name == "occlusion_texture" ? MaterialTextureChannel::Red
                                                : MaterialTextureChannel::Rgba;
        }

        const char *ColorSpaceName(MaterialTextureColorSpace value)
        {
            return value == MaterialTextureColorSpace::Srgb ? "srgb" : "linear";
        }

        const char *ChannelName(MaterialTextureChannel value)
        {
            switch (value)
            {
            case MaterialTextureChannel::Rgba: return "rgba";
            case MaterialTextureChannel::Red: return "r";
            case MaterialTextureChannel::Green: return "g";
            case MaterialTextureChannel::Blue: return "b";
            case MaterialTextureChannel::Alpha: return "a";
            }
            return "rgba";
        }

        std::string TextureJson(const MaterialParameterSource &parameter)
        {
            const auto &path = std::get<std::string>(parameter.value);
            std::string result{"{\"path\":" + JsonString(path)};
            if (!parameter.block_compressed_path.empty())
            {
                result += ",\"variants\":{\"portable\":" + JsonString(path) +
                          ",\"bc\":" + JsonString(parameter.block_compressed_path) + "}";
            }
            result += ",\"color_space\":" +
                   JsonString(ColorSpaceName(parameter.texture_color_space)) +
                   ",\"channel\":" + JsonString(ChannelName(parameter.texture_channel)) + "}";
            return result;
        }

        std::string MaterialJson(const MaterialResource &material)
        {
            std::string result{"{\"version\":" + std::to_string(material.version) +
                               ",\"shader\":" + JsonString(material.shader_path) +
                               ",\"surface\":{\"shading_model\":\"standard_pbr\",\"blend_mode\":"};
            result += material.surface.blend_mode == MaterialBlendMode::AlphaBlend
                          ? "\"alpha_blend\""
                          : "\"opaque\"";
            result += ",\"alpha_mode\":";
            result += material.surface.alpha_mode == MaterialAlphaMode::Blend
                          ? "\"blend\""
                          : material.surface.alpha_mode == MaterialAlphaMode::Mask ? "\"mask\"" : "\"opaque\"";
            result += ",\"alpha_cutoff\":" + FloatText(material.surface.alpha_cutoff);
            result += ",\"cull_mode\":";
            result += material.surface.cull_mode == MaterialCullMode::None
                          ? "\"none\""
                          : material.surface.cull_mode == MaterialCullMode::Front ? "\"front\"" : "\"back\"";
            result += ",\"double_sided\":" + std::string{material.surface.double_sided ? "true" : "false"};
            result += "},\"parameters\":{";
            for (std::size_t index = 0; index < material.parameters.size(); ++index)
            {
                if (index != 0) result += ",";
                const MaterialParameterSource &parameter = material.parameters[index];
                result += JsonString(parameter.name) + ":";
                if (parameter.type == MaterialParameterSourceType::Scalar)
                {
                    result += FloatText(std::get<float>(parameter.value));
                }
                else if (parameter.type == MaterialParameterSourceType::Vector4)
                {
                    const auto &value = std::get<std::array<float, 4>>(parameter.value);
                    result += "[";
                    for (std::size_t component = 0; component < value.size(); ++component)
                    {
                        if (component != 0) result += ",";
                        result += FloatText(value[component]);
                    }
                    result += "]";
                }
                else
                {
                    result += TextureJson(parameter);
                }
            }
            result += "}}";
            return result;
        }

        void AddParameter(MaterialResource &material, std::string name,
                          MaterialParameterSourceType type, MaterialParameterSourceValue value)
        {
            MaterialParameterSource parameter{};
            parameter.name = std::move(name);
            parameter.type = type;
            parameter.value = std::move(value);
            material.parameters.push_back(std::move(parameter));
        }
    }

    NativeMaterialConversionError::NativeMaterialConversionError(NativeMaterialErrorCode code,
                                                                 std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }

    NativeMaterialErrorCode NativeMaterialConversionError::Code() const noexcept
    {
        return code_;
    }

    NativeMaterialCookPlan BuildNativeMaterialCookPlan(
        const ImportedModelDocument &document,
        const NativeMaterialConversionSettings &settings)
    {
        if (settings.asset_root.empty() || settings.shader_asset_path.empty())
        {
            Fail(NativeMaterialErrorCode::InvalidArgument,
                 "material conversion requires an Asset root and shader path");
        }

        NativeMaterialCookPlan plan;
        plan.materials.reserve(document.materials.size());
        std::map<TextureCookKey, std::size_t> job_ordinals;
        for (std::size_t material_index = 0; material_index < document.materials.size();
             ++material_index)
        {
            const ImportedMaterialSource &source = document.materials[material_index];
            ValidateMaterialSource(source);
            NativeMaterialMaterialPlan material_plan{};
            material_plan.source_material_index = material_index;
            const auto add_texture = [&](const std::string &name, const std::string &reference,
                                         MaterialTextureColorSpace color_space,
                                         data::TextureSemantic semantic)
            {
                if (reference.empty()) return;
                const ImportedImageSource *const image = FindImage(document, reference);
                if (image == nullptr)
                {
                    Fail(NativeMaterialErrorCode::MissingImage,
                         "material references an image that was not decoded: " + reference);
                }
                ++plan.requested_texture_bindings;
                const TextureCookKey key = MakeTextureCookKey(*image, settings, semantic);
                const auto [iterator, inserted] = job_ordinals.emplace(key, plan.texture_jobs.size());
                if (inserted)
                {
                    plan.texture_jobs.push_back(
                        {static_cast<std::size_t>(image - document.images.data()), semantic});
                }
                material_plan.texture_bindings.push_back(
                    {name, color_space, ChannelFor(name), iterator->second});
            };
            add_texture("base_color_texture", source.base_color_texture,
                        MaterialTextureColorSpace::Srgb, data::TextureSemantic::Color);
            add_texture("normal_texture", source.normal_texture,
                        MaterialTextureColorSpace::Linear, data::TextureSemantic::Normal);
            add_texture("metallic_texture", source.metallic_roughness_texture,
                        MaterialTextureColorSpace::Linear, data::TextureSemantic::PackedLinear);
            add_texture("roughness_texture", source.metallic_roughness_texture,
                        MaterialTextureColorSpace::Linear, data::TextureSemantic::PackedLinear);
            add_texture("occlusion_texture", source.occlusion_texture,
                        MaterialTextureColorSpace::Linear, data::TextureSemantic::PackedLinear);
            plan.materials.push_back(std::move(material_plan));
        }
        return plan;
    }

    NativeTextureCookResult ExecuteNativeTextureCookJob(
        const ImportedModelDocument &document,
        const NativeMaterialConversionSettings &settings,
        const NativeTextureCookJob &job,
        std::size_t job_ordinal)
    {
        if (job.image_index >= document.images.size())
        {
            Fail(NativeMaterialErrorCode::InvalidArgument, "texture cook job image index is invalid");
        }
        NativeTextureCookResult result{};
        result.job_ordinal = job_ordinal;
        result.metrics.requested_texture_bindings = 1;
        result.metrics.unique_cook_keys = 1;
        ImportedTexture imported =
            DecodeTextureSource(document.images[job.image_index], settings, job.semantic,
                                result.metrics);
        const auto cooked = [&]
        {
            const data::TextureData prepared =
                TextureCooker{}.Prepare(std::move(imported));
            ++result.metrics.texture_prepare_count;
            return CookTextureProfiles(prepared, settings, result.metrics);
        }();
        const auto add_product = [&result](CookedTexture product)
        {
            try
            {
                ValidateNativeTextureProductStructure(product.bytes);
            }
            catch (const NativeTextureError &error)
            {
                Fail(NativeMaterialErrorCode::MalformedImage,
                     "serialized texture failed validation: " + std::string{error.what()});
            }
            const std::string path =
                "../" + ProductRelativePath(ArchiveProductType::Texture,
                                               product.product_hash, "texture");
            result.products.push_back(
                {product.product_hash, "texture", std::move(product.bytes)});
            return path;
        };
        result.portable_path = add_product(std::move(cooked.first));
        if (cooked.second.has_value())
        {
            result.block_compressed_path = add_product(std::move(*cooked.second));
        }
        return result;
    }

    NativeMaterialConversionResult FinalizeNativeMaterials(
        const ImportedModelDocument &document,
        const NativeMaterialCookPlan &plan,
        const std::vector<NativeTextureCookResult> &texture_results,
        const NativeMaterialConversionSettings &settings)
    {
        if (texture_results.size() != plan.texture_jobs.size())
        {
            Fail(NativeMaterialErrorCode::InvalidArgument,
                 "texture cook results do not match the cook plan");
        }
        NativeMaterialConversionResult result;
        result.materials.reserve(plan.materials.size());
        result.metrics.requested_texture_bindings = plan.requested_texture_bindings;
        result.metrics.unique_cook_keys = plan.texture_jobs.size();
        std::set<ContentHash> unique_products;
        for (std::size_t index = 0; index < texture_results.size(); ++index)
        {
            const NativeTextureCookResult &texture_result = texture_results[index];
            if (texture_result.job_ordinal != index)
            {
                Fail(NativeMaterialErrorCode::InvalidArgument,
                     "texture cook result order is not deterministic");
            }
            result.metrics.texture_decode_count += texture_result.metrics.texture_decode_count;
            result.metrics.texture_prepare_count += texture_result.metrics.texture_prepare_count;
            result.metrics.texture_cook_count += texture_result.metrics.texture_cook_count;
            result.metrics.portable_encode_count += texture_result.metrics.portable_encode_count;
            result.metrics.block_encode_count += texture_result.metrics.block_encode_count;
            result.metrics.bc_encoding += texture_result.metrics.bc_encoding;
            for (const NativeImageProduct &product : texture_result.products)
            {
                if (unique_products.insert(product.content_hash).second)
                {
                    ++result.metrics.unique_texture_product_count;
                    result.metrics.texture_product_bytes += product.bytes.size();
                }
            }
        }

        for (const NativeMaterialMaterialPlan &material_plan : plan.materials)
        {
            const ImportedMaterialSource &source = document.materials[material_plan.source_material_index];
            ValidateMaterialSource(source);
            MaterialResource material{};
            material.version = kNativeMaterialSchemaVersion;
            material.shader_path = "../../" + NormalizeAssetRelativePath(settings.shader_asset_path);
            material.surface.shading_model = MaterialShadingModel::StandardPbr;
            material.surface.double_sided = source.double_sided;
            material.surface.cull_mode = source.double_sided ? MaterialCullMode::None : MaterialCullMode::Back;
            material.surface.alpha_mode = source.alpha_mode == ImportedAlphaMode::Blend
                                             ? MaterialAlphaMode::Blend
                                             : source.alpha_mode == ImportedAlphaMode::Mask
                                                   ? MaterialAlphaMode::Mask
                                                   : MaterialAlphaMode::Opaque;
            material.surface.blend_mode = material.surface.alpha_mode == MaterialAlphaMode::Blend
                                             ? MaterialBlendMode::AlphaBlend
                                             : MaterialBlendMode::Opaque;
            material.surface.alpha_cutoff = source.alpha_cutoff;
            AddParameter(material, "base_color", MaterialParameterSourceType::Vector4,
                         std::array<float, 4>{source.base_color[0], source.base_color[1],
                                              source.base_color[2], source.base_color[3]});
            AddParameter(material, "metallic", MaterialParameterSourceType::Scalar, source.metallic);
            AddParameter(material, "roughness", MaterialParameterSourceType::Scalar, source.roughness);
            AddParameter(material, "occlusion", MaterialParameterSourceType::Scalar,
                         source.occlusion_strength);
            AddParameter(material, "normal_scale", MaterialParameterSourceType::Scalar, source.normal_scale);
            AddParameter(material, "emissive", MaterialParameterSourceType::Vector4,
                         std::array<float, 4>{source.emissive[0], source.emissive[1], source.emissive[2],
                                              source.emissive[3]});
            for (const NativeMaterialTextureBindingPlan &binding : material_plan.texture_bindings)
            {
                const NativeTextureCookResult &texture_result = texture_results[binding.job_ordinal];
                MaterialParameterSource parameter{};
                parameter.name = binding.name;
                parameter.type = MaterialParameterSourceType::Texture;
                parameter.value = texture_result.portable_path;
                parameter.block_compressed_path = texture_result.block_compressed_path;
                parameter.texture_color_space = binding.color_space;
                parameter.texture_channel = binding.channel;
                material.parameters.push_back(std::move(parameter));
            }
            const std::string json = MaterialJson(material);
            std::vector<std::byte> bytes(json.size());
            std::transform(json.begin(), json.end(), bytes.begin(),
                           [](char character) { return static_cast<std::byte>(character); });
            result.materials.push_back(
                {material_plan.source_material_index, source.name, std::move(material), std::move(bytes), {}});
            result.materials.back().content_hash = Sha256(result.materials.back().bytes);
        }
        return result;
    }

    NativeMaterialConversionResult ConvertImportedMaterials(
        const ImportedModelDocument &document,
        const NativeMaterialConversionSettings &settings)
    {
        const NativeMaterialCookPlan plan = BuildNativeMaterialCookPlan(document, settings);
        std::vector<NativeTextureCookResult> texture_results;
        texture_results.reserve(plan.texture_jobs.size());
        for (std::size_t ordinal = 0; ordinal < plan.texture_jobs.size(); ++ordinal)
        {
            const NativeTextureCookJob &job = plan.texture_jobs[ordinal];
            if (settings.texture_progress_callback)
            {
                settings.texture_progress_callback(document.images[job.image_index].path);
            }
            texture_results.push_back(
                ExecuteNativeTextureCookJob(document, settings, job, ordinal));
        }
        NativeMaterialConversionResult result =
            FinalizeNativeMaterials(document, plan, texture_results, settings);
        std::set<ContentHash> published_products;
        for (NativeTextureCookResult &texture_result : texture_results)
        {
            for (NativeImageProduct &product : texture_result.products)
            {
                if (published_products.insert(product.content_hash).second)
                {
                    result.embedded_images.push_back(std::move(product));
                }
            }
        }
        return result;
    }

    void ValidateNativeMaterialProduct(const std::vector<std::byte> &bytes)
    {
        try
        {
            const std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            const nlohmann::json source = nlohmann::json::parse(text);
            if (!source.is_object() || source.size() != 4 ||
                !source.contains("version") || !source["version"].is_number_integer() ||
                source["version"].get<int>() != kNativeMaterialSchemaVersion ||
                !source.contains("shader") || !source["shader"].is_string() ||
                source["shader"].get<std::string>().empty() ||
                !source.contains("surface") || !source["surface"].is_object() ||
                !source.contains("parameters") || !source["parameters"].is_object())
            {
                Fail(NativeMaterialErrorCode::InvalidValue, "native material schema is invalid");
            }

            const nlohmann::json &surface = source["surface"];
            if (surface.size() != 6 || !surface.contains("shading_model") ||
                surface["shading_model"] != "standard_pbr" ||
                !surface.contains("blend_mode") || !surface["blend_mode"].is_string() ||
                (surface["blend_mode"] != "opaque" && surface["blend_mode"] != "alpha_blend") ||
                !surface.contains("alpha_mode") || !surface["alpha_mode"].is_string() ||
                (surface["alpha_mode"] != "opaque" && surface["alpha_mode"] != "mask" &&
                 surface["alpha_mode"] != "blend") ||
                !surface.contains("alpha_cutoff") || !surface["alpha_cutoff"].is_number() ||
                !IsUnit(surface["alpha_cutoff"].get<float>()) ||
                !surface.contains("cull_mode") || !surface["cull_mode"].is_string() ||
                (surface["cull_mode"] != "none" && surface["cull_mode"] != "back" &&
                 surface["cull_mode"] != "front") ||
                !surface.contains("double_sided") || !surface["double_sided"].is_boolean())
            {
                Fail(NativeMaterialErrorCode::InvalidValue, "native material surface is invalid");
            }
            const bool is_blend = surface["blend_mode"] == "alpha_blend";
            if (is_blend != (surface["alpha_mode"] == "blend"))
            {
                Fail(NativeMaterialErrorCode::InvalidValue,
                     "native material blend policy is inconsistent");
            }

            for (const auto &[name, value] : source["parameters"].items())
            {
                if (name == "base_color" || name == "emissive")
                {
                    if (!value.is_array() || value.size() != 4)
                    {
                        Fail(NativeMaterialErrorCode::InvalidValue,
                             "native material vector parameter is invalid: " + name);
                    }
                    for (const auto &component : value)
                    {
                        if (!component.is_number())
                        {
                            Fail(NativeMaterialErrorCode::InvalidValue,
                                 "native material vector component is invalid: " + name);
                        }
                        const float number = component.get<float>();
                        if ((name == "base_color" && !IsUnit(number)) ||
                            (name == "emissive" && !IsFiniteNonNegative(number)))
                        {
                            Fail(NativeMaterialErrorCode::InvalidValue,
                                 "native material vector value is invalid: " + name);
                        }
                    }
                    continue;
                }

                if (name == "metallic" || name == "roughness" || name == "occlusion")
                {
                    if (!value.is_number() || !IsUnit(value.get<float>()))
                    {
                        Fail(NativeMaterialErrorCode::InvalidValue,
                             "native material scalar value is invalid: " + name);
                    }
                    continue;
                }
                if (name == "normal_scale")
                {
                    if (!value.is_number() || !IsFiniteNonNegative(value.get<float>()))
                    {
                        Fail(NativeMaterialErrorCode::InvalidValue,
                             "native material normal scale is invalid");
                    }
                    continue;
                }

                const bool is_texture = name == "base_color_texture" || name == "normal_texture" ||
                                        name == "metallic_texture" || name == "roughness_texture" ||
                                        name == "occlusion_texture";
                if (!is_texture ||
                    !(value.is_string() || (value.is_object() && value.contains("path"))))
                {
                    Fail(NativeMaterialErrorCode::InvalidValue,
                         "native material parameter is unsupported: " + name);
                }
                const nlohmann::json &texture = value;
                if ((texture.is_string() && texture.get<std::string>().empty()) ||
                    (texture.is_object() &&
                     (!texture.contains("path") || !texture["path"].is_string() ||
                      texture["path"].get<std::string>().empty())))
                {
                    Fail(NativeMaterialErrorCode::InvalidValue,
                         "native material texture path is invalid: " + name);
                }
                if (texture.is_object())
                {
                    if ((texture.size() != 3 && texture.size() != 4) ||
                        !texture.contains("color_space") ||
                        !texture["color_space"].is_string() ||
                        (texture["color_space"] != "srgb" && texture["color_space"] != "linear") ||
                        !texture.contains("channel") || !texture["channel"].is_string() ||
                        (texture["channel"] != "rgba" && texture["channel"] != "r" &&
                         texture["channel"] != "g" && texture["channel"] != "b" &&
                         texture["channel"] != "a"))
                    {
                        Fail(NativeMaterialErrorCode::InvalidValue,
                             "native material texture metadata is invalid: " + name);
                    }
                    if (texture.contains("variants"))
                    {
                        const nlohmann::json &variants = texture["variants"];
                        if (!variants.is_object() || variants.size() != 2 ||
                            !variants.contains("portable") || !variants["portable"].is_string() ||
                            !variants.contains("bc") || !variants["bc"].is_string() ||
                            variants["portable"] != texture["path"] ||
                            variants["bc"].get<std::string>().empty())
                        {
                            Fail(NativeMaterialErrorCode::InvalidValue,
                                 "native material texture variants are invalid: " + name);
                        }
                    }
                }
            }
        }
        catch (const nlohmann::json::exception &exception)
        {
            throw NativeMaterialConversionError(
                NativeMaterialErrorCode::InvalidValue,
                std::string{"native material JSON is malformed: "} + exception.what());
        }
    }
}
