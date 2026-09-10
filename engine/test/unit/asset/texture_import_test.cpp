#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <vector>

#include "asset/native_texture.h"
#include "asset/texture_importer.h"
#include "data/texture_mipmap.h"

namespace
{
    kpengine::image_io::ImageBuffer MakeImage()
    {
        kpengine::image_io::ImageBuffer image{};
        image.width = 4;
        image.height = 2;
        image.format = kpengine::image_io::ImagePixelFormat::Rgba8;
        image.pixels.resize(4u * 2u * 4u, 255);
        return image;
    }

    kpengine::image_io::ImageBuffer MakeOddPatternImage()
    {
        kpengine::image_io::ImageBuffer image{};
        image.width = 5;
        image.height = 3;
        image.format = kpengine::image_io::ImagePixelFormat::Rgba8;
        image.pixels.resize(image.width * image.height * 4U);
        for (std::size_t y = 0; y < image.height; ++y)
        {
            for (std::size_t x = 0; x < image.width; ++x)
            {
                const std::size_t offset = (y * image.width + x) * 4U;
                image.pixels[offset + 0U] = static_cast<std::uint8_t>(x * 37U + y * 11U);
                image.pixels[offset + 1U] = static_cast<std::uint8_t>(x * 13U + y * 53U);
                image.pixels[offset + 2U] = static_cast<std::uint8_t>(x * 71U + y * 7U);
                image.pixels[offset + 3U] = static_cast<std::uint8_t>(x * 29U + y * 61U);
            }
        }
        return image;
    }

