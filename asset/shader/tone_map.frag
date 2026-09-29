#version 450

layout(binding = 2) uniform sampler2D scene_hdr;
layout(binding = 3) uniform sampler2D selection_mask;
layout(binding = 4, std140) uniform ToneMapOptions { vec4 values; } options;
layout(binding = 5) uniform sampler2D path_trace_guide;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out vec4 out_color;

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 CompressRadiance(vec3 color)
{
    color = max(color, vec3(0.0));
    return color / (1.0 + Luminance(color));
}

float GuideWeight(vec4 center, vec4 neighbor)
{
    if (center.w < 0.0 || neighbor.w < 0.0)
        return center.w < 0.0 && neighbor.w < 0.0 ? 1.0 : 0.0;
    const float normal_weight = pow(max(dot(normalize(center.xyz),
                                           normalize(neighbor.xyz)), 0.0), 24.0);
    const float depth_scale = max(0.02, center.w * 0.025);
    return normal_weight * exp(-abs(neighbor.w - center.w) / depth_scale);
}

vec3 ReconstructMovingPreview(ivec2 center, ivec2 extent)
{
    const vec4 center_guide = texelFetch(path_trace_guide, center, 0);
    float mean = 0.0;
    float second_moment = 0.0;
    float local_weight = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            const ivec2 pixel = clamp(center + ivec2(x, y), ivec2(0), extent - 1);
            const float weight = GuideWeight(center_guide,
                texelFetch(path_trace_guide, pixel, 0));
            const float luminance = Luminance(CompressRadiance(
                texelFetch(scene_hdr, pixel, 0).rgb));
            mean += weight * luminance;
            second_moment += weight * luminance * luminance;
            local_weight += weight;
        }
    if (local_weight < 0.01)
        return max(texelFetch(scene_hdr, center, 0).rgb, vec3(0.0));
    mean /= local_weight;
    const float sigma = sqrt(max(second_moment / local_weight - mean * mean, 0.0));
    // At one sample, spatial variance supplies the unavailable temporal estimate.
    const float range_scale = max(3.0 * sigma, 0.18);
    const float firefly_limit = min(mean + 2.5 * sigma + 0.04, 0.98);
    const float reference_luminance = mean;
    const float kernel[9] = float[9](0.1353, 0.3247, 0.6065, 0.8825, 1.0,
                                    0.8825, 0.6065, 0.3247, 0.1353);
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;
    for (int y = -4; y <= 4; ++y)
        for (int x = -4; x <= 4; ++x)
        {
            const ivec2 pixel = clamp(center + ivec2(x, y), ivec2(0), extent - 1);
            const float guide_weight = GuideWeight(center_guide,
                texelFetch(path_trace_guide, pixel, 0));
            vec3 color = CompressRadiance(texelFetch(scene_hdr, pixel, 0).rgb);
            const float luminance = Luminance(color);
            color *= min(1.0, firefly_limit / max(luminance, 1e-6));
            const float range_weight = exp(-abs(Luminance(color) - reference_luminance) /
                                           range_scale);
            const float weight = kernel[x + 4] * kernel[y + 4] *
                                 guide_weight * range_weight;
            sum += color * weight;
            weight_sum += weight;
        }
    const vec3 compressed = sum / max(weight_sum, 1e-6);
    return compressed / max(1.0 - Luminance(compressed), 0.02);
}

