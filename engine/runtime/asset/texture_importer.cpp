#include "texture_importer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <utility>

#include "data/texture_mipmap.h"
#include <rgbcx.h>

namespace kpengine::asset
{
    namespace
    {
        [[noreturn]] void Fail(TextureCookErrorCode code, const std::string &message)
        {
            throw TextureCookError(code, message);
        }

        bool HasValidBcSettings(TextureBcEncoder encoder, TextureBcQuality quality) noexcept
        {
            return static_cast<std::uint8_t>(encoder) <=
                       static_cast<std::uint8_t>(TextureBcEncoder::RgbcxV113) &&
                   static_cast<std::uint8_t>(quality) <=
                       static_cast<std::uint8_t>(TextureBcQuality::Balanced);
        }

        void EnsureRgbcxInitialized()
        {
            static std::once_flag initialization;
            std::call_once(initialization, []
            {
                rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
            });
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

        data::TextureData ConvertImage(image_io::ImageBuffer image,
                                        const TextureCookSettings &settings)
        {
            if (!image.IsValid() || settings.max_dimension == 0 ||
                settings.max_dimension > kNativeTextureMaxDimension ||
                settings.max_levels > kNativeTextureMaxMipLevels ||
                !HasValidBcSettings(settings.bc_encoder, settings.bc_quality))
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
                result.pixels = std::move(image.pixels);
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

        std::uint16_t PackRgb565(const std::array<std::uint8_t, 3> &rgb) noexcept
        {
            const std::uint16_t red = static_cast<std::uint16_t>((rgb[0] * 31U + 127U) / 255U);
            const std::uint16_t green = static_cast<std::uint16_t>((rgb[1] * 63U + 127U) / 255U);
            const std::uint16_t blue = static_cast<std::uint16_t>((rgb[2] * 31U + 127U) / 255U);
            return static_cast<std::uint16_t>((red << 11) | (green << 5) | blue);
        }

        std::array<std::uint8_t, 3> UnpackRgb565(std::uint16_t value) noexcept
        {
            const std::uint8_t red = static_cast<std::uint8_t>((value >> 11) & 0x1fU);
            const std::uint8_t green = static_cast<std::uint8_t>((value >> 5) & 0x3fU);
            const std::uint8_t blue = static_cast<std::uint8_t>(value & 0x1fU);
            return {static_cast<std::uint8_t>((red * 255U + 15U) / 31U),
                    static_cast<std::uint8_t>((green * 255U + 31U) / 63U),
                    static_cast<std::uint8_t>((blue * 255U + 15U) / 31U)};
        }

        void EncodeBc4Block(const std::array<std::uint8_t, 16> &values,
                            std::uint8_t *destination) noexcept
        {
            const auto [min_value, max_value] = std::minmax_element(values.begin(), values.end());
            const std::uint8_t endpoint0 = *max_value;
            const std::uint8_t endpoint1 = *min_value;
            std::array<std::uint8_t, 8> palette{};
            palette[0] = endpoint0;
            palette[1] = endpoint1;
            if (endpoint0 > endpoint1)
            {
                for (std::size_t index = 1; index < 7; ++index)
                {
                    palette[index + 1] = static_cast<std::uint8_t>(
                        ((7U - index) * endpoint0 + index * endpoint1 + 3U) / 7U);
                }
            }
            else
            {
                palette[2] = static_cast<std::uint8_t>((2U * endpoint0 + endpoint1 + 1U) / 3U);
                palette[3] = static_cast<std::uint8_t>((endpoint0 + 2U * endpoint1 + 1U) / 3U);
                palette[4] = static_cast<std::uint8_t>((4U * endpoint0 + endpoint1 + 2U) / 5U);
                palette[5] = static_cast<std::uint8_t>((3U * endpoint0 + 2U * endpoint1 + 2U) / 5U);
                palette[6] = 0;
                palette[7] = 255;
            }

            std::uint64_t indices = 0;
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                std::uint8_t best_index = 0;
                std::uint32_t best_error = std::numeric_limits<std::uint32_t>::max();
                for (std::uint8_t candidate = 0; candidate < palette.size(); ++candidate)
                {
                    const int difference = static_cast<int>(values[index]) - palette[candidate];
                    const std::uint32_t error = static_cast<std::uint32_t>(difference * difference);
                    if (error < best_error)
                    {
                        best_error = error;
                        best_index = candidate;
                    }
                }
                indices |= static_cast<std::uint64_t>(best_index) << (index * 3U);
            }
            destination[0] = endpoint0;
            destination[1] = endpoint1;
            for (std::size_t index = 0; index < 6; ++index)
            {
                destination[index + 2] =
                    static_cast<std::uint8_t>(indices >> (index * 8U));
            }
        }

        void EncodeBc1Block(const std::array<std::array<std::uint8_t, 4>, 16> &pixels,
                            std::uint8_t *destination) noexcept
        {
            std::array<std::uint8_t, 3> minimum{255, 255, 255};
            std::array<std::uint8_t, 3> maximum{0, 0, 0};
            for (const auto &pixel : pixels)
            {
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    minimum[channel] = std::min(minimum[channel], pixel[channel]);
                    maximum[channel] = std::max(maximum[channel], pixel[channel]);
                }
            }
            std::uint16_t endpoint0 = PackRgb565(maximum);
            std::uint16_t endpoint1 = PackRgb565(minimum);
            if (endpoint0 <= endpoint1)
            {
                std::swap(endpoint0, endpoint1);
            }
            if (endpoint0 == endpoint1)
            {
                if (endpoint1 == std::numeric_limits<std::uint16_t>::max())
                {
                    endpoint1 = static_cast<std::uint16_t>(endpoint0 - 1U);
                }
                else
                {
                    endpoint0 = static_cast<std::uint16_t>(endpoint1 + 1U);
                }
            }

            const std::array<std::uint8_t, 3> color0 = UnpackRgb565(endpoint0);
            const std::array<std::uint8_t, 3> color1 = UnpackRgb565(endpoint1);
            const std::array<std::array<std::uint8_t, 3>, 4> palette{
                color0,
                color1,
                {static_cast<std::uint8_t>((2U * color0[0] + color1[0] + 1U) / 3U),
                 static_cast<std::uint8_t>((2U * color0[1] + color1[1] + 1U) / 3U),
                 static_cast<std::uint8_t>((2U * color0[2] + color1[2] + 1U) / 3U)},
                {static_cast<std::uint8_t>((color0[0] + 2U * color1[0] + 1U) / 3U),
                 static_cast<std::uint8_t>((color0[1] + 2U * color1[1] + 1U) / 3U),
                 static_cast<std::uint8_t>((color0[2] + 2U * color1[2] + 1U) / 3U)}};

            std::uint32_t indices = 0;
            for (std::size_t index = 0; index < pixels.size(); ++index)
            {
                std::uint8_t best_index = 0;
                std::uint32_t best_error = std::numeric_limits<std::uint32_t>::max();
                for (std::uint8_t candidate = 0; candidate < palette.size(); ++candidate)
                {
                    std::uint32_t error = 0;
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const int difference = static_cast<int>(pixels[index][channel]) -
                                               palette[candidate][channel];
                        error += static_cast<std::uint32_t>(difference * difference);
                    }
                    if (error < best_error)
                    {
                        best_error = error;
                        best_index = candidate;
                    }
                }
                indices |= static_cast<std::uint32_t>(best_index) << (index * 2U);
            }
            destination[0] = static_cast<std::uint8_t>(endpoint0);
            destination[1] = static_cast<std::uint8_t>(endpoint0 >> 8);
            destination[2] = static_cast<std::uint8_t>(endpoint1);
            destination[3] = static_cast<std::uint8_t>(endpoint1 >> 8);
            for (std::size_t index = 0; index < 4; ++index)
            {
                destination[index + 4] =
                    static_cast<std::uint8_t>(indices >> (index * 8U));
            }
        }