    kpengine::image_io::ImageBuffer MakeQualityFixture(
        kpengine::data::TextureSemantic semantic)
    {
        kpengine::image_io::ImageBuffer image{};
        image.width = 13;
        image.height = 9;
        image.format = kpengine::image_io::ImagePixelFormat::Rgba8;
        image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4U);
        for (std::uint32_t y = 0; y < image.height; ++y)
        {
            for (std::uint32_t x = 0; x < image.width; ++x)
            {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * image.width + x) * 4U;
                if (semantic == kpengine::data::TextureSemantic::Normal)
                {
                    const int normal_x = -56 + static_cast<int>(x) * 9;
                    const int normal_y = -40 + static_cast<int>(y) * 10;
                    image.pixels[offset + 0U] = static_cast<std::uint8_t>(128 + normal_x);
                    image.pixels[offset + 1U] = static_cast<std::uint8_t>(128 + normal_y);
                    image.pixels[offset + 2U] = 255;
                }
                else
                {
                    image.pixels[offset + 0U] = static_cast<std::uint8_t>(x * 19U + y * 7U);
                    image.pixels[offset + 1U] = static_cast<std::uint8_t>(x * 5U + y * 23U);
                    image.pixels[offset + 2U] = static_cast<std::uint8_t>(x * 31U + y * 11U);
                }
                image.pixels[offset + 3U] =
                    static_cast<std::uint8_t>(x * 17U + y * 29U + 23U);
            }
        }
        return image;
    }

    std::uint16_t ReadLittleEndian16(const std::uint8_t *source) noexcept
    {
        return static_cast<std::uint16_t>(source[0]) |
               static_cast<std::uint16_t>(source[1]) << 8U;
    }

    std::array<std::uint8_t, 3> DecodeRgb565(std::uint16_t packed) noexcept
    {
        const std::uint8_t red = static_cast<std::uint8_t>((packed >> 11U) & 0x1FU);
        const std::uint8_t green = static_cast<std::uint8_t>((packed >> 5U) & 0x3FU);
        const std::uint8_t blue = static_cast<std::uint8_t>(packed & 0x1FU);
        return {static_cast<std::uint8_t>((red * 255U + 15U) / 31U),
                static_cast<std::uint8_t>((green * 255U + 31U) / 63U),
                static_cast<std::uint8_t>((blue * 255U + 15U) / 31U)};
    }

    std::array<std::uint8_t, 8> DecodeBc4Palette(const std::uint8_t *block) noexcept
    {
        std::array<std::uint8_t, 8> palette{};
        palette[0] = block[0];
        palette[1] = block[1];
        if (palette[0] > palette[1])
        {
            palette[2] = static_cast<std::uint8_t>((6U * palette[0] + palette[1]) / 7U);
            palette[3] = static_cast<std::uint8_t>((5U * palette[0] + 2U * palette[1]) / 7U);
            palette[4] = static_cast<std::uint8_t>((4U * palette[0] + 3U * palette[1]) / 7U);
            palette[5] = static_cast<std::uint8_t>((3U * palette[0] + 4U * palette[1]) / 7U);
            palette[6] = static_cast<std::uint8_t>((2U * palette[0] + 5U * palette[1]) / 7U);
            palette[7] = static_cast<std::uint8_t>((palette[0] + 6U * palette[1]) / 7U);
        }
        else
        {
            palette[2] = static_cast<std::uint8_t>((4U * palette[0] + palette[1]) / 5U);
            palette[3] = static_cast<std::uint8_t>((3U * palette[0] + 2U * palette[1]) / 5U);
            palette[4] = static_cast<std::uint8_t>((2U * palette[0] + 3U * palette[1]) / 5U);
            palette[5] = static_cast<std::uint8_t>((palette[0] + 4U * palette[1]) / 5U);
            palette[6] = 0;
            palette[7] = 255;
        }
        return palette;
    }

    std::array<std::uint8_t, 16> DecodeBc4Block(const std::uint8_t *block) noexcept
    {
        const std::array<std::uint8_t, 8> palette = DecodeBc4Palette(block);
        std::uint64_t indices = 0;
        for (std::size_t byte = 0; byte < 6; ++byte)
        {
            indices |= static_cast<std::uint64_t>(block[byte + 2U]) << (byte * 8U);
        }

        std::array<std::uint8_t, 16> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            result[index] = palette[(indices >> (index * 3U)) & 0x7U];
        }
        return result;
    }

    std::array<std::array<std::uint8_t, 4>, 16> DecodeBc1Block(
        const std::uint8_t *block) noexcept
    {
        const std::uint16_t endpoint0 = ReadLittleEndian16(block);
        const std::uint16_t endpoint1 = ReadLittleEndian16(block + 2U);
        const std::array<std::uint8_t, 3> color0 = DecodeRgb565(endpoint0);
        const std::array<std::uint8_t, 3> color1 = DecodeRgb565(endpoint1);
        std::array<std::array<std::uint8_t, 4>, 4> palette{};
        palette[0] = {color0[0], color0[1], color0[2], 255};
        palette[1] = {color1[0], color1[1], color1[2], 255};
        if (endpoint0 > endpoint1)
        {
            palette[2] = {static_cast<std::uint8_t>((2U * color0[0] + color1[0] + 1U) / 3U),
                          static_cast<std::uint8_t>((2U * color0[1] + color1[1] + 1U) / 3U),
                          static_cast<std::uint8_t>((2U * color0[2] + color1[2] + 1U) / 3U), 255};
            palette[3] = {static_cast<std::uint8_t>((color0[0] + 2U * color1[0] + 1U) / 3U),
                          static_cast<std::uint8_t>((color0[1] + 2U * color1[1] + 1U) / 3U),
                          static_cast<std::uint8_t>((color0[2] + 2U * color1[2] + 1U) / 3U), 255};
        }
        else
        {
            palette[2] = {static_cast<std::uint8_t>((color0[0] + color1[0]) / 2U),
                          static_cast<std::uint8_t>((color0[1] + color1[1]) / 2U),
                          static_cast<std::uint8_t>((color0[2] + color1[2]) / 2U), 255};
            palette[3] = {0, 0, 0, 0};
        }

        std::uint32_t indices = 0;
        for (std::size_t byte = 0; byte < 4; ++byte)
        {
            indices |= static_cast<std::uint32_t>(block[byte + 4U]) << (byte * 8U);
        }
        std::array<std::array<std::uint8_t, 4>, 16> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            result[index] = palette[(indices >> (index * 2U)) & 0x3U];
        }
        return result;
    }

    std::vector<std::uint8_t> DecodeBcMip(const std::vector<std::uint8_t> &compressed,
                                          std::uint32_t width, std::uint32_t height,
                                          TextureFormat format)
    {
        const std::size_t block_width = (static_cast<std::size_t>(width) + 3U) / 4U;
        const std::size_t block_height = (static_cast<std::size_t>(height) + 3U) / 4U;
        const std::size_t block_bytes =
            format == TextureFormat::TEXTURE_FORMAT_BC4_UNORM ? 8U :
            format == TextureFormat::TEXTURE_FORMAT_BC5_UNORM ? 16U : 16U;
        if (compressed.size() != block_width * block_height * block_bytes)
        {
            return {};
        }

        std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height * 4U, 0);
        for (std::size_t block_y = 0; block_y < block_height; ++block_y)
        {
            for (std::size_t block_x = 0; block_x < block_width; ++block_x)
            {
                const std::uint8_t *block = compressed.data() +
                    (block_y * block_width + block_x) * block_bytes;
                const std::array<std::uint8_t, 16> channel0 = DecodeBc4Block(block);
                const std::array<std::uint8_t, 16> channel1 =
                    format == TextureFormat::TEXTURE_FORMAT_BC5_UNORM
                        ? DecodeBc4Block(block + 8U)
                        : channel0;
                const std::array<std::array<std::uint8_t, 4>, 16> color =
                    format == TextureFormat::TEXTURE_FORMAT_BC4_UNORM ||
                            format == TextureFormat::TEXTURE_FORMAT_BC5_UNORM
                        ? std::array<std::array<std::uint8_t, 4>, 16>{}
                        : DecodeBc1Block(block + 8U);
                for (std::size_t local_y = 0; local_y < 4; ++local_y)
                {
                    for (std::size_t local_x = 0; local_x < 4; ++local_x)
                    {
                        const std::size_t x = block_x * 4U + local_x;
                        const std::size_t y = block_y * 4U + local_y;
                        if (x >= width || y >= height)
                        {
                            continue;
                        }
                        const std::size_t block_index = local_y * 4U + local_x;
                        const std::size_t output = (y * width + x) * 4U;
                        if (format == TextureFormat::TEXTURE_FORMAT_BC4_UNORM)
                        {
                            result[output + 0U] = channel0[block_index];
                        }
                        else if (format == TextureFormat::TEXTURE_FORMAT_BC5_UNORM)
                        {
                            result[output + 0U] = channel0[block_index];
                            result[output + 1U] = channel1[block_index];
                        }
                        else
                        {
                            result[output + 0U] = color[block_index][0];
                            result[output + 1U] = color[block_index][1];
                            result[output + 2U] = color[block_index][2];
                            result[output + 3U] = channel0[block_index];
                        }
                    }
                }
            }
        }
        return result;
    }

    struct ScalarQuality
    {
        double squared_error = 0.0;
        double sample_count = 0.0;
        double maximum_error = 0.0;
        std::uint64_t classification_disagreements = 0;

        double Rmse() const noexcept
        {
            return std::sqrt(squared_error / std::max(1.0, sample_count));
        }
    };

    ScalarQuality MeasureChannelQuality(const kpengine::data::TextureData &source,
                                        const kpengine::data::TextureData &compressed,
                                        std::size_t channel, std::uint8_t cutoff = 0)
    {
        ScalarQuality result{};
        for (std::size_t level = 0; level < source.GetMipLevelCount(); ++level)
        {
            const auto &source_pixels = level == 0
                ? source.pixels
                : source.mip_subresources[level - 1U].pixels;
            const auto &compressed_level = level == 0
                ? compressed.pixels
                : compressed.mip_subresources[level - 1U].pixels;
            const std::uint32_t width = level == 0
                ? source.width
                : source.mip_subresources[level - 1U].width;
            const std::uint32_t height = level == 0
                ? source.height
                : source.mip_subresources[level - 1U].height;
            const std::vector<std::uint8_t> decoded =
                DecodeBcMip(compressed_level, width, height, compressed.format);
            for (std::size_t pixel = 0; pixel < source_pixels.size(); pixel += 4U)
            {
                const double difference = static_cast<double>(source_pixels[pixel + channel]) -
                                          decoded[pixel + channel];
                result.squared_error += difference * difference;
                result.sample_count += 1.0;
                result.maximum_error = std::max(result.maximum_error, std::abs(difference));
                if (cutoff != 0 &&
                    (source_pixels[pixel + channel] < cutoff) !=
                        (decoded[pixel + channel] < cutoff))
                {
                    ++result.classification_disagreements;
                }
            }
        }
        return result;
    }

    ScalarQuality MeasureRgbQuality(const kpengine::data::TextureData &source,
                                    const kpengine::data::TextureData &compressed)
    {
        ScalarQuality result{};
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const ScalarQuality channel_quality =
                MeasureChannelQuality(source, compressed, channel);
            result.squared_error += channel_quality.squared_error;
            result.sample_count += channel_quality.sample_count;
            result.maximum_error = std::max(result.maximum_error, channel_quality.maximum_error);
        }
        return result;
    }

    double MeasureRgbPsnr(const ScalarQuality &quality) noexcept
    {
        if (quality.squared_error == 0.0)
        {
            return std::numeric_limits<double>::infinity();
        }
        return 10.0 * std::log10((255.0 * 255.0 * quality.sample_count) /
                                 quality.squared_error);
    }

    struct AngularQuality
    {
        std::vector<double> errors;

        double MeanDegrees() const noexcept
        {
            if (errors.empty())
            {
                return 0.0;
            }
            double total = 0.0;
            for (const double error : errors)
            {
                total += error;
            }
            return total / static_cast<double>(errors.size());
        }

        double P95Degrees() const
        {
            if (errors.empty())
            {
                return 0.0;
            }
            std::vector<double> sorted = errors;
            std::sort(sorted.begin(), sorted.end());
            const std::size_t index =
                std::min(sorted.size() - 1U, (sorted.size() * 95U + 99U) / 100U - 1U);
            return sorted[index];
        }
    };

    AngularQuality MeasureNormalQuality(const kpengine::data::TextureData &source,
                                        const kpengine::data::TextureData &compressed)
    {
        AngularQuality result{};
        for (std::size_t level = 0; level < source.GetMipLevelCount(); ++level)
        {
            const auto &source_pixels = level == 0
                ? source.pixels
                : source.mip_subresources[level - 1U].pixels;
            const auto &compressed_level = level == 0
                ? compressed.pixels
                : compressed.mip_subresources[level - 1U].pixels;
            const std::uint32_t width = level == 0
                ? source.width
                : source.mip_subresources[level - 1U].width;
            const std::uint32_t height = level == 0
                ? source.height
                : source.mip_subresources[level - 1U].height;
            const std::vector<std::uint8_t> decoded =
                DecodeBcMip(compressed_level, width, height, compressed.format);
            for (std::size_t pixel = 0; pixel < source_pixels.size(); pixel += 4U)
            {
                const auto make_normal = [](const std::uint8_t *channels)
                {
                    const double x = static_cast<double>(channels[0]) / 127.5 - 1.0;
                    const double y = static_cast<double>(channels[1]) / 127.5 - 1.0;
                    const double z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
                    const double length = std::sqrt(x * x + y * y + z * z);
                    return std::array<double, 3>{x / length, y / length, z / length};
                };
                const std::array<double, 3> expected = make_normal(source_pixels.data() + pixel);
                const std::array<double, 3> actual = make_normal(decoded.data() + pixel);
                const double dot = std::clamp(expected[0] * actual[0] +
                                                  expected[1] * actual[1] +
                                                  expected[2] * actual[2],
                                              -1.0, 1.0);
                result.errors.push_back(std::acos(dot) * 180.0 / 3.14159265358979323846);
            }
        }
        return result;
    }
}

