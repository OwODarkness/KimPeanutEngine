#version 450

const float PI = 3.14159265359;
const uint LIGHT_ABI_VERSION = 1u;
const uint LIGHT_TYPE_DIRECTIONAL = 0u;
const uint LIGHT_TYPE_POINT = 1u;
const uint LIGHT_TYPE_SPOT = 2u;
const uint SHADOW_KIND_DIRECTIONAL_2D = 1u;
const uint SHADOW_KIND_SPOT_2D = 2u;
const uint SHADOW_KIND_POINT_CUBE = 3u;
const uint DIRECTIONAL_SHADOW_BINDING_SLOT = 0u;
const uint SPOT_SHADOW_BINDING_SLOT = 1u;
const uint MAX_FRAME_LIGHTS = 64u;

layout(binding = 0) uniform sampler2D gbuffer_albedo;
layout(binding = 1) uniform sampler2D gbuffer_normal;
layout(binding = 2) uniform sampler2D gbuffer_material;
layout(binding = 3) uniform sampler2D gbuffer_depth;
layout(binding = 6) uniform sampler2D directional_shadow_depth;
layout(binding = 11) uniform sampler2D spot_shadow_depth;
layout(binding = 12) uniform sampler2D point_shadow_depth;
layout(binding = 7) uniform sampler2D environment_radiance;
layout(binding = 8) uniform sampler2D environment_irradiance;
layout(binding = 9) uniform sampler2D environment_prefilter;
layout(binding = 10) uniform sampler2D environment_brdf_lut;

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
    mat4 spot_shadow_view_projection;
    vec4 spot_shadow_params;
    vec4 environment_ibl_params;
} lighting_constants;

layout(std140, binding = 13) uniform PointShadowConstants
{
    mat4 point_face_view_projection[6];
    vec4 point_shadow_params;
} point_shadow_constants;

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

vec2 environment_uv(vec3 direction)
{
    direction = normalize(direction);
    return vec2(atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
                asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
}

vec3 sample_environment_background()
{
    vec3 world_far = reconstruct_world_position(1.0);
    vec3 direction = normalize(world_far - lighting_constants.camera_world_position.xyz);
    return max(texture(environment_radiance, environment_uv(direction)).rgb, vec3(0.0));
}

vec3 fresnel_schlick_roughness(float cosine, vec3 reflectance_at_normal,
                               float roughness)
{
    return reflectance_at_normal +
           (max(vec3(1.0 - roughness), reflectance_at_normal) - reflectance_at_normal) *
               pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 sample_prefiltered_environment(vec3 direction, float roughness)
{
    float level_count = max(lighting_constants.environment_ibl_params.y, 2.0);
    float level = clamp(roughness, 0.0, 1.0) * (level_count - 1.0);
    float lower_level = floor(level);
    float upper_level = min(lower_level + 1.0, level_count - 1.0);
    vec2 uv = environment_uv(direction);
    float atlas_height = float(textureSize(environment_prefilter, 0).y);
    float band_height = atlas_height / level_count;
    float half_texel = 0.5 / max(band_height, 1.0);
    float band_v = clamp(uv.y, half_texel, 1.0 - half_texel);
    vec3 lower = texture(environment_prefilter,
                         vec2(uv.x, (lower_level + band_v) / level_count)).rgb;
    vec3 upper = texture(environment_prefilter,
                         vec2(uv.x, (upper_level + band_v) / level_count)).rgb;
    return max(mix(lower, upper, level - lower_level), vec3(0.0));
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

float spot_shadow_visibility(vec3 world_position, vec3 normal,
                             vec3 light_direction)
{
    vec4 light_clip = lighting_constants.spot_shadow_view_projection *
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
    float bias = max(lighting_constants.spot_shadow_params.x,
                     lighting_constants.spot_shadow_params.y *
                         (1.0 - max(dot(normal, light_direction), 0.0)));
    float texel_size = lighting_constants.spot_shadow_params.z;
    float occluded_samples = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float stored_depth = texture(
                spot_shadow_depth,
                shadow_uv + vec2(float(x), float(y)) * texel_size).r;
            occluded_samples += receiver_depth - bias > stored_depth ? 1.0 : 0.0;
        }
    }
    return 1.0 - occluded_samples / 9.0;
}

float point_shadow_visibility(vec3 world_position, vec3 normal,
                              vec3 light_position)
{
    float face_x = abs(world_position.x - light_position.x);
    float face_y = abs(world_position.y - light_position.y);
    float face_z = abs(world_position.z - light_position.z);
    int face = face_x >= face_y && face_x >= face_z
                   ? (world_position.x >= light_position.x ? 0 : 1)
                   : (face_y >= face_z
                          ? (world_position.y >= light_position.y ? 2 : 3)
                          : (world_position.z >= light_position.z ? 4 : 5));
    vec4 light_clip = point_shadow_constants.point_face_view_projection[face] *
                      vec4(world_position, 1.0);
    if (abs(light_clip.w) <= 1e-7)
    {
        return 1.0;
    }
    vec3 light_ndc = light_clip.xyz / light_clip.w;
    vec2 local_uv = light_ndc.xy * 0.5 + 0.5;
#if KP_GRAPHICS_API_VULKAN
    local_uv.y = 1.0 - local_uv.y;
#endif
    float receiver_depth = light_ndc.z * 0.5 + 0.5;
    if (any(lessThan(local_uv, vec2(0.0))) || any(greaterThan(local_uv, vec2(1.0))) ||
        receiver_depth < 0.0 || receiver_depth > 1.0 ||
        point_shadow_constants.point_shadow_params.y <= 0.0)
    {
        return 1.0;
    }
    const vec2 tile_scale = vec2(1.0 / 3.0, 1.0 / 2.0);
    const vec2 tile_origin[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(2.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(2.0, 1.0));
    vec2 shadow_uv = tile_origin[face] * tile_scale + local_uv * tile_scale;
    vec2 texel_size = vec2(point_shadow_constants.point_shadow_params.x,
                           point_shadow_constants.point_shadow_params.y);
    vec2 tile_min = tile_origin[face] * tile_scale + 0.5 * texel_size;
    vec2 tile_max = (tile_origin[face] + vec2(1.0)) * tile_scale - 0.5 * texel_size;
    float bias = max(point_shadow_constants.point_shadow_params.z,
                     point_shadow_constants.point_shadow_params.w *
                         (1.0 - max(dot(normal, normalize(light_position - world_position)), 0.0)));
    float occluded_samples = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 tap_uv = clamp(shadow_uv + vec2(float(x), float(y)) * texel_size,
                                tile_min, tile_max);
            float stored_depth = texture(point_shadow_depth, tap_uv).r;
            occluded_samples += receiver_depth - bias > stored_depth ? 1.0 : 0.0;
        }
    }
    return 1.0 - occluded_samples / 9.0;
}