        std::vector<std::uint8_t> CompressRgbaMipReferenceV1(
            const std::vector<std::uint8_t> &source, std::uint32_t width,
            std::uint32_t height, TextureFormat format,
            const std::function<bool()> &cancellation_requested)
        {
            if (source.size() != static_cast<std::size_t>(width) * height * 4U)
            {
                Fail(TextureCookErrorCode::ConversionFailed,
                     "RGBA texture mip payload has an invalid size");
            }
            const std::size_t expected_size =
                data::GetTextureMipByteCount(width, height, format);
            std::vector<std::uint8_t> output(expected_size);
            std::size_t output_offset = 0;
            for (std::uint32_t block_y = 0; block_y < height; block_y += 4U)
            {
                if (cancellation_requested && cancellation_requested())
                {
                    Fail(TextureCookErrorCode::Cancelled, "texture cook was cancelled");
                }
                for (std::uint32_t block_x = 0; block_x < width; block_x += 4U)
                {
                    std::array<std::array<std::uint8_t, 4>, 16> pixels{};
                    std::array<std::uint8_t, 16> channel0{};
                    std::array<std::uint8_t, 16> channel1{};
                    for (std::uint32_t local_y = 0; local_y < 4U; ++local_y)
                    {
                        for (std::uint32_t local_x = 0; local_x < 4U; ++local_x)
                        {
                            const std::uint32_t x = std::min(width - 1U, block_x + local_x);
                            const std::uint32_t y = std::min(height - 1U, block_y + local_y);
                            const std::size_t source_offset =
                                (static_cast<std::size_t>(y) * width + x) * 4U;
                            const std::size_t block_index = local_y * 4U + local_x;
                            for (std::size_t channel = 0; channel < 4; ++channel)
                            {
                                pixels[block_index][channel] = source[source_offset + channel];
                            }
                            channel0[block_index] = pixels[block_index][0];
                            channel1[block_index] = pixels[block_index][1];
                        }
                    }
                    if (format == TextureFormat::TEXTURE_FORMAT_BC4_UNORM)
                    {
                        EncodeBc4Block(channel0, output.data() + output_offset);
                        output_offset += 8;
                    }
                    else if (format == TextureFormat::TEXTURE_FORMAT_BC5_UNORM)
                    {
                        EncodeBc4Block(channel0, output.data() + output_offset);
                        EncodeBc4Block(channel1, output.data() + output_offset + 8);
                        output_offset += 16;
                    }
                    else
                    {
                        std::array<std::uint8_t, 16> alpha_values{};
                        for (std::size_t index = 0; index < alpha_values.size(); ++index)
                        {
                            alpha_values[index] = pixels[index][3];
                        }
                        EncodeBc4Block(alpha_values, output.data() + output_offset);
                        EncodeBc1Block(pixels, output.data() + output_offset + 8);
                        output_offset += 16;
                    }
                }
            }
            if (output_offset != expected_size)
            {
                Fail(TextureCookErrorCode::ConversionFailed,
                     "block-compressed texture payload size is inconsistent");
            }
            return output;
        }