TEST(TextureImportTest, ImportAndCookAreIndependentAndRoundTripNativeMips)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kpengine_texture_import_test.png";
    const auto encoded = kpengine::image_io::EncodePngMemory(MakeImage());
    ASSERT_TRUE(encoded.result.success);
    {
        std::ofstream file(root, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(encoded.bytes.data()),
                   static_cast<std::streamsize>(encoded.bytes.size()));
        ASSERT_TRUE(file.good());
    }

    kpengine::asset::TextureImportRequest request{};
    request.source_path = root;
    request.settings.semantic = kpengine::data::TextureSemantic::Color;
    request.settings.max_dimension = 2;
    const kpengine::asset::ImportedTexture imported =
        kpengine::asset::TextureImporter{}.Import(request);
    const kpengine::asset::CookedTexture cooked =
        kpengine::asset::TextureCooker{}.Cook(imported);
    EXPECT_NO_THROW(kpengine::asset::ValidateNativeTextureProductStructure(cooked.bytes));
    const kpengine::asset::NativeTextureProduct product =
        kpengine::asset::DeserializeNativeTexture(cooked.bytes);

    EXPECT_EQ(imported.image.width, 4u);
    EXPECT_EQ(product.data.width, 2u);
    EXPECT_EQ(product.data.height, 1u);
    EXPECT_EQ(product.data.GetMipLevelCount(), 2u);
    EXPECT_EQ(cooked.product_hash, kpengine::asset::Sha256(cooked.bytes));

    std::error_code error;
    std::filesystem::remove(root, error);
}

