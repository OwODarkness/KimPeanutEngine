#version 450

layout(binding = 0) uniform sampler2D gbuffer_depth;
layout(binding = 1) uniform sampler2D gbuffer_normal;
layout(std140, binding = 3) uniform ScreenSpaceAoConstants
{
    mat4 inverse_view_projection;
    vec4 radius_bias_strength;
} ao_constants;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out float out_visibility;

vec3 reconstruct_world_position(vec2 uv, float depth)
{
    vec2 ndc_xy = uv * 2.0 - 1.0;
#if KP_GRAPHICS_API_VULKAN
    ndc_xy.y = -ndc_xy.y;
#endif
    vec4 world = ao_constants.inverse_view_projection *
                 vec4(ndc_xy, depth * 2.0 - 1.0, 1.0);
    return world.xyz / max(abs(world.w), 1e-7) * sign(world.w);
}

void main()
{
    ivec2 extent = textureSize(gbuffer_depth, 0);
    ivec2 pixel = clamp(ivec2(frag_texcoord * vec2(extent)), ivec2(0), extent - 1);
    float depth = texelFetch(gbuffer_depth, pixel, 0).r;
    if (depth >= 0.999999)
    {
        out_visibility = 1.0;
        return;
    }
    if (ao_constants.radius_bias_strength.z <= 0.0)
    {
        out_visibility = 1.0;
        return;
    }

    vec3 normal = normalize(texelFetch(gbuffer_normal, pixel, 0).xyz);
    vec3 position = reconstruct_world_position(frag_texcoord, depth);
    float radius = max(ao_constants.radius_bias_strength.x, 1e-3);
    float bias = max(ao_constants.radius_bias_strength.y, 0.0);
    float occlusion = 0.0;
    float valid_weight = 0.0;
    const ivec2 offsets[24] = ivec2[24](
        ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1),
        ivec2(2, 1), ivec2(-2, -1), ivec2(1, -2), ivec2(-1, 2),
        ivec2(3, 2), ivec2(-3, -2), ivec2(2, -3), ivec2(-2, 3),
        ivec2(4, 1), ivec2(-4, -1), ivec2(1, -4), ivec2(-1, 4),
        ivec2(4, 3), ivec2(-4, -3), ivec2(3, -4), ivec2(-3, 4),
        ivec2(5, 0), ivec2(-5, 0), ivec2(0, 5), ivec2(0, -5));
    int sample_count = int(ao_constants.radius_bias_strength.w);
    for (int index = 0; index < sample_count; ++index)
    {
        ivec2 sample_pixel = pixel + offsets[index] * 3;
        if (any(lessThan(sample_pixel, ivec2(0))) || any(greaterThanEqual(sample_pixel, extent)))
            continue;
        float sample_depth = texelFetch(gbuffer_depth, sample_pixel, 0).r;
        if (sample_depth >= 0.999999)
            continue;
        vec2 sample_uv = (vec2(sample_pixel) + 0.5) / vec2(extent);
        vec3 delta = reconstruct_world_position(sample_uv, sample_depth) - position;
        float distance_to_sample = length(delta);
        if (distance_to_sample <= bias || distance_to_sample >= radius)
            continue;
        float normal_distance = dot(normal, delta);
        float hemisphere = normal_distance > bias
                               ? normal_distance / distance_to_sample
                               : 0.0;
        float range_weight = 1.0 - distance_to_sample / radius;
        valid_weight += 1.0;
        occlusion += hemisphere * range_weight;
    }
    float average_occlusion = valid_weight > 0.0 ? occlusion / valid_weight : 0.0;
    out_visibility = clamp(1.0 - average_occlusion * ao_constants.radius_bias_strength.z,
                           0.0, 1.0);
}
