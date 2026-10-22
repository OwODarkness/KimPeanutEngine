#version 450

layout(binding = 0) uniform sampler2D current_hdr;
layout(binding = 1) uniform sampler2D motion_depth_validity;
layout(binding = 2) uniform sampler2D current_normal;
layout(binding = 3) uniform sampler2D previous_history;
layout(binding = 4) uniform sampler2D previous_metadata;
layout(binding = 5) uniform sampler2D current_identity;
layout(binding = 6) uniform sampler2D previous_identity;
layout(binding = 7, std140) uniform TemporalAaOptions { vec4 values; } options;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out vec4 out_resolved;
layout(location = 1) out vec4 out_metadata;
layout(location = 2) out vec4 out_identity;

vec2 SignNotZero(vec2 value)
{
    return vec2(value.x >= 0.0 ? 1.0 : -1.0,
                value.y >= 0.0 ? 1.0 : -1.0);
}

vec2 EncodeOctahedralNormal(vec3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    vec2 encoded = normal.xy;
    if (normal.z < 0.0)
        encoded = (1.0 - abs(encoded.yx)) * SignNotZero(encoded.xy);
    return encoded * 0.5 + 0.5;
}

vec3 DecodeOctahedralNormal(vec2 encoded)
{
    const vec2 f = encoded * 2.0 - 1.0;
    vec3 normal = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    if (normal.z < 0.0)
        normal.xy = (1.0 - abs(normal.yx)) * SignNotZero(normal.xy);
    return normalize(normal);
}

vec4 EncodeHistoryMetadata(vec3 normal, float view_depth)
{
    const float max_depth = 65504.0;
    const float log_range = log2(1.0 + max_depth);
    const float normalized_depth = log2(1.0 + clamp(view_depth, 0.0, max_depth)) /
                                   log_range;
    const uint packed_depth = uint(round(normalized_depth * 65535.0));
    return vec4(EncodeOctahedralNormal(normal),
                float((packed_depth >> 8u) & 255u) / 255.0,
                float(packed_depth & 255u) / 255.0);
}

vec3 DecodeHistoryNormal(vec4 metadata)
{
    return DecodeOctahedralNormal(metadata.xy);
}

float DecodeHistoryDepth(vec4 metadata)
{
    const uint high_byte = uint(round(metadata.z * 255.0));
    const uint low_byte = uint(round(metadata.w * 255.0));
    const uint packed_depth = (high_byte << 8u) | low_byte;
    const float normalized_depth = float(packed_depth) / 65535.0;
    return exp2(normalized_depth * log2(1.0 + 65504.0)) - 1.0;
}

vec3 SampleCatmullRom(sampler2D source, vec2 uv)
{
    const vec2 extent = vec2(textureSize(source, 0));
    const vec2 position = uv * extent - 0.5;
    const vec2 base = floor(position);
    const vec2 f = position - base;
    const vec2 f2 = f * f;
    const vec2 f3 = f2 * f;
    const vec2 w0 = -0.5 * f + f2 - 0.5 * f3;
    const vec2 w1 = 1.0 - 2.5 * f2 + 1.5 * f3;
    const vec2 w2 = 0.5 * f + 2.0 * f2 - 1.5 * f3;
    const vec2 w3 = -0.5 * f2 + 0.5 * f3;
    const vec2 weights[3] = vec2[3](w0, w1 + w2, w3);
    const vec2 positions[3] = vec2[3](base - 0.5,
        base + 0.5 + w2 / (w1 + w2), base + 2.5);
    vec3 value = vec3(0.0);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
        {
            const vec2 sample_uv = clamp(vec2(positions[x].x, positions[y].y) / extent,
                                         0.5 / extent, 1.0 - 0.5 / extent);
            value += textureLod(source, sample_uv, 0.0).rgb * weights[x].x * weights[y].y;
        }
    return clamp(value, vec3(0.0), vec3(65504.0));
}

vec3 ToYCoCg(vec3 color)
{
    return vec3(dot(color, vec3(0.25, 0.5, 0.25)),
                color.r * 0.5 - color.b * 0.5,
                color.g * 0.5 - (color.r + color.b) * 0.25);
}

vec3 FromYCoCg(vec3 color)
{
    return vec3(color.x + color.y - color.z, color.x + color.z,
                color.x - color.y - color.z);
}