        std::vector<std::uint8_t> CompressRgbaMipRgbcxV113(
            const std::vector<std::uint8_t> &source, std::uint32_t width,
            std::uint32_t height, TextureFormat format, TextureBcQuality quality,
            const std::function<bool()> &cancellation_requested)
        {
            if (source.size() != static_cast<std::size_t>(width) * height * 4U)
            {
                Fail(TextureCookErrorCode::ConversionFailed,
                     "RGBA texture mip payload has an invalid size");
            }
            EnsureRgbcxInitialized();
            const std::size_t expected_size =
                data::GetTextureMipByteCount(width, height, format);
            std::vector<std::uint8_t> output(expected_size);
            const std::uint32_t total_orderings =
                quality == TextureBcQuality::Fast ? 4U : 10U;
            const std::uint32_t bc3_flags = rgbcx::cEncodeBC1UseLikelyTotalOrderings;
            std::size_t output_offset = 0;
            for (std::uint32_t block_y = 0; block_y < height; block_y += 4U)
            {
                if (cancellation_requested && cancellation_requested())
                {
                    Fail(TextureCookErrorCode::Cancelled, "texture cook was cancelled");
                }
                for (std::uint32_t block_x = 0; block_x < width; block_x += 4U)
                {
                    std::array<std::array<std::uint8_t, 4>, 16> pixels{};
                    for (std::uint32_t local_y = 0; local_y < 4U; ++local_y)
                    {
                        for (std::uint32_t local_x = 0; local_x < 4U; ++local_x)
                        {
                            const std::uint32_t x = std::min(width - 1U, block_x + local_x);
                            const std::uint32_t y = std::min(height - 1U, block_y + local_y);
                            const std::size_t source_offset =
                                (static_cast<std::size_t>(y) * width + x) * 4U;
                            const std::size_t block_index = local_y * 4U + local_x;
                            for (std::size_t channel = 0; channel < 4; ++channel)
                            {
                                pixels[block_index][channel] = source[source_offset + channel];
                            }
                        }
                    }
                    auto *destination = output.data() + output_offset;
                    const auto *block_pixels = pixels.front().data();
                    if (format == TextureFormat::TEXTURE_FORMAT_BC4_UNORM)
                    {
                        rgbcx::encode_bc4(destination, block_pixels, 4);
                        output_offset += 8;
                    }
                    else if (format == TextureFormat::TEXTURE_FORMAT_BC5_UNORM)
                    {
                        rgbcx::encode_bc5(destination, block_pixels, 0, 1, 4);
                        output_offset += 16;
                    }
                    else
                    {
                        rgbcx::encode_bc3(destination, block_pixels, bc3_flags,
                                          total_orderings);
                        output_offset += 16;
                    }
                }
            }
            if (output_offset != expected_size)
            {
                Fail(TextureCookErrorCode::ConversionFailed,
                     "block-compressed texture payload size is inconsistent");
            }
            return output;
        }

