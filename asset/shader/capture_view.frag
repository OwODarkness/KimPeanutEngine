#version 450

const uint CAPTURE_LINEAR_DEPTH = 1u;
const uint CAPTURE_WORLD_NORMAL = 2u;
const uint CAPTURE_BASE_COLOR = 3u;
const uint CAPTURE_MATERIAL_PARAMS = 4u;
const uint CAPTURE_SHADOW_VISIBILITY = 5u;

layout(binding = 2) uniform sampler2D gbuffer_albedo;
layout(binding = 3) uniform sampler2D gbuffer_normal;
layout(binding = 4) uniform sampler2D gbuffer_material;
layout(binding = 5) uniform sampler2D gbuffer_depth;
layout(binding = 6) uniform sampler2D directional_shadow_depth;

layout(std140, binding = 7) uniform CaptureViewConstants
{
    mat4 inverse_view_projection;
    mat4 view;
    mat4 directional_shadow_view_projection;
    vec4 directional_shadow_params;
    vec4 light_direction_and_view;
    vec4 depth_params;
} capture_constants;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out vec4 out_color;

vec3 reconstruct_world_position(vec2 source_uv, float depth)
{
    float ndc_z = depth * 2.0 - 1.0;
    vec2 ndc_xy = source_uv * 2.0 - 1.0;
#if KP_GRAPHICS_API_VULKAN
    ndc_xy.y = -ndc_xy.y;
#endif
    vec4 world = capture_constants.inverse_view_projection *
                 vec4(ndc_xy, ndc_z, 1.0);
    return world.xyz / max(abs(world.w), 1e-7) * sign(world.w);
}

float shadow_visibility(vec3 world_position, vec3 normal)
{
    if (capture_constants.depth_params.y < 0.5)
    {
        return 1.0;
    }
    vec4 light_clip = capture_constants.directional_shadow_view_projection *
                      vec4(world_position, 1.0);
    if (abs(light_clip.w) <= 1e-7)
    {
        return 1.0;
    }
    vec3 light_ndc = light_clip.xyz / light_clip.w;
    vec2 shadow_uv = light_ndc.xy * 0.5 + 0.5;
#if KP_GRAPHICS_API_VULKAN
    shadow_uv.y = 1.0 - shadow_uv.y;
#endif
    float receiver_depth = light_ndc.z * 0.5 + 0.5;
    if (any(lessThan(shadow_uv, vec2(0.0))) ||
        any(greaterThan(shadow_uv, vec2(1.0))) ||
        receiver_depth < 0.0 || receiver_depth > 1.0)
    {
        return 1.0;
    }

    vec3 light_direction = normalize(capture_constants.light_direction_and_view.xyz);
    float bias = max(capture_constants.directional_shadow_params.x,
                     capture_constants.directional_shadow_params.y *
                         (1.0 - max(dot(normal, light_direction), 0.0)));
    float texel_size = capture_constants.directional_shadow_params.z;
    float occluded_samples = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float stored_depth = texture(
                directional_shadow_depth,
                shadow_uv + vec2(float(x), float(y)) * texel_size).r;
            occluded_samples += receiver_depth - bias > stored_depth ? 1.0 : 0.0;
        }
    }
    return 1.0 - occluded_samples / 9.0;
}

void main()
{
    uint view = uint(capture_constants.light_direction_and_view.w + 0.5);
    vec2 source_uv = frag_texcoord;
#if KP_GRAPHICS_API_VULKAN
    // A single conversion pass must undo Vulkan's negative-height producer
    // orientation. Final SceneColor receives two fullscreen conversions and
    // therefore already cancels this flip.
    source_uv.y = 1.0 - source_uv.y;
#endif
    float depth = texture(gbuffer_depth, source_uv).r;
    if (depth >= 0.999999)
    {
        float background = view == CAPTURE_LINEAR_DEPTH ||
                           view == CAPTURE_SHADOW_VISIBILITY ? 1.0 : 0.0;
        out_color = vec4(vec3(background), 1.0);
        return;
    }

    if (view == CAPTURE_BASE_COLOR)
    {
        out_color = vec4(max(texture(gbuffer_albedo, source_uv).rgb, vec3(0.0)), 1.0);
        return;
    }
    if (view == CAPTURE_WORLD_NORMAL)
    {
        vec3 normal = normalize(texture(gbuffer_normal, source_uv).xyz);
        out_color = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (view == CAPTURE_MATERIAL_PARAMS)
    {
        out_color = vec4(clamp(texture(gbuffer_material, source_uv).rgb, 0.0, 1.0), 1.0);
        return;
    }

    vec3 world_position = reconstruct_world_position(source_uv, depth);
    if (view == CAPTURE_LINEAR_DEPTH)
    {
        vec4 view_position = capture_constants.view * vec4(world_position, 1.0);
        float normalized_depth = clamp(-view_position.z /
                                       max(capture_constants.depth_params.x, 1e-6),
                                       0.0, 1.0);
        out_color = vec4(vec3(normalized_depth), 1.0);
        return;
    }

    vec3 normal_sample = texture(gbuffer_normal, source_uv).xyz;
    vec3 normal = dot(normal_sample, normal_sample) > 1e-8
                      ? normalize(normal_sample)
                      : vec3(0.0, 0.0, 1.0);
    float visibility = shadow_visibility(world_position, normal);
    out_color = vec4(vec3(visibility), 1.0);
}