float point_range_attenuation(float distance_to_light, float range)
{
    float range_ratio = distance_to_light / max(range, 1e-4);
    float cutoff = max(1.0 - range_ratio * range_ratio * range_ratio * range_ratio, 0.0);
    return cutoff / max(distance_to_light * distance_to_light, 1e-4);
}

float spot_cone_attenuation(vec3 spot_direction, vec3 surface_to_light,
                            float inner_cone_radians, float outer_cone_radians)
{
    float cosine_inner = cos(inner_cone_radians);
    float cosine_outer = cos(outer_cone_radians);
    float scale = 1.0 / max(cosine_inner - cosine_outer, 1e-4);
    float cosine_angle = dot(normalize(spot_direction), -surface_to_light);
    float attenuation = clamp(cosine_angle * scale - cosine_outer * scale, 0.0, 1.0);
    return attenuation * attenuation;
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
        if (light.enabled == 0u)
        {
            continue;
        }

        vec3 light_direction;
        float attenuation = 1.0;
        bool directional = light.type == LIGHT_TYPE_DIRECTIONAL;
        if (directional)
        {
            // Authored direction is the direction the light travels; surface-to-light
            // is its negation.
            light_direction = normalize(-light.direction_inner_cone.xyz);
        }
        else if (light.type == LIGHT_TYPE_POINT)
        {
            vec3 light_delta = light.position_range.xyz - world_position;
            float distance_to_light = length(light_delta);
            if (distance_to_light >= light.position_range.w)
            {
                continue;
            }
            light_direction = light_delta / max(distance_to_light, 1e-4);
            attenuation = point_range_attenuation(distance_to_light, light.position_range.w);
        }
        else if (light.type == LIGHT_TYPE_SPOT)
        {
            vec3 light_delta = light.position_range.xyz - world_position;
            float distance_to_light = length(light_delta);
            if (distance_to_light >= light.position_range.w)
            {
                continue;
            }
            light_direction = light_delta / max(distance_to_light, 1e-4);
            attenuation = point_range_attenuation(distance_to_light, light.position_range.w) *
                          spot_cone_attenuation(light.direction_inner_cone.xyz, light_direction,
                                                light.direction_inner_cone.w,
                                                light.outer_cone_radians);
        }
        else
        {
            continue;
        }
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
        vec3 radiance = light.color_intensity.rgb * light.color_intensity.a * attenuation;
        float shadow_visibility = 1.0;
        if (directional && light.shadow_kind == SHADOW_KIND_DIRECTIONAL_2D &&
            light.shadow_binding_slot == DIRECTIONAL_SHADOW_BINDING_SLOT)
        {
            shadow_visibility = directional_shadow_visibility(
                world_position, normal, light_direction);
        }
        else if (!directional && light.type == LIGHT_TYPE_SPOT &&
                 light.shadow_kind == SHADOW_KIND_SPOT_2D &&
                 light.shadow_binding_slot == SPOT_SHADOW_BINDING_SLOT)
        {
            shadow_visibility = spot_shadow_visibility(
                world_position, normal, light_direction);
        }
        else if (light.type == LIGHT_TYPE_POINT &&
                 light.shadow_kind == SHADOW_KIND_POINT_CUBE &&
                 light.shadow_binding_slot == 2u)
        {
            shadow_visibility = point_shadow_visibility(
                world_position, normal, light.position_range.xyz);
        }
        direct += (diffuse_weight * albedo / PI + specular) * radiance * n_dot_l *
                  shadow_visibility;
    }

    vec3 ambient;
    if (lighting_constants.environment_ibl_params.x > 0.5)
    {
        float n_dot_v = max(dot(normal, view_direction), 0.0);
        vec3 fresnel = fresnel_schlick_roughness(n_dot_v, f0, roughness);
        vec3 diffuse_weight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec3 irradiance = max(texture(environment_irradiance,
                                      environment_uv(normal)).rgb,
                              vec3(0.0));
        vec3 reflected = reflect(-view_direction, normal);
        vec3 prefiltered = sample_prefiltered_environment(reflected, roughness);
        vec2 brdf = texture(environment_brdf_lut,
                            vec2(n_dot_v, roughness)).rg;
        vec3 diffuse = irradiance * albedo / PI;
        vec3 specular = prefiltered * (fresnel * brdf.x + brdf.y);
        ambient = (diffuse_weight * diffuse + specular) * occlusion *
                  lighting_constants.environment_ibl_params.z;
    }
    else
    {
        ambient = 0.02 * albedo * occlusion;
    }
    out_color = vec4(ambient + direct, 1.0);
}
