#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/audio.h"
#include "asset/native_audio.h"

namespace
{
    struct TemporaryProduct
    {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("kp_native_audio_asset_" + std::to_string(
                std::filesystem::file_time_type::clock::now().time_since_epoch().count()));

        TemporaryProduct()
        {
            std::filesystem::create_directories(directory);
            kpengine::asset::NativeAudioData audio{};
            audio.codec = kpengine::asset::NativeAudioCodec::Wav;
            audio.channels = 2;
            audio.sample_rate = 48000;
            audio.duration_frames = 48000;
            audio.encoded_audio = {std::byte{0x01}, std::byte{0x02}};
            audio.waveform.assign(kpengine::asset::kNativeAudioWaveformBucketCount,
                                  {-0.1f, 0.1f});
            const auto bytes = kpengine::asset::SerializeNativeAudio(audio);
            const auto path = directory / "music.audio";
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }

        ~TemporaryProduct()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    };
}

TEST(NativeAudioRuntimeTest, AssetManagerLoadsNativeAudioMetadataWithoutDecodingThePayload)
{
    TemporaryProduct product;
    const std::filesystem::path path = product.directory / "music.audio";
    const kpengine::asset::AssetID id =
        kpengine::asset::AssetManager::GetInstance().LoadSync(path.string());
    ASSERT_TRUE(id.IsValid());
    EXPECT_EQ(id.type, kpengine::asset::AssetType::KPAT_Audio);

    const auto resource = kpengine::asset::AssetManager::GetInstance()
        .GetResource<kpengine::asset::AudioResource>(id);
    ASSERT_TRUE(resource);
    EXPECT_FALSE(resource->data);
    ASSERT_TRUE(resource->native_product);
    EXPECT_EQ(resource->native_product->encoded_audio_size, 2u);
    EXPECT_TRUE(resource->native_product->metadata.encoded_audio.empty());
    EXPECT_EQ(resource->native_product->metadata.duration_frames, 48000u);
}
