#version 450

layout(binding = 0) uniform sampler2D raw_ao;
layout(binding = 1) uniform sampler2D gbuffer_depth;
layout(binding = 2) uniform sampler2D gbuffer_normal;
layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out float out_visibility;

void main()
{
    ivec2 extent = textureSize(gbuffer_depth, 0);
    ivec2 pixel = clamp(ivec2(frag_texcoord * vec2(extent)), ivec2(0), extent - 1);
    float center_depth = texelFetch(gbuffer_depth, pixel, 0).r;
    if (center_depth >= 0.999999)
    {
        out_visibility = 1.0;
        return;
    }
    vec3 center_normal = normalize(texelFetch(gbuffer_normal, pixel, 0).xyz);
    float total_weight = 0.0;
    float visibility = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            ivec2 sample_pixel = pixel + ivec2(x, y);
            if (any(lessThan(sample_pixel, ivec2(0))) ||
                any(greaterThanEqual(sample_pixel, extent)))
                continue;
            float sample_depth = texelFetch(gbuffer_depth, sample_pixel, 0).r;
            if (sample_depth >= 0.999999)
                continue;
            vec3 sample_normal = normalize(texelFetch(gbuffer_normal, sample_pixel, 0).xyz);
            float depth_weight = exp(-abs(sample_depth - center_depth) * 180.0);
            float normal_weight = pow(max(dot(center_normal, sample_normal), 0.0), 24.0);
            float spatial_weight = float((3 - abs(x)) * (3 - abs(y)));
            float weight = depth_weight * normal_weight * spatial_weight;
            visibility += texelFetch(raw_ao, sample_pixel, 0).r * weight;
            total_weight += weight;
        }
    }
    out_visibility = total_weight > 1e-5 ? clamp(visibility / total_weight, 0.0, 1.0) : 1.0;
}
