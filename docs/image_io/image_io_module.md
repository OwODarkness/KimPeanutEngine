# ImageIO Module

Location: `engine/runtime/image_io/`

ImageIO is the focused CPU image-codec boundary. It owns `ImageBuffer`,
supported-file decoding, and image encoding. The initial contract normalizes
input to tightly packed RGBA8 pixels and writes lossless PNG files.

ImageIO does not own asset identity, source-path caching, texture format policy,
render-target handles, frame scheduling, screenshot filenames, or GPU objects.
Asset uses ImageIO then creates its `TextureResource`/`data::TextureData` with
Asset's sRGB texture policy. Future RuntimeScreenshotService will use ImageIO
to write captured CPU pixels after Render/Graphics completes readback.

The public ImageIO API is codec-neutral. A private `IImageCodec` seam adapts
the current stb implementation, so ImageIO can add WIC, libpng, HDR, or EXR
codecs without changing Asset, Runtime, or its CPU image contract. There is no
public codec registry until a second codec has a consumer. `stb_image` remains
the current implementation; its declarations and direct calls are confined to
`stb_image_codec.cpp`, where `stb_image_write` has its one implementation TU.

Validation is `ImageIOUnitTest`: RGBA8 buffer validation, lossless PNG
encode/decode round trip, and invalid-output rejection. No asset manager or
graphics backend is required.