TEST(TextureImportTest, CooksSemanticBlockFormatsWithValidatedMipSizes)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeImage();
    source.settings.semantic = kpengine::data::TextureSemantic::Normal;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::PreferBlockCompression;

    const kpengine::asset::CookedTexture cooked = kpengine::asset::TextureCooker{}.Cook(source);
    EXPECT_EQ(cooked.data.format, TextureFormat::TEXTURE_FORMAT_BC5_UNORM);
    EXPECT_EQ(cooked.data.pixels.size(),
              kpengine::data::GetTextureMipByteCount(cooked.data.width, cooked.data.height,
                                                      cooked.data.format));
    EXPECT_TRUE(kpengine::data::IsTextureMipChainValid(cooked.data));

    const kpengine::asset::NativeTextureProduct product =
        kpengine::asset::DeserializeNativeTexture(cooked.bytes);
    EXPECT_EQ(product.data.format, TextureFormat::TEXTURE_FORMAT_BC5_UNORM);
    EXPECT_EQ(product.data.GetTotalByteCount(), cooked.data.GetTotalByteCount());

    source.settings.semantic = kpengine::data::TextureSemantic::OpacityMask;
    EXPECT_EQ(kpengine::asset::TextureCooker{}.Cook(source).data.format,
              TextureFormat::TEXTURE_FORMAT_BC4_UNORM);
    source.settings.semantic = kpengine::data::TextureSemantic::PackedLinear;
    EXPECT_EQ(kpengine::asset::TextureCooker{}.Cook(source).data.format,
              TextureFormat::TEXTURE_FORMAT_BC3_UNORM);
}

