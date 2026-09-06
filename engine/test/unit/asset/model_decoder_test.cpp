#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "asset/assimp_model_decoder.h"

#ifndef KPENGINE_MI1_1_FIXTURE_DIR
#error "Asset MI1.1 fixture directory must be provided by CMake"
#endif

namespace
{
    using kpengine::asset::AssimpModelDecoder;
    using kpengine::asset::ImportedDependencyKind;
    using kpengine::asset::ImportedImageStorage;
    using kpengine::asset::ImportedModelDecodeError;
    using kpengine::asset::ImportedModelErrorCode;

    std::filesystem::path Fixture(const char *relative_path)
    {
        return std::filesystem::path(KPENGINE_MI1_1_FIXTURE_DIR) / relative_path;
    }

    std::vector<std::uint8_t> DecodeBase64(const std::string &encoded)
    {
        std::vector<std::uint8_t> decoded;
        int value = 0;
        int bits = -8;
        for (const unsigned char character : encoded)
        {
            if (std::isspace(character))
            {
                continue;
            }
            if (character == '=')
            {
                break;
            }
            int digit = -1;
            if (character >= 'A' && character <= 'Z')
            {
                digit = character - 'A';
            }
            else if (character >= 'a' && character <= 'z')
            {
                digit = character - 'a' + 26;
            }
            else if (character >= '0' && character <= '9')
            {
                digit = character - '0' + 52;
            }
            else if (character == '+')
            {
                digit = 62;
            }
            else if (character == '/')
            {
                digit = 63;
            }
            if (digit < 0)
            {
                continue;
            }
            value = (value << 6) | digit;
            bits += 6;
            if (bits >= 0)
            {
                decoded.push_back(static_cast<std::uint8_t>((value >> bits) & 0xff));
                bits -= 8;
            }
        }
        return decoded;
    }

    std::filesystem::path MaterializeGlbFixture()
    {
        const std::filesystem::path encoded_path = Fixture("glb/triangle.glb.b64");
        std::ifstream encoded_file(encoded_path);
        EXPECT_TRUE(encoded_file.is_open());
        const std::string encoded((std::istreambuf_iterator<char>(encoded_file)),
                                  std::istreambuf_iterator<char>());
        const std::vector<std::uint8_t> bytes = DecodeBase64(encoded);
        EXPECT_FALSE(bytes.empty());

        const std::filesystem::path output_path =
            std::filesystem::temp_directory_path() / "kpengine_mi1_3_triangle.glb";
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        EXPECT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        return output_path;
    }

    bool HasDependency(const kpengine::asset::ImportedModelDocument &document,
                       ImportedDependencyKind kind, const std::string &path)
    {
        return std::any_of(document.source_dependencies.begin(),
                           document.source_dependencies.end(),
                           [&](const auto &dependency)
                           { return dependency.kind == kind && dependency.path == path; });
    }
}

TEST(ModelDecoderTest, DecodesAllSupportedForeignFormatsToCpuValues)
{
    const std::filesystem::path glb = MaterializeGlbFixture();
    AssimpModelDecoder decoder;

    const auto obj = decoder.Decode(Fixture("obj/triangle.obj"));
    const auto fbx = decoder.Decode(Fixture("fbx/triangle.fbx"));
    const auto gltf = decoder.Decode(Fixture("gltf/transform.gltf"));
    const auto glb_document = decoder.Decode(glb);
    const auto stl = decoder.Decode(Fixture("failure/unsupported.stl"));

    for (const auto *document : {&obj, &fbx, &gltf, &glb_document, &stl})
    {
        EXPECT_EQ(document->mesh.vertices.size(), 3u);
        EXPECT_EQ(document->mesh.indices.size(), 3u);
        EXPECT_EQ(document->mesh.sections.size(), 1u);
        EXPECT_FALSE(document->materials.empty());
        ASSERT_FALSE(document->source_dependencies.empty());
        EXPECT_EQ(document->source_dependencies.front().kind,
                  ImportedDependencyKind::PrimarySource);
    }
    EXPECT_EQ(stl.mesh.sections.front().material_index, 0u);
    EXPECT_EQ(stl.materials.size(), 1u);
    EXPECT_FLOAT_EQ(gltf.mesh.local_bounds.min_.x_, 10.0f);

    std::error_code error;
    std::filesystem::remove(glb, error);
}

