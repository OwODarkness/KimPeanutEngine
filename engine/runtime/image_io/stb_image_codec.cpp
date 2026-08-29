#include "image_io/image_codec.h"

#include <limits>
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
        };
    }

    const IImageCodec &GetDefaultImageCodec()
    {
        static const StbImageCodec codec;
        return codec;
    }
}