TEST(TextureImportTest, RequiredBlockCompressionIsDeterministic)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeImage();
    source.settings.semantic = kpengine::data::TextureSemantic::Color;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;
    const kpengine::asset::CookedTexture first = kpengine::asset::TextureCooker{}.Cook(source);
    const kpengine::asset::CookedTexture second = kpengine::asset::TextureCooker{}.Cook(source);
    EXPECT_EQ(first.data.format, TextureFormat::TEXTURE_FORMAT_BC3_SRGB);
    EXPECT_EQ(first.bytes, second.bytes);
    EXPECT_EQ(first.product_hash, second.product_hash);
}

TEST(TextureImportTest, ReferenceEncoderPreservesOddEdgeBlocksDeterministically)
{
    const std::array<kpengine::data::TextureSemantic, 3> semantics{
        kpengine::data::TextureSemantic::Color,
        kpengine::data::TextureSemantic::OpacityMask,
        kpengine::data::TextureSemantic::Normal};
    for (const auto semantic : semantics)
    {
        kpengine::asset::ImportedTexture source{};
        source.image = MakeOddPatternImage();
        source.settings.semantic = semantic;
        source.settings.compression =
            kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;

        const kpengine::asset::TextureCooker cooker;
        const kpengine::asset::CookedTexture first = cooker.Cook(source);
        const kpengine::asset::CookedTexture second = cooker.Cook(source);
        EXPECT_EQ(first.bytes, second.bytes);
        EXPECT_EQ(first.product_hash, second.product_hash);
        EXPECT_EQ(first.data.pixels.size(),
                  kpengine::data::GetTextureMipByteCount(first.data.width, first.data.height,
                                                         first.data.format));
        EXPECT_TRUE(kpengine::data::IsTextureMipChainValid(first.data));
    }
}

TEST(TextureImportTest, ReportsReferenceEncoderMetricsPerBcFormat)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeOddPatternImage();
    source.settings.semantic = kpengine::data::TextureSemantic::OpacityMask;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;

    const kpengine::asset::TextureCooker cooker;
    const kpengine::data::TextureData prepared = cooker.Prepare(source);
    kpengine::asset::TextureBcEncodingMetrics metrics{};
    const kpengine::asset::CookedTexture cooked = cooker.CookPrepared(
        prepared, source.settings.compression, source.settings.bc_encoder,
        source.settings.bc_quality, &metrics);

    EXPECT_EQ(cooked.data.format, TextureFormat::TEXTURE_FORMAT_BC4_UNORM);
    EXPECT_EQ(metrics.bc3.block_count, 0u);
    EXPECT_EQ(metrics.bc4.block_count, 4u);
    EXPECT_DOUBLE_EQ(metrics.bc4.source_megapixels, 18.0 / 1000000.0);
    EXPECT_GT(metrics.bc4.encode_seconds, 0.0);
    EXPECT_EQ(metrics.bc5.block_count, 0u);
}