        TextureBcFormatMetrics *MetricsForFormat(TextureBcEncodingMetrics *metrics,
                                                 TextureFormat format) noexcept
        {
            if (metrics == nullptr)
            {
                return nullptr;
            }
            switch (format)
            {
            case TextureFormat::TEXTURE_FORMAT_BC3_SRGB:
            case TextureFormat::TEXTURE_FORMAT_BC3_UNORM:
                return &metrics->bc3;
            case TextureFormat::TEXTURE_FORMAT_BC4_UNORM:
                return &metrics->bc4;
            case TextureFormat::TEXTURE_FORMAT_BC5_UNORM:
                return &metrics->bc5;
            default:
                return nullptr;
            }
        }

        void RecordEncodingMetrics(TextureBcEncodingMetrics *metrics,
                                    TextureFormat format,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    double seconds) noexcept
        {
            TextureBcFormatMetrics *format_metrics = MetricsForFormat(metrics, format);
            if (format_metrics == nullptr)
            {
                return;
            }
            const std::uint64_t block_width =
                (static_cast<std::uint64_t>(width) + 3U) / 4U;
            const std::uint64_t block_height =
                (static_cast<std::uint64_t>(height) + 3U) / 4U;
            format_metrics->block_count += block_width * block_height;
            format_metrics->source_megapixels +=
                static_cast<double>(width) * static_cast<double>(height) / 1000000.0;
            format_metrics->encode_seconds += seconds;
        }