void main()
{
    const ivec2 extent = textureSize(current_hdr, 0);
    vec2 current_uv = frag_texcoord;
#if KP_GRAPHICS_API_VULKAN
    // Preserve SceneHdr orientation across this extra fullscreen pass.
    current_uv.y = 1.0 - current_uv.y;
#endif
    const ivec2 pixel = clamp(ivec2(current_uv * vec2(extent)),
                              ivec2(0), extent - ivec2(1));
    const ivec2 guide_pixel = clamp(ivec2(frag_texcoord * vec2(extent)),
                                   ivec2(0), extent - ivec2(1));
    const vec4 motion = texelFetch(motion_depth_validity, guide_pixel, 0);
    const vec4 normal_depth = texelFetch(current_normal, guide_pixel, 0);
    const vec3 normal = length(normal_depth.xyz) > 1e-6
        ? normalize(normal_depth.xyz) : vec3(0.0, 0.0, 1.0);
    const vec2 current_jitter = vec2(options.values.z, -options.values.w);
    const vec2 half_texel = 0.5 / vec2(extent);
    const vec2 current_sample_uv = clamp(current_uv + current_jitter,
                                         half_texel, 1.0 - half_texel);
    const vec3 current = max(textureLod(current_hdr, current_sample_uv, 0.0).rgb,
                             vec3(0.0));
    vec3 resolved = current;

    if (options.values.x > 0.5 && motion.w > 0.5)
    {
        // History is on the unjittered output grid; motion removed jitter once.
        const vec2 previous_uv = current_uv + vec2(motion.x, -motion.y);
        if (all(greaterThanEqual(previous_uv, vec2(0.0))) &&
            all(lessThanEqual(previous_uv, vec2(1.0))))
        {
            const ivec2 prior_pixel = clamp(ivec2(previous_uv * vec2(extent)),
                                            ivec2(0), extent - ivec2(1));
            const vec4 prior_meta = texelFetch(previous_metadata, prior_pixel, 0);
            const vec4 identity = texelFetch(current_identity, guide_pixel, 0);
            const vec4 prior_identity = texelFetch(previous_identity, prior_pixel, 0);
            const float depth_tolerance = max(0.02, normal_depth.w * 0.02);
            const float depth_delta = abs(DecodeHistoryDepth(prior_meta) - normal_depth.w);
            const vec3 prior_normal = DecodeHistoryNormal(prior_meta);
            float normal_agreement = dot(prior_normal, normal);
            const bool identity_valid = any(greaterThan(identity, vec4(0.0)));
            const bool identity_matches = identity_valid &&
                                          all(equal(identity, prior_identity));
            // Normal maps vary within a pixel; bound rejection by the same surface's footprint.
            if (identity_matches && depth_delta <= depth_tolerance && normal_agreement < 0.85)
            {
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x)
                    {
                        const ivec2 neighbor_pixel = clamp(guide_pixel + ivec2(x, y),
                                                           ivec2(0), extent - ivec2(1));
                        const vec4 neighbor = texelFetch(current_normal, neighbor_pixel, 0);
                        if (all(equal(texelFetch(current_identity, neighbor_pixel, 0), identity)) &&
                            abs(neighbor.w - normal_depth.w) <= depth_tolerance &&
                            dot(neighbor.xyz, neighbor.xyz) > 1e-6)
                            normal_agreement = max(normal_agreement,
                                dot(prior_normal, normalize(neighbor.xyz)));
                    }
            }
            if (identity_matches && normal_depth.w > 0.0 &&
                depth_delta <= depth_tolerance &&
                normal_agreement >= 0.85)
            {
                vec3 neighborhood_min = ToYCoCg(current);
                vec3 neighborhood_max = neighborhood_min;
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x)
                    {
                        const ivec2 sample_pixel = clamp(pixel + ivec2(x, y),
                                                         ivec2(0), extent - ivec2(1));
                        const vec3 sample_color = ToYCoCg(max(
                            texelFetch(current_hdr, sample_pixel, 0).rgb, vec3(0.0)));
                        neighborhood_min = min(neighborhood_min, sample_color);
                        neighborhood_max = max(neighborhood_max, sample_color);
                    }
                const vec3 prior = max(FromYCoCg(clamp(
                    ToYCoCg(SampleCatmullRom(previous_history, previous_uv)),
                    neighborhood_min, neighborhood_max)), vec3(0.0));
                // Compress HDR during filtering so bright subpixel samples stay bounded.
                const vec3 filtered = mix(current / (1.0 + current), prior / (1.0 + prior),
                                           clamp(options.values.y, 0.0, 0.95));
                resolved = min(filtered / max(1.0 - filtered, vec3(1e-5)), vec3(65504.0));
            }
        }
    }
    out_resolved = vec4(resolved, 1.0);
    out_metadata = EncodeHistoryMetadata(normal, motion.z);
    out_identity = texelFetch(current_identity, guide_pixel, 0);
}
