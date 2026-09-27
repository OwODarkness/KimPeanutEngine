#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload
{
    vec3 position;
    uint hit;
    vec3 normal;
    vec3 albedo;
    vec3 emissive;
    float metallic;
    float roughness;
    vec3 radiance;
};

layout(set = 0, binding = 2, std140) uniform CameraData
{
    mat4 inverse_view_projection;
    vec4 camera_position;
    vec4 light_center;
    vec4 light_u;
    vec4 light_v;
    vec4 light_radiance;
    uint rng_seed;
    uint sample_count;
    uint samples_per_dispatch;
    uint probe_mode;
    uvec4 scene_data;
} camera;

layout(set = 0, binding = 4) uniform sampler2D environment_texture;
layout(location = 0) rayPayloadInEXT HitPayload payload;

void main()
{
    const vec3 direction = normalize(gl_WorldRayDirectionEXT);
    const vec2 uv = vec2(atan(direction.z, direction.x) * 0.159154943 + 0.5,
                         asin(clamp(direction.y, -1.0, 1.0)) * 0.318309886 + 0.5);
    payload.hit = 0u;
    payload.radiance = texture(environment_texture, uv).rgb * camera.light_radiance.w;
}
