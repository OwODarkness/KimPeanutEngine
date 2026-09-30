#include "terrain_material_settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "asset/asset.h"
#include "asset/asset_manager.h"
#include "asset/model_archive.h"
#include "asset/native_material.h"
#include "asset/native_texture.h"
#include "asset/texture.h"
#include "config/path.h"
#include "image_io/image_io.h"

namespace kpengine::terrain
{
    namespace
    {
        using json = nlohmann::json;

        std::filesystem::path ResolveTexture(const std::filesystem::path &asset_root,
                                             const std::string &reference)
        {
            const std::filesystem::path relative{reference};
            if (relative.is_absolute())
                throw std::invalid_argument("terrain material texture paths must be Asset-relative");
            const std::filesystem::path resolved = (asset_root / relative).lexically_normal();
            const std::filesystem::path normalized_root = asset_root.lexically_normal();
            const std::filesystem::path relative_check = resolved.lexically_relative(normalized_root);
            if (relative_check.empty() || *relative_check.begin() == "..")
                throw std::invalid_argument("terrain material texture path escapes the Asset directory");
            return resolved;
        }

        std::vector<float> ReadColor(const json &value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::invalid_argument("terrain material base_color must have four values");
            std::vector<float> result;
            result.reserve(4);
            for (const json &component : value)
            {
                if (!component.is_number())
                    throw std::invalid_argument("terrain material base_color values must be numbers");
                const float channel = component.get<float>();
                if (!std::isfinite(channel) || channel < 0.0f || channel > 1.0f)
                    throw std::invalid_argument("terrain material base_color values must be in [0, 1]");
                result.push_back(channel);
            }
            return result;
        }
    }

