#include "texture_importer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

#include "data/texture_mipmap.h"

namespace kpengine::asset
{
    namespace
    {
        [[noreturn]] void Fail(TextureCookErrorCode code, const std::string &message)
        {
            throw TextureCookError(code, message);
        }

        std::uint16_t FloatToHalf(float value) noexcept
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
            const std::uint32_t exponent = (bits >> 23) & 0xffu;
            std::uint32_t mantissa = bits & 0x7fffffu;
            if (exponent == 0xffu)
            {
                return static_cast<std::uint16_t>(sign | 0x7c00u |
                                                  (mantissa != 0 ? 0x0200u : 0u));
            }
            int exponent16 = static_cast<int>(exponent) - 127 + 15;
            if (exponent16 >= 31)
            {
                return static_cast<std::uint16_t>(sign | 0x7c00u);
            }
            if (exponent16 <= 0)
            {
                if (exponent16 < -10)
                {
                    return sign;
                }
                mantissa = (mantissa | 0x800000u) >> (1 - exponent16);
                return static_cast<std::uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
            }
            mantissa += 0x1000u;
            if ((mantissa & 0x800000u) != 0)
            {
                mantissa = 0;
                ++exponent16;
                if (exponent16 >= 31)
                {
                    return static_cast<std::uint16_t>(sign | 0x7c00u);
                }
            }
            return static_cast<std::uint16_t>(sign |
                                              (static_cast<std::uint16_t>(exponent16) << 10) |
                                              (mantissa >> 13));
        }

        data::TextureData ConvertImage(const image_io::ImageBuffer &image,
                                        const TextureCookSettings &settings)
        {
            if (!image.IsValid() || settings.max_dimension == 0 ||
                settings.max_dimension > kNativeTextureMaxDimension ||
                settings.max_levels > kNativeTextureMaxMipLevels)
            {
                Fail(TextureCookErrorCode::InvalidArgument,
                     "texture source or cook settings are invalid");
            }
            data::TextureData result{};
            result.width = image.width;
            result.height = image.height;
            result.format = image.format == image_io::ImagePixelFormat::Rgba32Float
                                ? TextureFormat::TEXTURE_FORMAT_RGBA16F
                                : (settings.semantic == data::TextureSemantic::Color ||
                                           settings.semantic == data::TextureSemantic::Generic
                                       ? TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB
                                       : TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM);
            result.semantic = settings.semantic;
            if (image.format == image_io::ImagePixelFormat::Rgba8)
            {
                result.pixels = image.pixels;
            }
            else
            {
                if (image.pixels.size() % (sizeof(float) * 4) != 0 ||
                    image.pixels.size() > std::numeric_limits<std::size_t>::max() / 2)
                {
                    Fail(TextureCookErrorCode::ConversionFailed,
                         "HDR texture payload has an invalid size");
                }
                result.pixels.resize(image.pixels.size() / 2);
                for (std::size_t offset = 0; offset < image.pixels.size(); offset += sizeof(float))
                {
                    float value = 0.0f;
                    std::memcpy(&value, image.pixels.data() + offset, sizeof(value));
                    const std::uint16_t half = FloatToHalf(value);
                    std::memcpy(result.pixels.data() + offset / 2, &half, sizeof(half));
                }
            }
            if (!data::GenerateTextureMipChain(
                    result, settings.semantic,
                    {settings.max_dimension, settings.max_levels}))
            {
                Fail(TextureCookErrorCode::ConversionFailed,
                     "texture mip chain generation failed");
            }
            return result;
        }
    }

    TextureCookError::TextureCookError(TextureCookErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }

    TextureCookErrorCode TextureCookError::Code() const noexcept
    {
        return code_;
    }

    ImportedTexture TextureImporter::Import(const TextureImportRequest &request) const
    {
        if (request.source_path.empty() || request.settings.max_dimension == 0 ||
            request.settings.max_dimension > kNativeTextureMaxDimension ||
            request.settings.max_levels > kNativeTextureMaxMipLevels)
        {
            Fail(TextureCookErrorCode::InvalidArgument, "texture import request is incomplete");
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(request.source_path, error) || error)
        {
            Fail(TextureCookErrorCode::IoError,
                 "texture source is missing or is not a regular file: " +
                     request.source_path.string());
        }
        const image_io::ImageDecodeResult decoded =
            image_io::DecodeImageFile(request.source_path.string());
        if (!decoded.result.success)
        {
            Fail(TextureCookErrorCode::DecodeFailed,
                 "texture source could not be decoded: " + decoded.result.diagnostic);
        }
        try
        {
            return {Sha256File(request.source_path), decoded.image, request.settings};
        }
        catch (const ModelArchiveError &error)
        {
            Fail(TextureCookErrorCode::IoError, error.what());
        }
    }

    CookedTexture TextureCooker::Cook(const ImportedTexture &source) const
    {
        if (source.settings.compression == TextureCompressionPolicy::RequireBlockCompression)
        {
            Fail(TextureCookErrorCode::UnsupportedCompression,
                 "block-compressed texture products are not supported by the current RHI format contract");
        }
        data::TextureData data = ConvertImage(source.image, source.settings);
        std::vector<std::byte> bytes;
        try
        {
            bytes = SerializeNativeTexture(data);
        }
        catch (const NativeTextureError &error)
        {
            Fail(TextureCookErrorCode::ProductInvalid, error.what());
        }
        const ContentHash product_hash = Sha256(bytes);
        return {std::move(data), std::move(bytes), product_hash};
    }

    ImportedTexture ImportTexture(const TextureImportRequest &request)
    {
        return TextureImporter{}.Import(request);
    }

    CookedTexture CookTexture(const ImportedTexture &source)
    {
        return TextureCooker{}.Cook(source);
    }
}