void main()
{
    const vec4 center_color = texture(scene_hdr, frag_texcoord);
    vec3 hdr = max(center_color.rgb, vec3(0.0));
    if (options.values.y > 0.5 && options.values.y < 1.5)
    {
        const ivec2 extent = textureSize(scene_hdr, 0);
        const ivec2 center = clamp(ivec2(frag_texcoord * vec2(extent)),
                                   ivec2(0), extent - ivec2(1));
        hdr = ReconstructMovingPreview(center, extent);
    }
    else if (options.values.y > 1.5)
    {
        const ivec2 extent = textureSize(scene_hdr, 0);
        const ivec2 center = clamp(ivec2(frag_texcoord * vec2(extent)),
                                   ivec2(0), extent - ivec2(1));
        const vec4 center_sample = texelFetch(scene_hdr, center, 0);
        const vec4 center_guide = texelFetch(path_trace_guide, center, 0);
        const vec3 center_normal = normalize(center_guide.xyz);
        const float sample_count = max(options.values.z, 2.0);
        const float center_luminance = dot(center_sample.rgb,
                                           vec3(0.2126, 0.7152, 0.0722));
        const float center_variance = max(center_sample.a, 0.0) / sample_count;
        const float kernel[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
        vec3 sum = max(center_sample.rgb, vec3(0.0)) * 36.0;
        float weight_sum = 36.0;
        for (int y = -2; y <= 2; ++y)
        {
            for (int x = -2; x <= 2; ++x)
            {
                if (x == 0 && y == 0)
                    continue;
                const ivec2 sample_pixel = clamp(center + ivec2(x, y) * 2,
                                                 ivec2(0), extent - ivec2(1));
                const vec4 neighbor_guide = texelFetch(path_trace_guide,
                                                       sample_pixel, 0);
                float guide_weight = 0.0;
                if (center_guide.w < 0.0 && neighbor_guide.w < 0.0)
                    guide_weight = 1.0;
                else if (center_guide.w >= 0.0 && neighbor_guide.w >= 0.0)
                {
                    const float normal_weight = pow(max(dot(center_normal,
                        normalize(neighbor_guide.xyz)), 0.0), 24.0);
                    const float depth_scale = max(0.02, center_guide.w * 0.025);
                    const float depth_weight = exp(-abs(neighbor_guide.w -
                        center_guide.w) / depth_scale);
                    guide_weight = normal_weight * depth_weight;
                }

                const vec4 neighbor_sample = texelFetch(scene_hdr, sample_pixel, 0);
                const float neighbor_luminance = dot(neighbor_sample.rgb,
                    vec3(0.2126, 0.7152, 0.0722));
                const float neighbor_variance = max(neighbor_sample.a, 0.0) /
                                                sample_count;
                const float low_spp_noise_floor = sample_count <= 2.0
                    ? 0.5 * max(max(center_luminance, neighbor_luminance), 0.25)
                    : 0.0;
                const float luminance_scale = max(low_spp_noise_floor,
                    max(0.01, 3.0 * sqrt(center_variance + neighbor_variance) +
                    0.02 * max(center_luminance, neighbor_luminance)));
                const float luminance_weight = exp(-abs(neighbor_luminance -
                    center_luminance) / luminance_scale);
                const float kernel_weight = kernel[x + 2] * kernel[y + 2];
                const float weight = kernel_weight * guide_weight * luminance_weight;
                sum += max(neighbor_sample.rgb, vec3(0.0)) * weight;
                weight_sum += weight;
            }
        }
        hdr = sum / max(weight_sum, 1e-6);
    }
    // Global Reinhard maps linear HDR into display-linear [0, 1]. The sRGB
    // SceneColor attachment performs the final transfer-function encoding.
    vec3 mapped = hdr / (vec3(1.0) + hdr);
    if (options.values.x > 0.5)
    {
        vec2 mask_uv = frag_texcoord;
#if KP_GRAPHICS_API_VULKAN
        // SceneHdr has already passed through the Vulkan fullscreen orientation
        // conversion. The selection mask is sampled directly from the G-buffer,
        // so it needs the same source-space correction as CaptureView.
        mask_uv.y = 1.0 - mask_uv.y;
#endif
        float selected = texture(selection_mask, mask_uv).r;
        vec2 texel_size = 1.0 / vec2(textureSize(selection_mask, 0));
        float neighbor_selected = 0.0;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                neighbor_selected = max(
                    neighbor_selected,
                    texture(selection_mask,
                    mask_uv + vec2(float(x), float(y)) * texel_size).r);
            }
        }
        float outline = clamp(neighbor_selected - selected, 0.0, 1.0);
        const vec3 highlight_color = vec3(1.0, 0.72, 0.08);
        mapped = mix(mapped, highlight_color, outline * 0.9);
    }
    out_color = vec4(mapped, 1.0);
}
