#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

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
    float cone_width;
    float cone_spread;
};

struct GeometryData
{
    uvec4 address_words;
    uvec4 attributes;
};

struct InstanceData
{
    uvec4 geometry_material_count;
};

struct MaterialData
{
    vec4 base_color;
    vec4 emissive;
    vec4 surface;
    uvec4 texture_indices;
};

struct LightData
{
    vec4 position_or_type;
    vec4 direction_and_range;
    vec4 color_intensity;
    vec4 parameters;
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

layout(set = 0, binding = 3, std140) uniform SceneData
{
    GeometryData geometries[512];
    InstanceData instances[512];
    MaterialData materials[512];
    LightData lights[128];
} scene;

layout(set = 0, binding = 4) uniform sampler2D environment_texture;
layout(set = 1, binding = 0) uniform sampler2D kp_textures[];

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RawBuffer
{
    uint values[];
};

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 barycentrics;

uint64_t JoinAddress(uvec2 words)
{
    return uint64_t(words.x) | (uint64_t(words.y) << 32u);
}

uint LoadIndex(GeometryData geometry, uint index)
{
    RawBuffer indices = RawBuffer(JoinAddress(geometry.address_words.zw));
    return indices.values[index];
}

vec3 LoadVector3(GeometryData geometry, uint vertex_index, uint byte_offset)
{
    RawBuffer vertices = RawBuffer(JoinAddress(geometry.address_words.xy));
    const uint word = vertex_index * (geometry.attributes.x / 4u) + byte_offset / 4u;
    return vec3(uintBitsToFloat(vertices.values[word]),
                uintBitsToFloat(vertices.values[word + 1u]),
                uintBitsToFloat(vertices.values[word + 2u]));
}

vec2 LoadUv(GeometryData geometry, uint vertex_index)
{
    RawBuffer vertices = RawBuffer(JoinAddress(geometry.address_words.xy));
    const uint word = vertex_index * (geometry.attributes.x / 4u) +
                      geometry.attributes.z / 4u;
    return vec2(uintBitsToFloat(vertices.values[word]),
                uintBitsToFloat(vertices.values[word + 1u]));
}

void main()
{
    const uint instance_index = gl_InstanceCustomIndexEXT;
    if (instance_index >= camera.scene_data.y)
    {
        payload.hit = 0u;
        return;
    }
    const InstanceData instance = scene.instances[instance_index];
    if (gl_GeometryIndexEXT >= instance.geometry_material_count.z)
    {
        payload.hit = 0u;
        return;
    }
    const uint geometry_index = instance.geometry_material_count.x + gl_GeometryIndexEXT;
    const uint material_index = instance.geometry_material_count.y + gl_GeometryIndexEXT;
    if (geometry_index >= camera.scene_data.x || material_index >= camera.scene_data.z)
    {
        payload.hit = 0u;
        return;
    }

    const GeometryData geometry = scene.geometries[geometry_index];
    const MaterialData material = scene.materials[material_index];
    const uint index_base = gl_PrimitiveID * 3u;
    const uint i0 = LoadIndex(geometry, index_base);
    const uint i1 = LoadIndex(geometry, index_base + 1u);
    const uint i2 = LoadIndex(geometry, index_base + 2u);
    const float weight0 = 1.0 - barycentrics.x - barycentrics.y;
    const vec2 uv = LoadUv(geometry, i0) * weight0 +
                    LoadUv(geometry, i1) * barycentrics.x +
                    LoadUv(geometry, i2) * barycentrics.y;
    const float world_cone_width = payload.cone_width + gl_HitTEXT * payload.cone_spread;
    payload.cone_width = world_cone_width;
    float cone_world_texel_density = 0.0;
    if (camera.probe_mode == 7u)
    {
        const vec3 p0 = gl_ObjectToWorldEXT * vec4(LoadVector3(geometry, i0, 0u), 1.0);
        const vec3 p1 = gl_ObjectToWorldEXT * vec4(LoadVector3(geometry, i1, 0u), 1.0);
        const vec3 p2 = gl_ObjectToWorldEXT * vec4(LoadVector3(geometry, i2, 0u), 1.0);
        const float world_area = 0.5 * length(cross(p1 - p0, p2 - p0));
        const float uv_area = 0.5 * abs((LoadUv(geometry, i1).x - LoadUv(geometry, i0).x) *
                                        (LoadUv(geometry, i2).y - LoadUv(geometry, i0).y) -
                                        (LoadUv(geometry, i1).y - LoadUv(geometry, i0).y) *
                                        (LoadUv(geometry, i2).x - LoadUv(geometry, i0).x));
        cone_world_texel_density = sqrt(uv_area / max(world_area, 1e-12));
    }
    const vec3 object_normal = normalize(
        LoadVector3(geometry, i0, geometry.attributes.w) * weight0 +
        LoadVector3(geometry, i1, geometry.attributes.w) * barycentrics.x +
        LoadVector3(geometry, i2, geometry.attributes.w) * barycentrics.y);
    payload.position = gl_WorldRayOriginEXT +
                       gl_HitTEXT * gl_WorldRayDirectionEXT;
    payload.normal = normalize(transpose(mat3(gl_WorldToObjectEXT)) * object_normal);
    if (dot(payload.normal, -gl_WorldRayDirectionEXT) < 0.0)
        payload.normal = -payload.normal;

    vec3 base_color = material.base_color.rgb;
    const uint texture_index = material.texture_indices.x;
    if (texture_index != 0xffffffffu)
    {
        if (camera.probe_mode == 7u)
        {
            const ivec2 dimensions = textureSize(kp_textures[nonuniformEXT(texture_index)], 0);
            const float texel_density = cone_world_texel_density *
                sqrt(float(dimensions.x) * float(dimensions.y));
            const float lod = clamp(log2(max(world_cone_width * texel_density, 1.0)),
                                    0.0, float(textureQueryLevels(
                                        kp_textures[nonuniformEXT(texture_index)]) - 1));
            base_color *= textureLod(kp_textures[nonuniformEXT(texture_index)], uv, lod).rgb;
        }
        else
            base_color *= texture(kp_textures[nonuniformEXT(texture_index)], uv).rgb;
    }
    payload.albedo = base_color;
    payload.emissive = material.emissive.rgb;
    const uint channels = uint(material.surface.w + 0.5);
    float metallic = material.surface.x;
    float roughness = material.surface.y;
    const uint metallic_texture = material.texture_indices.y;
    const uint roughness_texture = material.texture_indices.z;
    if (metallic_texture != 0xffffffffu && metallic_texture == roughness_texture)
    {
        const vec4 packed_surface = textureLod(
            kp_textures[nonuniformEXT(metallic_texture)], uv, 0.0);
        metallic *= packed_surface[channels % 4u];
        roughness *= packed_surface[channels / 4u];
    }
    else
    {
        if (metallic_texture != 0xffffffffu)
            metallic *= textureLod(kp_textures[nonuniformEXT(metallic_texture)],
                                   uv, 0.0)[channels % 4u];
        if (roughness_texture != 0xffffffffu)
            roughness *= textureLod(kp_textures[nonuniformEXT(roughness_texture)],
                                    uv, 0.0)[channels / 4u];
    }
    payload.metallic = clamp(metallic, 0.0, 1.0);
    payload.roughness = clamp(roughness, 0.04, 1.0);
    payload.radiance = vec3(0.0);
    payload.hit = 1u;
}