TEST(TextureImportTest, CancellationStopsBlockEncodingBeforePublishingAProduct)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeOddPatternImage();
    source.settings.semantic = kpengine::data::TextureSemantic::Color;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;

    const kpengine::asset::TextureCooker cooker;
    const kpengine::data::TextureData prepared = cooker.Prepare(source);
    kpengine::asset::TextureBcEncodingMetrics metrics{};
    try
    {
        cooker.CookPrepared(prepared, source.settings.compression, source.settings.bc_encoder,
                            source.settings.bc_quality, &metrics, [] { return true; });
        FAIL() << "expected texture cook cancellation";
    }
    catch (const kpengine::asset::TextureCookError &error)
    {
        EXPECT_EQ(error.Code(), kpengine::asset::TextureCookErrorCode::Cancelled);
    }
}

TEST(TextureImportTest, RgbcxEncoderPreservesSemanticRoutesAndDeterminism)
{
    const std::array<kpengine::data::TextureSemantic, 3> semantics{
        kpengine::data::TextureSemantic::Color,
        kpengine::data::TextureSemantic::OpacityMask,
        kpengine::data::TextureSemantic::Normal};
    for (const auto semantic : semantics)
    {
        kpengine::asset::ImportedTexture source{};
        source.image = MakeOddPatternImage();
        source.settings.semantic = semantic;
        source.settings.compression =
            kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;
        source.settings.bc_encoder = kpengine::asset::TextureBcEncoder::RgbcxV113;

        const kpengine::asset::TextureCooker cooker;
        const kpengine::asset::CookedTexture first = cooker.Cook(source);
        const kpengine::asset::CookedTexture second = cooker.Cook(source);
        EXPECT_EQ(first.bytes, second.bytes);
        EXPECT_EQ(first.product_hash, second.product_hash);
        EXPECT_TRUE(kpengine::data::IsTextureMipChainValid(first.data));
    }
}

TEST(TextureImportTest, RgbcxInitializationAndEncodingAreSafeConcurrently)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeOddPatternImage();
    source.settings.semantic = kpengine::data::TextureSemantic::Color;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;
    source.settings.bc_encoder = kpengine::asset::TextureBcEncoder::RgbcxV113;

    constexpr std::size_t worker_count = 8;
    std::vector<std::future<kpengine::asset::CookedTexture>> cooks;
    cooks.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker)
    {
        cooks.push_back(std::async(std::launch::async, [source]
        {
            return kpengine::asset::TextureCooker{}.Cook(source);
        }));
    }

    const kpengine::asset::CookedTexture first = cooks.front().get();
    for (std::size_t worker = 1; worker < cooks.size(); ++worker)
    {
        const kpengine::asset::CookedTexture current = cooks[worker].get();
        EXPECT_EQ(current.bytes, first.bytes);
        EXPECT_EQ(current.product_hash, first.product_hash);
    }
}