TEST(ModelDecoderTest, DiscoversObjSidecarsAndExternalImages)
{
    AssimpModelDecoder decoder;
    const auto document = decoder.Decode(Fixture("obj/triangle.obj"));

    EXPECT_TRUE(HasDependency(document, ImportedDependencyKind::PrimarySource, "triangle.obj"));
    EXPECT_TRUE(HasDependency(document, ImportedDependencyKind::ExternalFile, "triangle.mtl"));
    EXPECT_TRUE(HasDependency(document, ImportedDependencyKind::ExternalFile,
                              "textures/albedo.ppm"));
    ASSERT_EQ(document.images.size(), 1u);
    EXPECT_EQ(document.images.front().storage, ImportedImageStorage::ExternalFile);
    EXPECT_EQ(document.images.front().path, "textures/albedo.ppm");
    ASSERT_LT(document.mesh.sections.front().material_index, document.materials.size());
    EXPECT_EQ(document.materials[document.mesh.sections.front().material_index].base_color_texture,
              "textures/albedo.ppm");
}

TEST(ModelDecoderTest, RetainsEmbeddedGltfDependencyBytes)
{
    AssimpModelDecoder decoder;
    const auto document = decoder.Decode(Fixture("gltf/transform.gltf"));

    const auto dependency = std::find_if(
        document.source_dependencies.begin(), document.source_dependencies.end(),
        [](const auto &value)
        { return value.kind == ImportedDependencyKind::EmbeddedData; });
    ASSERT_NE(dependency, document.source_dependencies.end());
    EXPECT_EQ(dependency->path, "buffers[0]");
    EXPECT_FALSE(dependency->embedded_bytes.empty());
}

TEST(ModelDecoderTest, ReportsStableSourceScopedFailures)
{
    AssimpModelDecoder decoder;

    try
    {
        (void)decoder.Decode(Fixture("failure/missing_buffer.gltf"));
        FAIL() << "expected missing dependency failure";
    }
    catch (const ImportedModelDecodeError &error)
    {
        EXPECT_EQ(error.Code(), ImportedModelErrorCode::DependencyMissing);
        ASSERT_FALSE(error.Diagnostics().empty());
        EXPECT_EQ(error.Diagnostics().front().code, "MissingDependency");
    }

    try
    {
        (void)decoder.Decode(Fixture("failure/malformed.gltf"));
        FAIL() << "expected malformed source failure";
    }
    catch (const ImportedModelDecodeError &error)
    {
        EXPECT_EQ(error.Code(), ImportedModelErrorCode::MalformedSource);
        ASSERT_FALSE(error.Diagnostics().empty());
        EXPECT_EQ(error.Diagnostics().front().source_path,
                  std::filesystem::absolute(Fixture("failure/malformed.gltf"))
                      .lexically_normal()
                      .generic_string());
    }
}

TEST(ModelDecoderTest, RepeatedDecodeProducesEquivalentImportValues)
{
    AssimpModelDecoder decoder;
    const auto first = decoder.Decode(Fixture("obj/triangle.obj"));
    const auto second = decoder.Decode(Fixture("obj/triangle.obj"));

    ASSERT_EQ(first.mesh.vertices.size(), second.mesh.vertices.size());
    ASSERT_EQ(first.mesh.indices, second.mesh.indices);
    ASSERT_EQ(first.mesh.sections.size(), second.mesh.sections.size());
    EXPECT_EQ(first.materials.size(), second.materials.size());
    ASSERT_EQ(first.source_dependencies.size(), second.source_dependencies.size());
    for (std::size_t index = 0; index < first.source_dependencies.size(); ++index)
    {
        EXPECT_EQ(first.source_dependencies[index].kind,
                  second.source_dependencies[index].kind);
        EXPECT_EQ(first.source_dependencies[index].path,
                  second.source_dependencies[index].path);
    }
}