        data::TextureData CompressTexture(data::TextureData source,
                                          TextureCompressionPolicy policy,
                                          TextureBcEncoder bc_encoder,
                                          TextureBcQuality bc_quality,
                                          TextureBcEncodingMetrics *encoding_metrics,
                                          const std::function<bool()> &cancellation_requested)
        {
            static_cast<void>(bc_quality);
            if (bc_encoder != TextureBcEncoder::ReferenceV1 &&
                bc_encoder != TextureBcEncoder::RgbcxV113)
            {
                Fail(TextureCookErrorCode::UnsupportedCompression,
                     "the selected BC encoder is not integrated in this build");
            }
            if (source.format == TextureFormat::TEXTURE_FORMAT_RGBA16F)
            {
                if (policy == TextureCompressionPolicy::RequireBlockCompression)
                {
                    Fail(TextureCookErrorCode::UnsupportedCompression,
                         "HDR textures do not have a BC fallback in this profile");
                }
                return source;
            }
            if (source.format != TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM &&
                source.format != TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB)
            {
                Fail(TextureCookErrorCode::UnsupportedCompression,
                     "block compression requires an RGBA8 source texture");
            }
            TextureFormat compressed_format = TextureFormat::TEXTURE_FORMAT_BC3_UNORM;
            switch (source.semantic)
            {
            case data::TextureSemantic::Normal:
                compressed_format = TextureFormat::TEXTURE_FORMAT_BC5_UNORM;
                break;
            case data::TextureSemantic::OpacityMask:
                compressed_format = TextureFormat::TEXTURE_FORMAT_BC4_UNORM;
                break;
            case data::TextureSemantic::Color:
            case data::TextureSemantic::Generic:
                compressed_format = TextureFormat::TEXTURE_FORMAT_BC3_SRGB;
                break;
            case data::TextureSemantic::PackedLinear:
                compressed_format = TextureFormat::TEXTURE_FORMAT_BC3_UNORM;
                break;
            }
            source.format = compressed_format;
            const auto compress_mip = [&](const std::vector<std::uint8_t> &pixels,
                                          std::uint32_t width, std::uint32_t height)
            {
                const auto started = std::chrono::steady_clock::now();
                std::vector<std::uint8_t> compressed_pixels =
                    bc_encoder == TextureBcEncoder::RgbcxV113
                        ? CompressRgbaMipRgbcxV113(pixels, width, height,
                                                    compressed_format, bc_quality,
                                                    cancellation_requested)
                        : CompressRgbaMipReferenceV1(pixels, width, height,
                                                     compressed_format,
                                                     cancellation_requested);
                RecordEncodingMetrics(
                    encoding_metrics, compressed_format, width, height,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
                        .count());
                return compressed_pixels;
            };
            source.pixels = compress_mip(source.pixels, source.width, source.height);
            for (std::size_t index = 0; index < source.mip_subresources.size(); ++index)
            {
                source.mip_subresources[index].pixels = compress_mip(
                    source.mip_subresources[index].pixels,
                    source.mip_subresources[index].width,
                    source.mip_subresources[index].height);
            }
            return source;
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
            request.settings.max_levels > kNativeTextureMaxMipLevels ||
            !HasValidBcSettings(request.settings.bc_encoder, request.settings.bc_quality))
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
        image_io::ImageDecodeResult decoded =
            image_io::DecodeImageFile(request.source_path.string());
        if (!decoded.result.success)
        {
            Fail(TextureCookErrorCode::DecodeFailed,
                 "texture source could not be decoded: " + decoded.result.diagnostic);
        }
        try
        {
            const ContentHash source_hash = Sha256File(request.source_path);
            return {source_hash, std::move(decoded.image), request.settings};
        }
        catch (const ModelArchiveError &error)
        {
            Fail(TextureCookErrorCode::IoError, error.what());
        }
    }

    data::TextureData TextureCooker::Prepare(const ImportedTexture &source) const
    {
        return ConvertImage(source.image, source.settings);
    }

    data::TextureData TextureCooker::Prepare(ImportedTexture &&source) const
    {
        return ConvertImage(std::move(source.image), source.settings);
    }

    CookedTexture TextureCooker::CookPrepared(const data::TextureData &prepared,
                                              TextureCompressionPolicy compression,
                                              TextureBcEncoder bc_encoder,
                                              TextureBcQuality bc_quality,
                                              TextureBcEncodingMetrics *encoding_metrics,
                                              std::function<bool()> cancellation_requested) const
    {
        if (!HasValidBcSettings(bc_encoder, bc_quality))
        {
            Fail(TextureCookErrorCode::InvalidArgument,
                 "texture BC encoder settings are invalid");
        }
        if (cancellation_requested && cancellation_requested())
        {
            Fail(TextureCookErrorCode::Cancelled, "texture cook was cancelled");
        }
        data::TextureData data = prepared;
        if (compression != TextureCompressionPolicy::Portable)
        {
            data = CompressTexture(std::move(data), compression, bc_encoder, bc_quality,
                                   encoding_metrics, cancellation_requested);
        }
        if (cancellation_requested && cancellation_requested())
        {
            Fail(TextureCookErrorCode::Cancelled, "texture cook was cancelled");
        }
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

    CookedTexture TextureCooker::Cook(const ImportedTexture &source) const
    {
        return CookPrepared(Prepare(source), source.settings.compression,
                            source.settings.bc_encoder, source.settings.bc_quality);
    }

    ImportedTexture ImportTexture(const TextureImportRequest &request)
    {
        return TextureImporter{}.Import(request);
    }

    CookedTexture CookTexture(const ImportedTexture &source)
    {
        return TextureCooker{}.Cook(source);
    }

    void PublishCookedTextureProduct(const std::filesystem::path &archive_root,
                                     const CookedTexture &cooked)
    {
        const std::filesystem::path destination =
            archive_root / ProductRelativePath(ArchiveProductType::Texture,
                                               cooked.product_hash, "texture");
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
        {
            throw std::runtime_error("failed to create texture archive directory: " +
                                     error.message());
        }

        if (std::filesystem::exists(destination, error))
        {
            if (error)
            {
                throw std::runtime_error("failed to inspect cooked texture destination: " +
                                         error.message());
            }
            std::ifstream existing(destination, std::ios::binary | std::ios::ate);
            if (!existing.is_open())
            {
                throw std::runtime_error("failed to inspect cooked texture product");
            }
            const std::streampos end = existing.tellg();
            if (end < 0)
            {
                throw std::runtime_error("failed to determine cooked texture product size");
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(end));
            existing.seekg(0, std::ios::beg);
            existing.read(reinterpret_cast<char *>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
            if (!existing.good() && !existing.eof())
            {
                throw std::runtime_error("failed to read cooked texture product");
            }
            if (bytes != cooked.bytes)
            {
                throw std::runtime_error("immutable cooked texture product collision: " +
                                         destination.string());
            }
            return;
        }
        if (error)
        {
            throw std::runtime_error("failed to inspect cooked texture destination: " +
                                     error.message());
        }

        const std::filesystem::path temporary = destination.string() + ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("failed to create cooked texture product");
        }
        output.write(reinterpret_cast<const char *>(cooked.bytes.data()),
                     static_cast<std::streamsize>(cooked.bytes.size()));
        if (!output.good())
        {
            throw std::runtime_error("failed to write cooked texture product");
        }
        output.close();
        std::filesystem::rename(temporary, destination, error);
        if (error)
        {
            std::filesystem::remove(temporary);
            throw std::runtime_error("failed to publish cooked texture product: " +
                                     error.message());
        }
    }
}