    bool LoadTerrainMaterialSettings(const std::filesystem::path &settings_path,
                                     const std::filesystem::path &asset_root,
                                     TerrainMaterialSettings &settings,
                                     std::string &diagnostic)
    {
        diagnostic.clear();
        try
        {
            std::ifstream input(settings_path, std::ios::binary);
            if (!input) throw std::runtime_error("could not open terrain material settings: " + settings_path.string());
            const json source = json::parse(input);
            if (!source.is_object() || source.value("version", 0) != 1 ||
                !source.contains("layers") || !source["layers"].is_array() ||
                source["layers"].empty() || source["layers"].size() > 64 ||
                !source.contains("texture_tile_size_m") || !source["texture_tile_size_m"].is_number())
                throw std::invalid_argument("terrain material settings schema is invalid");

            TerrainMaterialSettings loaded;
            loaded.settings_path = settings_path;
            loaded.texture_tile_size_m = source["texture_tile_size_m"].get<float>();
            if (!std::isfinite(loaded.texture_tile_size_m) || loaded.texture_tile_size_m <= 0.0f)
                throw std::invalid_argument("texture_tile_size_m must be finite and positive");
            float previous_maximum = 0.0f;
            std::size_t height_layer_count = 0;
            bool saw_slope_layer = false;
            std::set<std::string, std::less<>> layer_ids;
            for (const json &entry : source["layers"])
            {
                if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() ||
                    !entry.contains("id") || !entry["id"].is_string())
                    throw std::invalid_argument("terrain material layer is missing name or id");
                const std::string id = entry["id"].get<std::string>();
                if (id.empty() || !std::all_of(id.begin(), id.end(), [](unsigned char value)
                    { return std::isalnum(value) || value == '_' || value == '-'; }) ||
                    !layer_ids.insert(id).second)
                    throw std::invalid_argument("terrain material layer IDs must be unique and use letters, digits, '_' or '-'");
                TerrainMaterialLayerMask mask{};
                const std::string mask_type = entry.value("mask", std::string{"height"});
                if (mask_type == "height")
                {
                    if (saw_slope_layer || !entry.contains("height_max") ||
                        !entry.contains("blend_width") || !entry["height_max"].is_number() ||
                        !entry["blend_width"].is_number())
                        throw std::invalid_argument("height layers require height_max/blend_width and must precede slope overlays");
                    mask.height_max = entry["height_max"].get<float>();
                    mask.blend_width = entry["blend_width"].get<float>();
                    if (!std::isfinite(mask.height_max) || mask.height_max <= previous_maximum ||
                        mask.height_max > 1.0f)
                        throw std::invalid_argument("terrain material height_max values must increase in (0, 1]");
                    if (!std::isfinite(mask.blend_width) || mask.blend_width < 0.0f ||
                        mask.blend_width > 2.0f * (mask.height_max - previous_maximum))
                        throw std::invalid_argument("terrain layer blend_width must fit inside its elevation band");
                    previous_maximum = mask.height_max;
                    ++height_layer_count;
                }
                else if (mask_type == "slope")
                {
                    saw_slope_layer = true;
                    if (height_layer_count == 0 || !entry.contains("slope_min_radians") ||
                        !entry.contains("slope_blend_width_radians") ||
                        !entry["slope_min_radians"].is_number() ||
                        !entry["slope_blend_width_radians"].is_number())
                        throw std::invalid_argument("slope layers require a base height stack and slope_min_radians/slope_blend_width_radians");
                    mask.kind = TerrainMaterialMaskKind::SlopeOverlay;
                    mask.slope_min_radians = entry["slope_min_radians"].get<float>();
                    mask.slope_blend_width_radians = entry["slope_blend_width_radians"].get<float>();
                    mask.height_min = entry.value("height_min", 0.0f);
                    mask.height_max_for_overlay = entry.value("height_max", 1.0f);
                    mask.height_fade_width = entry.value("height_fade_width", 0.0f);
                    constexpr float half_pi = 1.57079632679f;
                    if (!std::isfinite(mask.slope_min_radians) || mask.slope_min_radians < 0.0f ||
                        mask.slope_min_radians > half_pi ||
                        !std::isfinite(mask.slope_blend_width_radians) ||
                        mask.slope_blend_width_radians < 0.0f ||
                        mask.slope_blend_width_radians > half_pi ||
                        !std::isfinite(mask.height_min) || !std::isfinite(mask.height_max_for_overlay) ||
                        mask.height_min < 0.0f || mask.height_max_for_overlay > 1.0f ||
                        mask.height_min >= mask.height_max_for_overlay ||
                        !std::isfinite(mask.height_fade_width) || mask.height_fade_width < 0.0f ||
                        mask.height_fade_width > (mask.height_max_for_overlay - mask.height_min) * 0.5f)
                        throw std::invalid_argument("slope layer ranges must be finite and within the normalized height/slope domains");
                }
                else
                {
                    throw std::invalid_argument("terrain material mask must be height or slope");
                }

                asset::ImportedMaterialSource material;
                material.name = entry["name"].get<std::string>();
                const auto color = ReadColor(entry.at("base_color"));
                material.base_color = {color[0], color[1], color[2], color[3]};
                material.metallic = entry.at("metallic").get<float>();
                material.roughness = entry.at("roughness").get<float>();
                if (!std::isfinite(material.metallic) || material.metallic < 0.0f || material.metallic > 1.0f ||
                    !std::isfinite(material.roughness) || material.roughness < 0.0f || material.roughness > 1.0f)
                    throw std::invalid_argument("terrain material metallic and roughness must be in [0, 1]");

                const auto add_image = [&](const char *key, std::string &material_reference)
                {
                    if (!entry.contains(key)) return;
                    if (!entry[key].is_string())
                        throw std::invalid_argument(std::string{"terrain texture path must be a string: "} + key);
                    const std::string reference = entry.at(key).get<std::string>();
                    const std::filesystem::path resolved = ResolveTexture(asset_root, reference);
                    if (!std::filesystem::is_regular_file(resolved))
                        throw std::runtime_error("terrain material texture is missing: " + resolved.string());
                    material_reference = reference;
                    const auto found = std::find_if(loaded.document.images.begin(), loaded.document.images.end(),
                        [&](const asset::ImportedImageSource &image) { return image.path == reference; });
                    if (found == loaded.document.images.end())
                    {
                        asset::ImportedImageSource image;
                        image.path = reference;
                        image.resolved_path = resolved;
                        image.source_hash = asset::Sha256File(resolved);
                        loaded.document.images.push_back(std::move(image));
                        loaded.source_files.push_back(resolved);
                    }
                };
                add_image("albedo", material.base_color_texture);
                add_image("normal", material.normal_texture);
                add_image("occlusion", material.occlusion_texture);
                if (entry.contains("metallic_map"))
                    add_image("metallic_map", material.metallic_texture);
                if (entry.contains("roughness_map"))
                    add_image("roughness_map", material.roughness_texture);
                loaded.document.materials.push_back(std::move(material));
                loaded.layer_masks.push_back(mask);
                loaded.layer_ids.push_back(id);
                loaded.names.push_back(entry["name"].get<std::string>());
            }
            if (height_layer_count == 0)
                throw std::invalid_argument("terrain material profile needs at least one height layer");
            if (std::abs(previous_maximum - 1.0f) > 1.0e-6f)
                throw std::invalid_argument("last height terrain material height_max must be 1.0");
            loaded.layer_masks[height_layer_count - 1].blend_width = 0.0f;
            settings = std::move(loaded);
            return true;
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            return false;
        }
    }

    void BuildTerrainLayerMaterial(const TerrainMaterialSettings &settings,
                                   const ScalarField2D &heightfield,
                                   asset::ImportedModelDocument &document)
    {
        using namespace asset;
        constexpr std::uint32_t output_size = 1024;
        struct LayerMaps
        {
            image_io::ImageBuffer albedo;
            image_io::ImageBuffer normal;
            image_io::ImageBuffer occlusion;
            image_io::ImageBuffer metallic;
            image_io::ImageBuffer roughness;
            std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
            float metallic_value{};
            float roughness_value{1.0f};
        };
        const auto decode = [&](const std::string &reference) -> image_io::ImageBuffer
        {
            if (reference.empty()) return {};
            const auto image = std::find_if(settings.document.images.begin(), settings.document.images.end(),
                [&](const ImportedImageSource &candidate) { return candidate.path == reference; });
            if (image == settings.document.images.end())
                throw std::invalid_argument("Terrain layer references an image missing from its settings");
            const auto decoded = image_io::DecodeImageFile(image->resolved_path.generic_string());
            if (!decoded.result.success || decoded.image.format != image_io::ImagePixelFormat::Rgba8)
                throw std::runtime_error("could not decode Terrain layer texture: " + reference);
            return decoded.image;
        };
        std::vector<LayerMaps> maps;
        maps.reserve(settings.document.materials.size());
        for (const ImportedMaterialSource &source : settings.document.materials)
        {
            LayerMaps layer;
            layer.albedo = decode(source.base_color_texture);
            layer.normal = decode(source.normal_texture);
            layer.occlusion = decode(source.occlusion_texture);
            if (!source.metallic_texture.empty()) layer.metallic = decode(source.metallic_texture);
            if (!source.roughness_texture.empty()) layer.roughness = decode(source.roughness_texture);
            layer.base_color = {source.base_color[0], source.base_color[1],
                                source.base_color[2], source.base_color[3]};
            layer.metallic_value = source.metallic;
            layer.roughness_value = source.roughness;
            maps.push_back(std::move(layer));
        }
        const auto sample = [](const image_io::ImageBuffer &image, float u, float v, int channel,
                               float fallback)
        {
            if (!image.IsValid()) return fallback;
            u -= std::floor(u);
            v -= std::floor(v);
            const float x = u * static_cast<float>(image.width - 1);
            const float y = v * static_cast<float>(image.height - 1);
            const std::uint32_t x0 = static_cast<std::uint32_t>(x);
            const std::uint32_t y0 = static_cast<std::uint32_t>(y);
            const std::uint32_t x1 = std::min(x0 + 1, image.width - 1);
            const std::uint32_t y1 = std::min(y0 + 1, image.height - 1);
            const float tx = x - static_cast<float>(x0);
            const float ty = y - static_cast<float>(y0);
            const auto texel = [&](std::uint32_t px, std::uint32_t py)
            {
                const std::size_t offset = (static_cast<std::size_t>(py) * image.width + px) * 4 + channel;
                return static_cast<float>(image.pixels[offset]) / 255.0f;
            };
            const float top = texel(x0, y0) * (1.0f - tx) + texel(x1, y0) * tx;
            const float bottom = texel(x0, y1) * (1.0f - tx) + texel(x1, y1) * tx;
            return top * (1.0f - ty) + bottom * ty;
        };
        const auto &domain = heightfield.Domain();
        const auto [minimum_it, maximum_it] = std::minmax_element(
            heightfield.Samples().begin(), heightfield.Samples().end());
        const float height_extent = std::max(*maximum_it - *minimum_it, 1.0e-6f);
        const float world_width = static_cast<float>(domain.width - 1) *
                                  static_cast<float>(domain.spacing_x_m);
        const float world_depth = static_cast<float>(domain.height - 1) *
                                  static_cast<float>(domain.spacing_z_m);
        const auto height_at = [&](float u, float v)
        {
            const float x = u * (domain.width - 1);
            const float y = v * (domain.height - 1);
            const std::uint32_t x0 = static_cast<std::uint32_t>(x);
            const std::uint32_t y0 = static_cast<std::uint32_t>(y);
            const std::uint32_t x1 = std::min(x0 + 1, domain.width - 1);
            const std::uint32_t y1 = std::min(y0 + 1, domain.height - 1);
            const float tx = x - x0;
            const float ty = y - y0;
            const float top = static_cast<float>(heightfield.At(x0, y0)) * (1.0f - tx) +
                              static_cast<float>(heightfield.At(x1, y0)) * tx;
            const float bottom = static_cast<float>(heightfield.At(x0, y1)) * (1.0f - tx) +
                                 static_cast<float>(heightfield.At(x1, y1)) * tx;
            return top * (1.0f - ty) + bottom * ty;
        };
        const auto smoothstep = [](float lower, float upper, float value)
        {
            if (upper <= lower) return value >= upper ? 1.0f : 0.0f;
            const float t = std::clamp((value - lower) / (upper - lower), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        const auto height_transition = [&](float lower, float upper, float value)
        {
            if (upper <= lower) return value >= upper ? 1.0f : 0.0f;
            const float t = std::clamp((value - lower) / (upper - lower), 0.0f, 1.0f);
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        };
        const std::vector<float> slope_samples = ComputeSlopeRadians(heightfield);
        const auto slope_at = [&](float u, float v)
        {
            const float x = u * (domain.width - 1);
            const float y = v * (domain.height - 1);
            const std::uint32_t x0 = static_cast<std::uint32_t>(x);
            const std::uint32_t y0 = static_cast<std::uint32_t>(y);
            const std::uint32_t x1 = std::min(x0 + 1, domain.width - 1);
            const std::uint32_t y1 = std::min(y0 + 1, domain.height - 1);
            const float tx = x - static_cast<float>(x0);
            const float ty = y - static_cast<float>(y0);
            const auto at = [&](std::uint32_t sx, std::uint32_t sy)
            { return slope_samples[static_cast<std::size_t>(sy) * domain.width + sx]; };
            const float top = at(x0, y0) * (1.0f - tx) + at(x1, y0) * tx;
            const float bottom = at(x0, y1) * (1.0f - tx) + at(x1, y1) * tx;
            return top * (1.0f - ty) + bottom * ty;
        };
        const std::size_t height_layer_count = static_cast<std::size_t>(std::find_if(
            settings.layer_masks.begin(), settings.layer_masks.end(),
            [](const TerrainMaterialLayerMask &mask)
            { return mask.kind == TerrainMaterialMaskKind::SlopeOverlay; }) -
            settings.layer_masks.begin());
        const auto make_image = []
        {
            image_io::ImageBuffer image;
            image.width = output_size;
            image.height = output_size;
            image.format = image_io::ImagePixelFormat::Rgba8;
            image.pixels.resize(static_cast<std::size_t>(output_size) * output_size * 4);
            return image;
        };
        image_io::ImageBuffer albedo = make_image();
        image_io::ImageBuffer normal = make_image();
        image_io::ImageBuffer occlusion = make_image();
        image_io::ImageBuffer metallic = make_image();
        image_io::ImageBuffer roughness = make_image();
        const auto to_byte = [](float value)
        { return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
        const auto srgb_to_linear = [](float value)
        { return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f); };
        const auto linear_to_srgb = [](float value)
        { return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f; };
        std::vector<float> boundary_weights(height_layer_count > 0 ? height_layer_count - 1 : 0);
        std::vector<float> weights(settings.document.materials.size());
        for (std::uint32_t y = 0; y < output_size; ++y)
        {
            const float v = static_cast<float>(y) / (output_size - 1);
            for (std::uint32_t x = 0; x < output_size; ++x)
            {
                const float u = static_cast<float>(x) / (output_size - 1);
                const float elevation = std::clamp(
                    (height_at(u, v) - *minimum_it) / height_extent, 0.0f, 1.0f);
                for (std::size_t boundary = 0; boundary < boundary_weights.size(); ++boundary)
                {
                    const float threshold = settings.layer_masks[boundary].height_max;
                    const float width = settings.layer_masks[boundary].blend_width;
                    boundary_weights[boundary] = height_transition(
                        threshold - width * 0.5f, threshold + width * 0.5f, elevation);
                }
                weights[0] = 1.0f;
                if (height_layer_count > 1)
                    weights[0] = 1.0f - boundary_weights[0];
                for (std::size_t layer = 1; layer + 1 < height_layer_count; ++layer)
                    weights[layer] = boundary_weights[layer - 1] * (1.0f - boundary_weights[layer]);
                if (height_layer_count > 1)
                    weights[height_layer_count - 1] = boundary_weights.back();
                const float slope = slope_at(u, v);
                for (std::size_t layer = height_layer_count; layer < maps.size(); ++layer)
                {
                    const TerrainMaterialLayerMask &mask = settings.layer_masks[layer];
                    const float slope_weight = smoothstep(
                        mask.slope_min_radians - mask.slope_blend_width_radians * 0.5f,
                        mask.slope_min_radians + mask.slope_blend_width_radians * 0.5f,
                        slope);
                    float height_weight = 1.0f;
                    if (mask.height_fade_width > 0.0f)
                    {
                        height_weight = height_transition(mask.height_min,
                            mask.height_min + mask.height_fade_width, elevation) *
                            (1.0f - height_transition(mask.height_max_for_overlay - mask.height_fade_width,
                                mask.height_max_for_overlay, elevation));
                    }
                    else
                    {
                        height_weight = elevation >= mask.height_min &&
                            elevation <= mask.height_max_for_overlay ? 1.0f : 0.0f;
                    }
                    const float overlay_weight = slope_weight * height_weight;
                    for (std::size_t previous = 0; previous < layer; ++previous)
                        weights[previous] *= 1.0f - overlay_weight;
                    weights[layer] = overlay_weight;
                }
                const float world_x = static_cast<float>(domain.origin_x_m) + u * world_width;
                const float world_z = static_cast<float>(domain.origin_z_m) + v * world_depth;
                const float tu = world_x / settings.texture_tile_size_m;
                const float tv = world_z / settings.texture_tile_size_m;
                std::array<float, 4> color{};
                std::array<float, 3> tangent_normal{};
                float ao = 0.0f;
                float metal = 0.0f;
                float rough = 0.0f;
                for (std::size_t layer_index = 0; layer_index < maps.size(); ++layer_index)
                {
                    const LayerMaps &layer = maps[layer_index];
                    const float weight = weights[layer_index];
                    for (int channel = 0; channel < 3; ++channel)
                        color[channel] += srgb_to_linear(sample(layer.albedo, tu, tv, channel, 1.0f)) *
                            layer.base_color[channel] * weight;
                    color[3] += sample(layer.albedo, tu, tv, 3, 1.0f) *
                        layer.base_color[3] * weight;
                    tangent_normal[0] += (sample(layer.normal, tu, tv, 0, 0.5f) * 2.0f - 1.0f) * weight;
                    tangent_normal[1] += (sample(layer.normal, tu, tv, 1, 0.5f) * 2.0f - 1.0f) * weight;
                    tangent_normal[2] += (sample(layer.normal, tu, tv, 2, 1.0f) * 2.0f - 1.0f) * weight;
                    ao += sample(layer.occlusion, tu, tv, 0, 1.0f) * weight;
                    metal += sample(layer.metallic, tu, tv, 0, layer.metallic_value) * weight;
                    rough += sample(layer.roughness, tu, tv, 0, layer.roughness_value) * weight;
                }
                const float normal_length = std::max(std::sqrt(tangent_normal[0] * tangent_normal[0] +
                    tangent_normal[1] * tangent_normal[1] + tangent_normal[2] * tangent_normal[2]), 1.0e-6f);
                const std::array<float, 3> normalized_normal{tangent_normal[0] / normal_length,
                    tangent_normal[1] / normal_length, tangent_normal[2] / normal_length};
                const std::size_t offset = (static_cast<std::size_t>(y) * output_size + x) * 4;
                for (int channel = 0; channel < 3; ++channel)
                {
                    albedo.pixels[offset + channel] = to_byte(linear_to_srgb(color[channel]));
                    normal.pixels[offset + channel] = to_byte(normalized_normal[channel] * 0.5f + 0.5f);
                }
                albedo.pixels[offset + 3] = to_byte(color[3]);
                normal.pixels[offset + 3] = 255;
                occlusion.pixels[offset] = to_byte(ao);
                metallic.pixels[offset] = to_byte(metal);
                roughness.pixels[offset] = to_byte(rough);
                for (int channel = 1; channel < 4; ++channel)
                {
                    occlusion.pixels[offset + channel] = 255;
                    metallic.pixels[offset + channel] = 255;
                    roughness.pixels[offset + channel] = 255;
                }
            }
        }
        document = {};
        ImportedMaterialSource material;
        material.name = "Terrain Layer Blend";
        material.base_color = {1.0f, 1.0f, 1.0f, 1.0f};
        material.metallic = 1.0f;
        material.roughness = 1.0f;
        const auto add_embedded = [&](const char *reference,
                                      image_io::ImageBuffer image, std::string &material_reference)
        {
            const auto encoded = image_io::EncodePngMemory(image);
            if (!encoded.result.success)
                throw std::runtime_error("could not encode blended Terrain PBR map");
            ImportedImageSource source;
            source.path = std::string("terrain-layer://") + reference;
            source.storage = ImportedImageStorage::EmbeddedBytes;
            source.format_hint = "png";
            source.embedded_bytes = encoded.bytes;
            source.source_hash = Sha256(source.embedded_bytes);
            material_reference = source.path;
            document.images.push_back(std::move(source));
        };
        add_embedded("albedo.png", std::move(albedo), material.base_color_texture);
        add_embedded("normal.png", std::move(normal), material.normal_texture);
        add_embedded("occlusion.png", std::move(occlusion), material.occlusion_texture);
        add_embedded("metallic.png", std::move(metallic), material.metallic_texture);
        add_embedded("roughness.png", std::move(roughness), material.roughness_texture);
        document.materials.push_back(std::move(material));
    }

    void AssignTerrainMaterialSections(data::MeshData &mesh)
    {
        if (mesh.indices.empty() || mesh.vertices.empty() || mesh.indices.size() % 3 != 0)
            throw std::invalid_argument("Terrain material section needs indexed triangles");
        spatial::AABB bounds{{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                              std::numeric_limits<float>::max()},
                             {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                              -std::numeric_limits<float>::max()}};
        for (const data::Vertex &vertex : mesh.vertices) bounds.ExpandToInclude(vertex.position);
        mesh.sections = {{0u, static_cast<std::uint32_t>(mesh.indices.size()), 0u, bounds}};
    }

    bool RegisterTerrainPreviewMaterials(const TerrainMaterialSettings &settings,
                                        const ScalarField2D &heightfield,
                                        const std::filesystem::path &asset_root,
                                        const std::filesystem::path &archive_root,
                                        std::vector<asset::AssetID> &material_ids,
                                        std::vector<asset::AssetID> &texture_ids,
                                        std::string &diagnostic)
    {
        using namespace asset;
        diagnostic.clear();
        material_ids.clear();
        texture_ids.clear();
        try
        {
            static std::atomic<std::uint64_t> next_material_asset{0};
            const std::uint64_t asset_revision =
                next_material_asset.fetch_add(1, std::memory_order_relaxed);
            ImportedModelDocument composite_document;
            BuildTerrainLayerMaterial(settings, heightfield, composite_document);
            NativeMaterialConversionSettings conversion;
            conversion.asset_root = asset_root;
            conversion.archive_root = archive_root;
            NativeMaterialConversionResult converted =
                ConvertImportedMaterials(composite_document, conversion);
            AssetManager &assets = AssetManager::GetInstance();
            std::map<std::string, AssetID, std::less<>> textures_by_hash;
            for (const NativeImageProduct &product : converted.embedded_images)
            {
                NativeTextureProduct decoded = DeserializeNativeTexture(product.bytes);
                auto texture = std::make_shared<TextureResource>();
                texture->channel_count = 4;
                *texture->data = std::move(decoded.data);
                AssetRegisterInfo info{};
                info.resource = std::move(texture);
                info.path = "generated://terrain/texture-" + product.content_hash.ToHex() +
                    "-" + std::to_string(asset_revision);
                info.name = "Terrain PBR Texture";
                info.type = AssetType::KPAT_Texture;
                const AssetID id = assets.RegisterAsset(info);
                if (!id.IsValid()) throw std::runtime_error("AssetManager rejected a Terrain PBR texture");
                texture_ids.push_back(id);
                textures_by_hash.emplace(product.content_hash.ToHex(), id);
            }

            const std::string shader_path = (asset_root / "shader/pbr_gbuffer.shader").generic_string();
            const AssetID shader_id = assets.LoadSync(shader_path);
            if (!shader_id.IsValid()) throw std::runtime_error("could not load the standard PBR shader");
            for (std::size_t index = 0; index < converted.materials.size(); ++index)
            {
                auto material = std::make_shared<MaterialResource>(converted.materials[index].material);
                material->shader_dependency_index = 0;
                std::vector<AssetID> dependencies{shader_id};
                for (MaterialParameterSource &parameter : material->parameters)
                {
                    if (parameter.type != MaterialParameterSourceType::Texture) continue;
                    const std::filesystem::path texture_path{std::get<std::string>(parameter.value)};
                    const std::string hash = texture_path.stem().string();
                    const auto found = textures_by_hash.find(hash);
                    if (found == textures_by_hash.end())
                        throw std::runtime_error("Terrain material references an uncooked PBR texture");
                    parameter.dependency_index = static_cast<std::uint32_t>(dependencies.size());
                    dependencies.push_back(found->second);
                }
                AssetRegisterInfo info{};
                info.resource = std::move(material);
                info.path = "generated://terrain/material-blend-" +
                    std::to_string(asset_revision);
                info.name = "Terrain Layer Blend";
                info.dependencies = std::move(dependencies);
                info.type = AssetType::KPAT_Material;
                const AssetID id = assets.RegisterAsset(info);
                if (!id.IsValid()) throw std::runtime_error("AssetManager rejected a Terrain PBR material");
                material_ids.push_back(id);
            }
            return true;
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            for (const AssetID id : material_ids) AssetManager::GetInstance().UnRegisterAsset(id);
            for (const AssetID id : texture_ids) AssetManager::GetInstance().UnRegisterAsset(id);
            material_ids.clear();
            texture_ids.clear();
            return false;
        }
    }
}
