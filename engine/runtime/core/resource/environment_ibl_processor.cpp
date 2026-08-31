#include "environment_ibl_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace kpengine::resource
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kMinimumRoughness = 0.045f;

        struct Float3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        Float3 operator+(const Float3 &left, const Float3 &right)
        {
            return {left.x + right.x, left.y + right.y, left.z + right.z};
        }

        Float3 operator-(const Float3 &left, const Float3 &right)
        {
            return {left.x - right.x, left.y - right.y, left.z - right.z};
        }

        Float3 operator*(const Float3 &value, float scalar)
        {
            return {value.x * scalar, value.y * scalar, value.z * scalar};
        }

        Float3 operator/(const Float3 &value, float scalar)
        {
            return value * (1.0f / scalar);
        }

        float Dot(const Float3 &left, const Float3 &right)
        {
            return left.x * right.x + left.y * right.y + left.z * right.z;
        }

        Float3 Cross(const Float3 &left, const Float3 &right)
        {
            return {left.y * right.z - left.z * right.y,
                    left.z * right.x - left.x * right.z,
                    left.x * right.y - left.y * right.x};
        }

        Float3 Normalize(const Float3 &value)
        {
            const float length_squared = Dot(value, value);
            return length_squared > std::numeric_limits<float>::epsilon()
                       ? value / std::sqrt(length_squared)
                       : Float3{0.0f, 1.0f, 0.0f};
        }

        Float3 Reflect(const Float3 &incident, const Float3 &normal)
        {
            return incident - normal * (2.0f * Dot(incident, normal));
        }

        uint16_t FloatToHalf(float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
            const uint32_t exponent = (bits >> 23) & 0xffu;
            uint32_t mantissa = bits & 0x7fffffu;
            if (exponent == 0xffu)
            {
                return static_cast<uint16_t>(sign | 0x7c00u |
                                             (mantissa != 0 ? 0x0200u : 0u));
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

        float HalfToFloat(uint16_t value)
        {
            const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16;
            int32_t exponent = static_cast<int32_t>((value >> 10) & 0x1fu);
            uint32_t mantissa = value & 0x03ffu;
            uint32_t bits = 0;
            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits = sign;
                }
                else
                {
                    exponent = 1;
                    while ((mantissa & 0x0400u) == 0)
                    {
                        mantissa <<= 1;
                        --exponent;
                    }
                    mantissa &= 0x03ffu;
                    bits = sign |
                           (static_cast<uint32_t>(exponent + 112) << 23) |
                           (mantissa << 13);
                }
            }
            else if (exponent == 31)
            {
                bits = sign | 0x7f800000u | (mantissa << 13);
            }
            else
            {
                bits = sign | (static_cast<uint32_t>(exponent + 112) << 23) |
                       (mantissa << 13);
            }
            float result = 0.0f;
            std::memcpy(&result, &bits, sizeof(result));
            return result;
        }

        Float3 ReadPixel(const data::TextureData &texture, uint32_t x, uint32_t y)
        {
            const size_t offset =
                (static_cast<size_t>(y) * texture.width + x) * 4 * sizeof(uint16_t);
            std::array<uint16_t, 3> half{};
            std::memcpy(half.data(), texture.pixels.data() + offset,
                        half.size() * sizeof(uint16_t));
            return {HalfToFloat(half[0]), HalfToFloat(half[1]), HalfToFloat(half[2])};
        }

        void WritePixel(data::TextureData &texture, uint32_t x, uint32_t y,
                        const Float3 &color, float alpha = 1.0f)
        {
            const size_t offset =
                (static_cast<size_t>(y) * texture.width + x) * 4 * sizeof(uint16_t);
            const std::array<uint16_t, 4> half{
                FloatToHalf(std::max(color.x, 0.0f)),
                FloatToHalf(std::max(color.y, 0.0f)),
                FloatToHalf(std::max(color.z, 0.0f)), FloatToHalf(alpha)};
            std::memcpy(texture.pixels.data() + offset, half.data(),
                        half.size() * sizeof(uint16_t));
        }

        data::TextureData MakeTexture(uint32_t width, uint32_t height)
        {
            data::TextureData result{};
            result.width = width;
            result.height = height;
            result.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
            result.pixels.resize(static_cast<size_t>(width) * height * 4 * sizeof(uint16_t));
            return result;
        }

        Float3 SampleEnvironment(const data::TextureData &source, const Float3 &direction)
        {
            const Float3 normalized = Normalize(direction);
            float u = std::atan2(normalized.z, normalized.x) / (2.0f * kPi) + 0.5f;
            u -= std::floor(u);
            const float v = std::clamp(std::asin(std::clamp(normalized.y, -1.0f, 1.0f)) /
                                             kPi + 0.5f,
                                         0.0f, 1.0f);
            const float source_x = u * static_cast<float>(source.width);
            const float source_y = v * static_cast<float>(source.height - 1);
            const uint32_t x0 = static_cast<uint32_t>(std::floor(source_x)) % source.width;
            const uint32_t x1 = (x0 + 1) % source.width;
            const uint32_t y0 = std::min(static_cast<uint32_t>(std::floor(source_y)),
                                         source.height - 1);
            const uint32_t y1 = std::min(y0 + 1, source.height - 1);
            const float tx = source_x - std::floor(source_x);
            const float ty = source_y - std::floor(source_y);
            const Float3 top = ReadPixel(source, x0, y0) * (1.0f - tx) +
                               ReadPixel(source, x1, y0) * tx;
            const Float3 bottom = ReadPixel(source, x0, y1) * (1.0f - tx) +
                                  ReadPixel(source, x1, y1) * tx;
            return top * (1.0f - ty) + bottom * ty;
        }

        Float3 DirectionFromTexel(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            const float longitude = (u - 0.5f) * 2.0f * kPi;
            const float latitude = (v - 0.5f) * kPi;
            const float latitude_cosine = std::cos(latitude);
            return {latitude_cosine * std::cos(longitude), std::sin(latitude),
                    latitude_cosine * std::sin(longitude)};
        }

        std::array<Float3, 3> MakeBasis(const Float3 &normal)
        {
            const Float3 reference = std::abs(normal.y) < 0.999f
                                         ? Float3{0.0f, 1.0f, 0.0f}
                                         : Float3{1.0f, 0.0f, 0.0f};
            const Float3 tangent = Normalize(Cross(reference, normal));
            return {tangent, Cross(normal, tangent), normal};
        }

        Float3 ToWorld(const Float3 &sample, const std::array<Float3, 3> &basis)
        {
            return Normalize(basis[0] * sample.x + basis[1] * sample.y + basis[2] * sample.z);
        }

        float RadicalInverse(uint32_t bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
            bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
            bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
            return static_cast<float>(bits) * 2.3283064365386963e-10f;
        }

        std::array<float, 2> Hammersley(uint32_t index, uint32_t count)
        {
            return {static_cast<float>(index) / static_cast<float>(count),
                    RadicalInverse(index)};
        }

        Float3 CosineHemisphere(const std::array<float, 2> &sample)
        {
            const float radius = std::sqrt(sample[0]);
            const float phi = 2.0f * kPi * sample[1];
            return {radius * std::cos(phi), radius * std::sin(phi),
                    std::sqrt(std::max(0.0f, 1.0f - sample[0]))};
        }

        Float3 ImportanceSampleGgx(const std::array<float, 2> &sample,
                                   const Float3 &normal, float roughness)
        {
            const float alpha = roughness * roughness;
            const float alpha_squared = alpha * alpha;
            const float phi = 2.0f * kPi * sample[0];
            const float cosine = std::sqrt((1.0f - sample[1]) /
                                           (1.0f + (alpha_squared - 1.0f) * sample[1]));
            const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
            return ToWorld({std::cos(phi) * sine, std::sin(phi) * sine, cosine},
                           MakeBasis(normal));
        }

        float GeometrySchlickGgx(float n_dot_direction, float roughness)
        {
            const float alpha = roughness * roughness;
            const float k = alpha * 0.5f;
            return n_dot_direction /
                   std::max(n_dot_direction * (1.0f - k) + k, 1.0e-6f);
        }

        bool IsValid(const data::TextureData &source, const EnvironmentIblSettings &settings)
        {
            const size_t expected_bytes = static_cast<size_t>(source.width) * source.height *
                                          4 * sizeof(uint16_t);
            return source.format == TextureFormat::TEXTURE_FORMAT_RGBA16F &&
                   source.width > 0 && source.height > 0 &&
                   source.pixels.size() >= expected_bytes &&
                   settings.irradiance_width > 0 && settings.irradiance_height > 0 &&
                   settings.prefilter_width > 0 && settings.prefilter_height > 0 &&
                   settings.prefilter_level_count >= 2 && settings.brdf_lut_size > 0 &&
                   settings.sample_count > 0;
        }
    }

    std::optional<EnvironmentIblData> BuildEnvironmentIbl(
        const data::TextureData &source, const EnvironmentIblSettings &settings)
    {
        if (!IsValid(source, settings))
        {
            return std::nullopt;
        }

        EnvironmentIblData result{};
        result.prefilter_level_count = settings.prefilter_level_count;
        result.irradiance = MakeTexture(settings.irradiance_width,
                                        settings.irradiance_height);
        result.prefiltered_radiance = MakeTexture(
            settings.prefilter_width,
            settings.prefilter_height * settings.prefilter_level_count);
        result.brdf_lut = MakeTexture(settings.brdf_lut_size, settings.brdf_lut_size);

        for (uint32_t y = 0; y < settings.irradiance_height; ++y)
        {
            for (uint32_t x = 0; x < settings.irradiance_width; ++x)
            {
                const Float3 normal = DirectionFromTexel(
                    x, y, settings.irradiance_width, settings.irradiance_height);
                const auto basis = MakeBasis(normal);
                Float3 sum{};
                for (uint32_t sample_index = 0; sample_index < settings.sample_count;
                     ++sample_index)
                {
                    const Float3 direction = ToWorld(
                        CosineHemisphere(Hammersley(sample_index, settings.sample_count)),
                        basis);
                    sum = sum + SampleEnvironment(source, direction);
                }
                WritePixel(result.irradiance, x, y,
                           sum * (kPi / static_cast<float>(settings.sample_count)));
            }
        }

        for (uint32_t level = 0; level < settings.prefilter_level_count; ++level)
        {
            const float roughness = std::max(
                static_cast<float>(level) /
                    static_cast<float>(settings.prefilter_level_count - 1),
                kMinimumRoughness);
            for (uint32_t y = 0; y < settings.prefilter_height; ++y)
            {
                for (uint32_t x = 0; x < settings.prefilter_width; ++x)
                {
                    const Float3 normal = DirectionFromTexel(
                        x, y, settings.prefilter_width, settings.prefilter_height);
                    const Float3 view = normal;
                    Float3 sum{};
                    float weight = 0.0f;
                    for (uint32_t sample_index = 0; sample_index < settings.sample_count;
                         ++sample_index)
                    {
                        const Float3 halfway = ImportanceSampleGgx(
                            Hammersley(sample_index, settings.sample_count), normal, roughness);
                        const Float3 light = Normalize(Reflect(view * -1.0f, halfway));
                        const float n_dot_l = std::max(Dot(normal, light), 0.0f);
                        if (n_dot_l > 0.0f)
                        {
                            sum = sum + SampleEnvironment(source, light) * n_dot_l;
                            weight += n_dot_l;
                        }
                    }
                    WritePixel(result.prefiltered_radiance, x,
                               level * settings.prefilter_height + y,
                               weight > 0.0f ? sum / weight : Float3{});
                }
            }
        }

        for (uint32_t y = 0; y < settings.brdf_lut_size; ++y)
        {
            const float roughness = std::max(
                (static_cast<float>(y) + 0.5f) /
                    static_cast<float>(settings.brdf_lut_size),
                kMinimumRoughness);
            for (uint32_t x = 0; x < settings.brdf_lut_size; ++x)
            {
                const float n_dot_v = std::clamp(
                    (static_cast<float>(x) + 0.5f) /
                        static_cast<float>(settings.brdf_lut_size),
                    1.0e-4f, 1.0f);
                const Float3 normal{0.0f, 0.0f, 1.0f};
                const Float3 view{std::sqrt(std::max(0.0f, 1.0f - n_dot_v * n_dot_v)),
                                  0.0f, n_dot_v};
                float scale = 0.0f;
                float bias = 0.0f;
                for (uint32_t sample_index = 0; sample_index < settings.sample_count;
                     ++sample_index)
                {
                    const Float3 halfway = ImportanceSampleGgx(
                        Hammersley(sample_index, settings.sample_count), normal, roughness);
                    const Float3 light = Normalize(Reflect(view * -1.0f, halfway));
                    const float n_dot_l = std::max(light.z, 0.0f);
                    const float n_dot_h = std::max(halfway.z, 0.0f);
                    const float v_dot_h = std::max(Dot(view, halfway), 0.0f);
                    if (n_dot_l <= 0.0f)
                    {
                        continue;
                    }
                    const float geometry = GeometrySchlickGgx(n_dot_v, roughness) *
                                           GeometrySchlickGgx(n_dot_l, roughness);
                    const float visibility = geometry * v_dot_h /
                                             std::max(n_dot_h * n_dot_v, 1.0e-6f);
                    const float fresnel = std::pow(1.0f - v_dot_h, 5.0f);
                    scale += (1.0f - fresnel) * visibility;
                    bias += fresnel * visibility;
                }
                const float inverse_samples = 1.0f / static_cast<float>(settings.sample_count);
                WritePixel(result.brdf_lut, x, y,
                           {scale * inverse_samples, bias * inverse_samples, 0.0f});
            }
        }
        return result;
    }
}
