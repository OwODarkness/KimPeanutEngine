#ifndef KPENGINE_RUNTIME_IMAGE_IO_CODEC_H
#define KPENGINE_RUNTIME_IMAGE_IO_CODEC_H

#include <string>

#include "image_io/image_io.h"

namespace kpengine::image_io
{
    // Private ImageIO seam. Codecs translate files to/from the stable CPU
    // ImageBuffer contract; callers never name a codec library or codec type.
    class IImageCodec
    {
    public:
        virtual ~IImageCodec() = default;

        virtual ImageDecodeResult DecodeFile(const std::string &path) const = 0;
        virtual ImageIoResult WritePngFile(const ImageBuffer &image,
                                           const std::string &path) const = 0;
    };

    // The factory is private to ImageIO. A future implementation may choose by
    // file format or platform capability without changing Asset/Runtime APIs.
    const IImageCodec &GetDefaultImageCodec();
}

#endif
