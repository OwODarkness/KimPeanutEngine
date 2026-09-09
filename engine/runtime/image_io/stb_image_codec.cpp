#include "image_io/image_codec.h"

#include <limits>
#include <cstring>
#include <utility>

#include <stb_image/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image/stb_image_write.h>

namespace kpengine::image_io
{
    namespace
    {
        constexpr size_t kRgba8PixelBytes = 4;

        ImageIoResult Failure(std::string diagnostic)
        {
            return {false, std::move(diagnostic)};
        }

        ImageMetadataResult BuildMetadata(int width, int height, bool hdr)
        {
            if (width <= 0 || height <= 0)
            {
                return {Failure("Image metadata has an invalid extent"), {}};
            }
            const std::uint64_t bytes_per_pixel = hdr ? sizeof(float) * 4u : 4u;
            const std::uint64_t pixel_count = static_cast<std::uint64_t>(width) *
                                              static_cast<std::uint64_t>(height);
            if (pixel_count > std::numeric_limits<std::uint64_t>::max() / bytes_per_pixel)
            {
                return {Failure("Image metadata exceeds the supported byte range"), {}};
            }
            ImageMetadata metadata{};
            metadata.width = static_cast<std::uint32_t>(width);
            metadata.height = static_cast<std::uint32_t>(height);
            metadata.decoded_format = hdr ? ImagePixelFormat::Rgba32Float
                                           : ImagePixelFormat::Rgba8;
            metadata.decoded_byte_count = pixel_count * bytes_per_pixel;
            return {{true, {}}, metadata};
        }

        class StbImageCodec final : public IImageCodec
        {
        public:
            ImageDecodeResult DecodeFile(const std::string &path) const override
            {
                int width = 0;
                int height = 0;
                // Asset UVs use the engine's bottom-origin convention. Keep
                // this state thread-local so concurrent image loads do not
                // race through stb_image's process-wide decoder setting.
                stbi_set_flip_vertically_on_load_thread(1);
                if (stbi_is_hdr(path.c_str()) != 0)
                {
                    float *decoded = stbi_loadf(path.c_str(), &width, &height, nullptr,
                                                STBI_rgb_alpha);
                    if (decoded == nullptr)
                    {
                        const char *reason = stbi_failure_reason();
                        return {Failure(reason ? reason : "HDR image decoder failed"), {}};
                    }
                    if (width <= 0 || height <= 0)
                    {
                        stbi_image_free(decoded);
                        return {Failure("Decoded HDR image has an invalid extent"), {}};
                    }

                    ImageBuffer image;
                    image.width = static_cast<uint32_t>(width);
                    image.height = static_cast<uint32_t>(height);
                    image.format = ImagePixelFormat::Rgba32Float;
                    const size_t byte_count = image.ExpectedByteCount();
                    if (byte_count == 0)
                    {
                        stbi_image_free(decoded);
                        return {Failure("Decoded HDR image exceeds the supported byte range"), {}};
                    }
                    const auto *bytes = reinterpret_cast<const uint8_t *>(decoded);
                    image.pixels.assign(bytes, bytes + byte_count);
                    stbi_image_free(decoded);
                    return {{true, {}}, std::move(image)};
                }

                stbi_uc *decoded = stbi_load(path.c_str(), &width, &height, nullptr,
                                             STBI_rgb_alpha);
                if (decoded == nullptr)
                {
                    const char *reason = stbi_failure_reason();
                    return {Failure(reason ? reason : "Image decoder failed"), {}};
                }

                if (width <= 0 || height <= 0)
                {
                    stbi_image_free(decoded);
                    return {Failure("Decoded image has an invalid extent"), {}};
                }

                ImageBuffer image;
                image.width = static_cast<uint32_t>(width);
                image.height = static_cast<uint32_t>(height);
                image.format = ImagePixelFormat::Rgba8;
                const size_t byte_count = image.ExpectedByteCount();
                if (byte_count == 0)
                {
                    stbi_image_free(decoded);
                    return {Failure("Decoded image exceeds the supported byte range"), {}};
                }

                image.pixels.assign(decoded, decoded + byte_count);
                stbi_image_free(decoded);
                return {{true, {}}, std::move(image)};
            }

