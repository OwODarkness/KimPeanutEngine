#include "data/texture_mipmap.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace kpengine::data
{
    namespace
    {
        constexpr float kAlphaCoverageCutoff = 0.5f;

        size_t BytesPerPixel(TextureFormat format) noexcept
        {
            switch (format)
            {
            case TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM:
            case TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB:
                return 4;
            case TextureFormat::TEXTURE_FORMAT_RGBA16F:
                return 8;
            default:
                return 0;
            }
        }

        size_t BlockByteCount(TextureFormat format) noexcept
        {
            switch (format)
            {
            case TextureFormat::TEXTURE_FORMAT_BC4_UNORM:
                return 8;
            case TextureFormat::TEXTURE_FORMAT_BC5_UNORM:
            case TextureFormat::TEXTURE_FORMAT_BC3_UNORM:
            case TextureFormat::TEXTURE_FORMAT_BC3_SRGB:
                return 16;
            default:
                return 0;
            }
        }

        float SrgbToLinear(float value) noexcept
        {
            return value <= 0.04045f ? value / 12.92f
                                     : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        float LinearToSrgb(float value) noexcept
        {
            value = std::max(0.0f, value);
            return value <= 0.0031308f ? value * 12.92f
                                       : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        uint8_t ToByte(float value) noexcept
        {
            return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        uint16_t FloatToHalf(float value) noexcept
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
            const uint32_t exponent = (bits >> 23) & 0xffu;
            uint32_t mantissa = bits & 0x7fffffu;
            if (exponent == 0xffu)
            {
                return static_cast<uint16_t>(sign | 0x7c00u | (mantissa != 0 ? 0x0200u : 0u));
            }
            int32_t half_exponent = static_cast<int32_t>(exponent) - 127 + 15;
            if (half_exponent >= 31)
            {
                return static_cast<uint16_t>(sign | 0x7c00u);
            }
            if (half_exponent <= 0)
            {
                if (half_exponent < -10)
                {
                    return sign;
                }
                mantissa = (mantissa | 0x800000u) >> (1 - half_exponent);
                return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
            }
            mantissa += 0x1000u;
            if ((mantissa & 0x800000u) != 0)
            {
                mantissa = 0;
                ++half_exponent;
                if (half_exponent >= 31)
                {
                    return static_cast<uint16_t>(sign | 0x7c00u);
                }
            }
            return static_cast<uint16_t>(sign |
                                         (static_cast<uint16_t>(half_exponent) << 10) |
                                         (mantissa >> 13));
        }

        float HalfToFloat(uint16_t value) noexcept
        {
            const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16;
            uint32_t exponent = (value >> 10) & 0x1fu;
            uint32_t mantissa = value & 0x3ffu;
            uint32_t bits = sign;
            if (exponent == 0)
            {
                if (mantissa != 0)
                {
                    exponent = 1;
                    while ((mantissa & 0x400u) == 0)
                    {
                        mantissa <<= 1;
                        --exponent;
                    }
                    mantissa &= 0x3ffu;
                    bits |= (exponent + 112u) << 23;
                    bits |= mantissa << 13;
                }
            }
            else if (exponent == 0x1fu)
            {
                bits |= 0x7f800000u | (mantissa << 13);
            }
            else
            {
                bits |= (exponent + 112u) << 23 | (mantissa << 13);
            }
            float result = 0.0f;
            std::memcpy(&result, &bits, sizeof(result));
            return result;
        }

        TextureMipSubresource DownsampleRgba8(
            uint32_t source_width, uint32_t source_height,
            const std::vector<uint8_t> &source, TextureFormat format,
            TextureSemantic semantic)
        {
            const uint32_t width = std::max(1U, source_width / 2U);
            const uint32_t height = std::max(1U, source_height / 2U);
            TextureMipSubresource result{};
            result.width = width;
            result.height = height;
            result.pixels.resize(static_cast<size_t>(width) * height * 4U);
            const bool srgb_color = semantic == TextureSemantic::Color &&
                                    format == TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;

            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    float average[4]{};
                    bool has_covered_source = false;
                    uint32_t count = 0;
                    for (uint32_t oy = 0; oy < 2; ++oy)
                    {
                        const uint32_t sy = std::min(source_height - 1U, y * 2U + oy);
                        for (uint32_t ox = 0; ox < 2; ++ox)
                        {
                            const uint32_t sx = std::min(source_width - 1U, x * 2U + ox);
                            const size_t offset = (static_cast<size_t>(sy) * source_width + sx) * 4U;
                            for (uint32_t channel = 0; channel < 3; ++channel)
                            {
                                const float value = source[offset + channel] / 255.0f;
                                average[channel] += srgb_color ? SrgbToLinear(value) : value;
                            }
                            const float alpha = source[offset + 3] / 255.0f;
                            average[3] += alpha;
                            has_covered_source |= alpha >= kAlphaCoverageCutoff;
                            ++count;
                        }
                    }

                    const size_t output = (static_cast<size_t>(y) * width + x) * 4U;
                    if (semantic == TextureSemantic::Normal)
                    {
                        float normal[3]{};
                        for (uint32_t channel = 0; channel < 3; ++channel)
                        {
                            normal[channel] = average[channel] / static_cast<float>(count) * 2.0f - 1.0f;
                        }
                        const float length = std::sqrt(normal[0] * normal[0] +
                                                        normal[1] * normal[1] +
                                                        normal[2] * normal[2]);
                        if (length > 0.0f)
                        {
                            for (float &component : normal)
                            {
                                component /= length;
                            }
                        }
                        for (uint32_t channel = 0; channel < 3; ++channel)
                        {
                            result.pixels[output + channel] = ToByte(normal[channel] * 0.5f + 0.5f);
                        }
                    }
                    else
                    {
                        for (uint32_t channel = 0; channel < 3; ++channel)
                        {
                            const float value = average[channel] / static_cast<float>(count);
                            result.pixels[output + channel] = ToByte(
                                srgb_color ? LinearToSrgb(value) : value);
                        }
                    }
                    float alpha = average[3] / static_cast<float>(count);
                    if (semantic == TextureSemantic::OpacityMask && has_covered_source)
                    {
                        alpha = std::max(alpha, kAlphaCoverageCutoff);
                    }
                    result.pixels[output + 3] = ToByte(alpha);
                }
            }
            return result;
        }

        TextureMipSubresource DownsampleRgba16F(
            uint32_t source_width, uint32_t source_height,
            const std::vector<uint8_t> &source, TextureSemantic semantic)
        {
            const uint32_t width = std::max(1U, source_width / 2U);
            const uint32_t height = std::max(1U, source_height / 2U);
            TextureMipSubresource result{};
            result.width = width;
            result.height = height;
            result.pixels.resize(static_cast<size_t>(width) * height * 8U);
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    float average[4]{};
                    uint32_t count = 0;
                    for (uint32_t oy = 0; oy < 2; ++oy)
                    {
                        const uint32_t sy = std::min(source_height - 1U, y * 2U + oy);
                        for (uint32_t ox = 0; ox < 2; ++ox)
                        {
                            const uint32_t sx = std::min(source_width - 1U, x * 2U + ox);
                            const size_t offset = (static_cast<size_t>(sy) * source_width + sx) * 8U;
                            for (uint32_t channel = 0; channel < 4; ++channel)
                            {
                                uint16_t half = 0;
                                std::memcpy(&half, source.data() + offset + channel * 2U, sizeof(half));
                                average[channel] += HalfToFloat(half);
                            }
                            ++count;
                        }
                    }
                    const size_t output = (static_cast<size_t>(y) * width + x) * 8U;
                    if (semantic == TextureSemantic::Normal)
                    {
                        float normal[3]{};
                        for (uint32_t channel = 0; channel < 3; ++channel)
                        {
                            normal[channel] = average[channel] / static_cast<float>(count) * 2.0f - 1.0f;
                        }
                        const float length = std::sqrt(normal[0] * normal[0] +
                                                        normal[1] * normal[1] +
                                                        normal[2] * normal[2]);
                        if (length > 0.0f)
                        {
                            for (float &component : normal)
                            {
                                component /= length;
                            }
                        }
                        for (uint32_t channel = 0; channel < 3; ++channel)
                        {
                            const uint16_t half = FloatToHalf(normal[channel] * 0.5f + 0.5f);
                            std::memcpy(result.pixels.data() + output + channel * 2U,
                                        &half, sizeof(half));
                        }
                        const uint16_t alpha = FloatToHalf(average[3] / static_cast<float>(count));
                        std::memcpy(result.pixels.data() + output + 6U, &alpha, sizeof(alpha));
                    }
                    else
                    {
                        for (uint32_t channel = 0; channel < 4; ++channel)
                        {
                            const uint16_t half = FloatToHalf(
                                average[channel] / static_cast<float>(count));
                            std::memcpy(result.pixels.data() + output + channel * 2U,
                                        &half, sizeof(half));
                        }
                    }
                }
            }
            return result;
        }

        bool IsValidBase(const TextureData &texture, size_t bytes_per_pixel) noexcept
        {
            return texture.width != 0 && texture.height != 0 && bytes_per_pixel != 0 &&
                   texture.pixels.size() ==
                       static_cast<size_t>(texture.width) * texture.height * bytes_per_pixel;
        }

        bool ContainsToken(std::string_view value, std::string_view token) noexcept
        {
            return value.find(token) != std::string_view::npos;
        }
    }

    bool IsTextureFormatBlockCompressed(TextureFormat format) noexcept
    {
        return BlockByteCount(format) != 0;
    }

    std::size_t TextureFormatBlockByteCount(TextureFormat format) noexcept
    {
        return BlockByteCount(format);
    }

    std::size_t GetTextureMipByteCount(std::uint32_t width, std::uint32_t height,
                                       TextureFormat format) noexcept
    {
        if (width == 0 || height == 0)
        {
            return 0;
        }
        const std::size_t bytes_per_pixel = BytesPerPixel(format);
        if (bytes_per_pixel != 0)
        {
            const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
            if (pixel_count / width != height ||
                pixel_count > std::numeric_limits<std::size_t>::max() / bytes_per_pixel)
            {
                return 0;
            }
            return pixel_count * bytes_per_pixel;
        }
        const std::size_t block_bytes = BlockByteCount(format);
        if (block_bytes == 0)
        {
            return 0;
        }
        const std::size_t block_width = (static_cast<std::size_t>(width) + 3U) / 4U;
        const std::size_t block_height = (static_cast<std::size_t>(height) + 3U) / 4U;
        if (block_width > std::numeric_limits<std::size_t>::max() / block_height)
        {
            return 0;
        }
        const std::size_t block_count = block_width * block_height;
        if (block_count > std::numeric_limits<std::size_t>::max() / block_bytes)
        {
            return 0;
        }
        return block_count * block_bytes;
    }

    TextureSemantic ClassifyTextureSemantic(std::string_view path)
    {
        std::string lower(path);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (ContainsToken(lower, "normal") || ContainsToken(lower, "_nrm") ||
            ContainsToken(lower, "-nrm"))
        {
            return TextureSemantic::Normal;
        }
        if (ContainsToken(lower, "metal") || ContainsToken(lower, "rough") ||
            ContainsToken(lower, "occlusion") || ContainsToken(lower, "_ao") ||
            ContainsToken(lower, "-ao") || ContainsToken(lower, "orm"))
        {
            return TextureSemantic::PackedLinear;
        }
        if (ContainsToken(lower, "opacity") || ContainsToken(lower, "alpha") ||
            ContainsToken(lower, "mask"))
        {
            return TextureSemantic::OpacityMask;
        }
        if (ContainsToken(lower, "albedo") || ContainsToken(lower, "basecolor") ||
            ContainsToken(lower, "base_color") || ContainsToken(lower, "diffuse") ||
            ContainsToken(lower, "emissive"))
        {
            return TextureSemantic::Color;
        }
        return TextureSemantic::Generic;
    }

    bool GenerateTextureMipChain(TextureData &texture, TextureSemantic semantic,
                                 const TextureMipGenerationSettings &settings)
    {
        if (IsTextureFormatBlockCompressed(texture.format))
        {
            return false;
        }
        const size_t bytes_per_pixel = BytesPerPixel(texture.format);
        if (!IsValidBase(texture, bytes_per_pixel) || settings.max_dimension == 0)
        {
            return false;
        }
        if (!texture.mip_subresources.empty())
        {
            uint32_t expected_width = texture.width;
            uint32_t expected_height = texture.height;
            for (const TextureMipSubresource &level : texture.mip_subresources)
            {
                expected_width = std::max(1U, expected_width / 2U);
                expected_height = std::max(1U, expected_height / 2U);
                if (level.width != expected_width || level.height != expected_height ||
                    level.pixels.size() !=
                        static_cast<size_t>(level.width) * level.height * bytes_per_pixel)
                {
                    return false;
                }
            }
            texture.semantic = semantic;
            return true;
        }

        texture.semantic = semantic;
        uint32_t current_width = texture.width;
        uint32_t current_height = texture.height;
        std::vector<uint8_t> current_pixels = std::move(texture.pixels);
        auto downsample = [&](uint32_t width, uint32_t height,
                              const std::vector<uint8_t> &pixels)
        {
            return bytes_per_pixel == 4
                       ? DownsampleRgba8(width, height, pixels, texture.format, semantic)
                       : DownsampleRgba16F(width, height, pixels, semantic);
        };

        while (current_width > settings.max_dimension || current_height > settings.max_dimension)
        {
            TextureMipSubresource next = downsample(current_width, current_height, current_pixels);
            current_width = next.width;
            current_height = next.height;
            current_pixels = std::move(next.pixels);
        }
        texture.width = current_width;
        texture.height = current_height;
        texture.pixels = current_pixels;

        uint32_t level_count = 1;
        while ((current_width > 1 || current_height > 1) &&
               (settings.max_levels == 0 || level_count < settings.max_levels))
        {
            TextureMipSubresource next = downsample(current_width, current_height, current_pixels);
            current_width = next.width;
            current_height = next.height;
            current_pixels = next.pixels;
            texture.mip_subresources.push_back(std::move(next));
            ++level_count;
        }
        return true;
    }

    bool IsTextureMipChainValid(const TextureData &texture) noexcept
    {
        const size_t expected_base_bytes =
            GetTextureMipByteCount(texture.width, texture.height, texture.format);
        if (texture.width == 0 || texture.height == 0 || expected_base_bytes == 0)
        {
            return texture.mip_subresources.empty() && texture.pixels.empty();
        }
        if (texture.pixels.size() != expected_base_bytes)
        {
            return false;
        }
        uint32_t expected_width = texture.width;
        uint32_t expected_height = texture.height;
        for (const TextureMipSubresource &level : texture.mip_subresources)
        {
            expected_width = std::max(1U, expected_width / 2U);
            expected_height = std::max(1U, expected_height / 2U);
            if (level.width != expected_width || level.height != expected_height ||
                level.pixels.size() != GetTextureMipByteCount(level.width, level.height,
                                                               texture.format))
            {
                return false;
            }
        }
        return true;
    }
}