TEST(TextureImportTest, IndependentBcDecoderEnforcesSemanticQualityGate)
{
    const std::array<kpengine::data::TextureSemantic, 4> semantics{
        kpengine::data::TextureSemantic::Color,
        kpengine::data::TextureSemantic::PackedLinear,
        kpengine::data::TextureSemantic::OpacityMask,
        kpengine::data::TextureSemantic::Normal};
    for (const auto semantic : semantics)
    {
        SCOPED_TRACE(static_cast<int>(semantic));
        kpengine::asset::ImportedTexture source{};
        source.image = MakeQualityFixture(semantic);
        source.settings.semantic = semantic;
        source.settings.compression =
            kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;

        const kpengine::asset::TextureCooker cooker;
        const kpengine::data::TextureData prepared = cooker.Prepare(source);
        const kpengine::asset::CookedTexture reference = cooker.CookPrepared(
            prepared, source.settings.compression,
            kpengine::asset::TextureBcEncoder::ReferenceV1,
            kpengine::asset::TextureBcQuality::Balanced);
        const kpengine::asset::CookedTexture candidate = cooker.CookPrepared(
            prepared, source.settings.compression,
            kpengine::asset::TextureBcEncoder::RgbcxV113,
            kpengine::asset::TextureBcQuality::Balanced);

        ASSERT_EQ(reference.data.format, candidate.data.format);
        ASSERT_TRUE(kpengine::data::IsTextureMipChainValid(reference.data));
        ASSERT_TRUE(kpengine::data::IsTextureMipChainValid(candidate.data));

        if (semantic == kpengine::data::TextureSemantic::Color ||
            semantic == kpengine::data::TextureSemantic::PackedLinear)
        {
            const ScalarQuality reference_rgb = MeasureRgbQuality(prepared, reference.data);
            const ScalarQuality candidate_rgb = MeasureRgbQuality(prepared, candidate.data);
            const double reference_psnr = MeasureRgbPsnr(reference_rgb);
            const double candidate_psnr = MeasureRgbPsnr(candidate_rgb);
            EXPECT_GE(candidate_psnr + 0.5, reference_psnr);
            EXPECT_LE(candidate_rgb.squared_error, reference_rgb.squared_error + 1.0e-9);
            if (semantic == kpengine::data::TextureSemantic::PackedLinear)
            {
                for (std::size_t channel = 0; channel < 4; ++channel)
                {
                    const ScalarQuality reference_channel =
                        MeasureChannelQuality(prepared, reference.data, channel);
                    const ScalarQuality candidate_channel =
                        MeasureChannelQuality(prepared, candidate.data, channel);
                    EXPECT_LE(candidate_channel.Rmse(), reference_channel.Rmse() + 1.0e-6);
                }
            }
        }
        else if (semantic == kpengine::data::TextureSemantic::OpacityMask)
        {
            const ScalarQuality reference_opacity =
                MeasureChannelQuality(prepared, reference.data, 0, 128);
            const ScalarQuality candidate_opacity =
                MeasureChannelQuality(prepared, candidate.data, 0, 128);
            EXPECT_LE(candidate_opacity.Rmse(), reference_opacity.Rmse() + 1.0e-6);
            EXPECT_LE(candidate_opacity.maximum_error, reference_opacity.maximum_error + 1.0e-6);
            EXPECT_LE(candidate_opacity.classification_disagreements,
                      reference_opacity.classification_disagreements);
        }
        else
        {
            const AngularQuality reference_normal =
                MeasureNormalQuality(prepared, reference.data);
            const AngularQuality candidate_normal =
                MeasureNormalQuality(prepared, candidate.data);
            EXPECT_LE(candidate_normal.MeanDegrees(), reference_normal.MeanDegrees() + 1.0e-6);
            EXPECT_LE(candidate_normal.P95Degrees(), reference_normal.P95Degrees() + 1.0e-6);
        }
    }
}

TEST(TextureImportTest, Bc4OpacityUsesRedChannelAndAllInterpolatedEntries)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeImage();
    source.image.height = 4;
    source.image.pixels.resize(4U * 4U * 4U, 255);
    for (std::size_t index = 0; index < 16; ++index)
    {
        source.image.pixels[index * 4U + 0U] = 0;
        source.image.pixels[index * 4U + 3U] = 128;
    }
    source.image.pixels[0] = 255;
    source.image.pixels[4] = 0;
    source.image.pixels[8] = 37;
    source.settings.semantic = kpengine::data::TextureSemantic::OpacityMask;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;

    const kpengine::asset::CookedTexture cooked = kpengine::asset::TextureCooker{}.Cook(source);
    ASSERT_EQ(cooked.data.format, TextureFormat::TEXTURE_FORMAT_BC4_UNORM);
    ASSERT_GE(cooked.data.pixels.size(), 8U);
    EXPECT_EQ(cooked.data.pixels[0], 255U);
    EXPECT_EQ(cooked.data.pixels[1], 0U);

    std::uint64_t indices = 0;
    for (std::size_t index = 0; index < 6; ++index)
    {
        indices |= static_cast<std::uint64_t>(cooked.data.pixels[2U + index]) << (index * 8U);
    }
    EXPECT_EQ((indices >> 6U) & 0x7U, 7U);
}