            ImageDecodeResult DecodeMemory(const std::byte *data, size_t size) const override
            {
                if (data == nullptr || size == 0 || size > static_cast<size_t>(std::numeric_limits<int>::max()))
                {
                    return {Failure("encoded image memory is empty or too large"), {}};
                }
                const auto *bytes = reinterpret_cast<const stbi_uc *>(data);
                int width = 0;
                int height = 0;
                stbi_set_flip_vertically_on_load_thread(1);
                if (stbi_is_hdr_from_memory(bytes, static_cast<int>(size)) != 0)
                {
                    float *decoded = stbi_loadf_from_memory(bytes, static_cast<int>(size), &width,
                                                            &height, nullptr, STBI_rgb_alpha);
                    if (decoded == nullptr)
                    {
                        const char *reason = stbi_failure_reason();
                        return {Failure(reason ? reason : "HDR image decoder failed"), {}};
                    }
                    if (width <= 0 || height <= 0)
                    {
                        stbi_image_free(decoded);
                        return {Failure("Decoded HDR image has an invalid extent"), {}};
                    }
                    ImageBuffer image;
                    image.width = static_cast<uint32_t>(width);
                    image.height = static_cast<uint32_t>(height);
                    image.format = ImagePixelFormat::Rgba32Float;
                    const size_t byte_count = image.ExpectedByteCount();
                    if (byte_count == 0)
                    {
                        stbi_image_free(decoded);
                        return {Failure("Decoded HDR image exceeds the supported byte range"), {}};
                    }
                    const auto *decoded_bytes = reinterpret_cast<const uint8_t *>(decoded);
                    image.pixels.assign(decoded_bytes, decoded_bytes + byte_count);
                    stbi_image_free(decoded);
                    return {{true, {}}, std::move(image)};
                }

                stbi_uc *decoded = stbi_load_from_memory(bytes, static_cast<int>(size), &width,
                                                         &height, nullptr, STBI_rgb_alpha);
                if (decoded == nullptr)
                {
                    const char *reason = stbi_failure_reason();
                    return {Failure(reason ? reason : "Image decoder failed"), {}};
                }
                if (width <= 0 || height <= 0)
                {
                    stbi_image_free(decoded);
                    return {Failure("Decoded image has an invalid extent"), {}};
                }
                ImageBuffer image;
                image.width = static_cast<uint32_t>(width);
                image.height = static_cast<uint32_t>(height);
                image.format = ImagePixelFormat::Rgba8;
                const size_t byte_count = image.ExpectedByteCount();
                if (byte_count == 0)
                {
                    stbi_image_free(decoded);
                    return {Failure("Decoded image exceeds the supported byte range"), {}};
                }
                image.pixels.assign(decoded, decoded + byte_count);
                stbi_image_free(decoded);
                return {{true, {}}, std::move(image)};
            }

            ImageMetadataResult ProbeFile(const std::string &path) const override
            {
                int width = 0;
                int height = 0;
                int channels = 0;
                const bool hdr = stbi_is_hdr(path.c_str()) != 0;
                if (stbi_info(path.c_str(), &width, &height, &channels) == 0)
                {
                    const char *reason = stbi_failure_reason();
                    return {Failure(reason ? reason : "Image metadata probe failed"), {}};
                }
                return BuildMetadata(width, height, hdr);
            }

            ImageMetadataResult ProbeMemory(const std::byte *data, size_t size) const override
            {
                if (data == nullptr || size == 0 ||
                    size > static_cast<size_t>(std::numeric_limits<int>::max()))
                {
                    return {Failure("encoded image memory is empty or too large"), {}};
                }
                const auto *bytes = reinterpret_cast<const stbi_uc *>(data);
                int width = 0;
                int height = 0;
                int channels = 0;
                const bool hdr = stbi_is_hdr_from_memory(bytes, static_cast<int>(size)) != 0;
                if (stbi_info_from_memory(bytes, static_cast<int>(size), &width, &height,
                                          &channels) == 0)
                {
                    const char *reason = stbi_failure_reason();
                    return {Failure(reason ? reason : "Image metadata probe failed"), {}};
                }
                return BuildMetadata(width, height, hdr);
            }

            ImageIoResult WritePngFile(const ImageBuffer &image,
                                       const std::string &path) const override
            {
                if (image.width > static_cast<uint32_t>(std::numeric_limits<int>::max() /
                                                        kRgba8PixelBytes) ||
                    image.height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
                {
                    return Failure("PNG extent exceeds the codec limit");
                }

                const int stride = static_cast<int>(image.width * kRgba8PixelBytes);
                if (stbi_write_png(path.c_str(), static_cast<int>(image.width),
                                   static_cast<int>(image.height),
                                   static_cast<int>(kRgba8PixelBytes), image.pixels.data(),
                                   stride) == 0)
                {
                    return Failure("PNG encoder failed to write the output file");
                }
                return {true, {}};
            }

            ImageEncodeResult EncodePngMemory(const ImageBuffer &image) const override
            {
                if (image.width > static_cast<uint32_t>(std::numeric_limits<int>::max() /
                                                        kRgba8PixelBytes) ||
                    image.height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
                {
                    return {Failure("PNG extent exceeds the codec limit"), {}};
                }
                struct Buffer
                {
                    std::vector<std::byte> bytes;
                } buffer;
                const auto callback = [](void *context, void *data, int size)
                {
                    auto &output = *static_cast<Buffer *>(context);
                    const auto *source = static_cast<const std::byte *>(data);
                    output.bytes.insert(output.bytes.end(), source, source + size);
                };
                const int stride = static_cast<int>(image.width * kRgba8PixelBytes);
                if (stbi_write_png_to_func(callback, &buffer, static_cast<int>(image.width),
                                           static_cast<int>(image.height),
                                           static_cast<int>(kRgba8PixelBytes), image.pixels.data(),
                                           stride) == 0)
                {
                    return {Failure("PNG encoder failed"), {}};
                }
                return {{true, {}}, std::move(buffer.bytes)};
            }
        };
    }

    const IImageCodec &GetDefaultImageCodec()
    {
        static const StbImageCodec codec;
        return codec;
    }
}
