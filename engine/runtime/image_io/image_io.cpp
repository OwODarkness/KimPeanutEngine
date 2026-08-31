#include "image_io/image_io.h"
#include "image_io/image_codec.h"

#include <limits>
#include <utility>

namespace kpengine::image_io
{
    namespace
    {
        constexpr size_t kRgba8PixelBytes = 4;
        constexpr size_t kRgba32FloatPixelBytes = sizeof(float) * 4;
    }

    size_t ImageBuffer::ExpectedByteCount() const
    {
        if (width == 0 || height == 0)
        {
            return 0;
        }
        if (static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height)
        {
            return 0;
        }
        const size_t pixel_count = static_cast<size_t>(width) * height;
        size_t bytes_per_pixel = 0;
        switch (format)
        {
        case ImagePixelFormat::Rgba8:
            bytes_per_pixel = kRgba8PixelBytes;
            break;
        case ImagePixelFormat::Rgba32Float:
            bytes_per_pixel = kRgba32FloatPixelBytes;
            break;
        }
        if (bytes_per_pixel == 0)
        {
            return 0;
        }
        if (pixel_count > std::numeric_limits<size_t>::max() / bytes_per_pixel)
        {
            return 0;
        }
        return pixel_count * bytes_per_pixel;
    }

    bool ImageBuffer::IsValid() const
    {
        const size_t expected_byte_count = ExpectedByteCount();
        return expected_byte_count != 0 && pixels.size() == expected_byte_count;
    }

    ImageDecodeResult DecodeImageFile(const std::string &path)
    {
        return GetDefaultImageCodec().DecodeFile(path);
    }

    ImageIoResult WritePng(const ImageBuffer &image, const std::string &path)
    {
        if (path.empty())
        {
            return {false, "PNG output path is empty"};
        }
        if (!image.IsValid() || image.format != ImagePixelFormat::Rgba8)
        {
            return {false, "PNG export requires a valid tightly packed RGBA8 image"};
        }
        return GetDefaultImageCodec().WritePngFile(image, path);
    }
}
