#version 450

const float PI = 3.14159265359;
const uint LIGHT_ABI_VERSION = 1u;
const uint LIGHT_TYPE_DIRECTIONAL = 0u;
const uint SHADOW_KIND_DIRECTIONAL_2D = 1u;
const uint DIRECTIONAL_SHADOW_BINDING_SLOT = 0u;
const uint MAX_FRAME_LIGHTS = 64u;

layout(binding = 0) uniform sampler2D gbuffer_albedo;
layout(binding = 1) uniform sampler2D gbuffer_normal;
layout(binding = 2) uniform sampler2D gbuffer_material;
layout(binding = 3) uniform sampler2D gbuffer_depth;
layout(binding = 6) uniform sampler2D directional_shadow_depth;
layout(binding = 7) uniform sampler2D environment_radiance;

struct LightGpuData
{
    vec4 color_intensity;
    vec4 position_range;
    vec4 direction_inner_cone;
    float outer_cone_radians;
    uint type;
    uint shadow_kind;
    uint shadow_binding_slot;
    uint layer_mask;
    uint enabled;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};

layout(std140, binding = 4) uniform FrameLighting
{
    uvec4 header;
    LightGpuData lights[MAX_FRAME_LIGHTS];
} frame_lighting;

layout(std140, binding = 5) uniform DeferredLightingConstants
{
    mat4 inverse_view_projection;
    vec4 camera_world_position;
    mat4 directional_shadow_view_projection;
    vec4 directional_shadow_params;
} lighting_constants;

layout(location = 0) in vec2 frag_texcoord;
layout(location = 0) out vec4 out_color;

vec3 reconstruct_world_position(float depth)
{
    float ndc_z = depth * 2.0 - 1.0;
    vec2 ndc_xy = frag_texcoord * 2.0 - 1.0;
#if KP_GRAPHICS_API_VULKAN
    ndc_xy.y = -ndc_xy.y;
#endif
    vec4 world = lighting_constants.inverse_view_projection *
                 vec4(ndc_xy, ndc_z, 1.0);
    return world.xyz / max(abs(world.w), 1e-7) * sign(world.w);
}

vec3 sample_environment_background()
{
    vec3 world_far = reconstruct_world_position(1.0);
    vec3 direction = normalize(world_far - lighting_constants.camera_world_position.xyz);
    vec2 environment_uv = vec2(
        atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
        asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
    return max(texture(environment_radiance, environment_uv).rgb, vec3(0.0));
}

float distribution_ggx(vec3 normal, vec3 halfway, float roughness)
{
    float alpha = roughness * roughness;
    float alpha_squared = alpha * alpha;
    float n_dot_h = max(dot(normal, halfway), 0.0);
    float denominator = n_dot_h * n_dot_h * (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(PI * denominator * denominator, 1e-7);
}

float geometry_schlick_ggx(float n_dot_direction, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return n_dot_direction / max(n_dot_direction * (1.0 - k) + k, 1e-7);
}

float geometry_smith(vec3 normal, vec3 view_direction, vec3 light_direction,
                     float roughness)
{
    return geometry_schlick_ggx(max(dot(normal, view_direction), 0.0), roughness) *
           geometry_schlick_ggx(max(dot(normal, light_direction), 0.0), roughness);
}

vec3 fresnel_schlick(float cosine, vec3 reflectance_at_normal)
{
    return reflectance_at_normal + (1.0 - reflectance_at_normal) *
           pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

float directional_shadow_visibility(vec3 world_position, vec3 normal,
                                    vec3 light_direction)
{
    vec4 light_clip = lighting_constants.directional_shadow_view_projection *
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

    float bias = max(lighting_constants.directional_shadow_params.x,
                     lighting_constants.directional_shadow_params.y *
                         (1.0 - max(dot(normal, light_direction), 0.0)));
    float texel_size = lighting_constants.directional_shadow_params.z;
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
    float depth = texture(gbuffer_depth, frag_texcoord).r;
    if (depth >= 0.999999)
    {
        out_color = vec4(sample_environment_background(), 1.0);
        return;
    }

    vec3 albedo = max(texture(gbuffer_albedo, frag_texcoord).rgb, vec3(0.0));
    vec3 normal_sample = texture(gbuffer_normal, frag_texcoord).xyz;
    vec3 normal = dot(normal_sample, normal_sample) > 1e-8
                      ? normalize(normal_sample)
                      : vec3(0.0, 0.0, 1.0);
    vec3 material = texture(gbuffer_material, frag_texcoord).rgb;
    float metallic = clamp(material.r, 0.0, 1.0);
    float roughness = clamp(material.g, 0.045, 1.0);
    float occlusion = clamp(material.b, 0.0, 1.0);
    vec3 world_position = reconstruct_world_position(depth);
    vec3 view_delta = lighting_constants.camera_world_position.xyz - world_position;
    vec3 view_direction = dot(view_delta, view_delta) > 1e-8
                              ? normalize(view_delta)
                              : normal;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 direct = vec3(0.0);

    uint light_count = frame_lighting.header.x == LIGHT_ABI_VERSION
                           ? min(frame_lighting.header.y, MAX_FRAME_LIGHTS)
                           : 0u;
    for (uint index = 0u; index < light_count; ++index)
    {
        LightGpuData light = frame_lighting.lights[index];
        if (light.enabled == 0u || light.type != LIGHT_TYPE_DIRECTIONAL)
        {
            continue;
        }

        // Authored direction is the direction the light travels; surface-to-light
        // is its negation.
        vec3 light_direction = normalize(-light.direction_inner_cone.xyz);
        vec3 halfway = normalize(view_direction + light_direction);
        float n_dot_l = max(dot(normal, light_direction), 0.0);
        if (n_dot_l <= 0.0)
        {
            continue;
        }

        float ndf = distribution_ggx(normal, halfway, roughness);
        float geometry = geometry_smith(normal, view_direction, light_direction, roughness);
        vec3 fresnel = fresnel_schlick(max(dot(halfway, view_direction), 0.0), f0);
        vec3 specular = ndf * geometry * fresnel /
                        max(4.0 * max(dot(normal, view_direction), 0.0) * n_dot_l, 1e-5);
        vec3 diffuse_weight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec3 radiance = light.color_intensity.rgb * light.color_intensity.a;
        float shadow_visibility = 1.0;
        if (light.shadow_kind == SHADOW_KIND_DIRECTIONAL_2D &&
            light.shadow_binding_slot == DIRECTIONAL_SHADOW_BINDING_SLOT)
        {
            shadow_visibility = directional_shadow_visibility(
                world_position, normal, light_direction);
        }
        direct += (diffuse_weight * albedo / PI + specular) * radiance * n_dot_l *
                  shadow_visibility;
    }

    // A small non-IBL visibility floor keeps occluded material readable while
    // derived environment assets remain explicitly out of scope.
    vec3 ambient = 0.02 * albedo * occlusion;
    out_color = vec4(ambient + direct, 1.0);
}
