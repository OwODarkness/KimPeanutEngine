#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 3, std430) readonly buffer VertexData { uint values[]; } vertices;
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
    uvec2 padding;
    uvec4 geometry_index_starts[4];
} camera;
layout(set = 0, binding = 19, std430) readonly buffer IndexData0 { uint values[]; } index_data_0;
layout(set = 0, binding = 20, std430) readonly buffer IndexData1 { uint values[]; } index_data_1;
layout(set = 0, binding = 21, std430) readonly buffer IndexData2 { uint values[]; } index_data_2;
layout(set = 0, binding = 22, std430) readonly buffer IndexData3 { uint values[]; } index_data_3;
layout(set = 0, binding = 23, std430) readonly buffer IndexData4 { uint values[]; } index_data_4;
layout(set = 0, binding = 24, std430) readonly buffer IndexData5 { uint values[]; } index_data_5;
layout(set = 0, binding = 25, std430) readonly buffer IndexData6 { uint values[]; } index_data_6;
layout(set = 0, binding = 26, std430) readonly buffer IndexData7 { uint values[]; } index_data_7;
layout(set = 0, binding = 27, std430) readonly buffer IndexData8 { uint values[]; } index_data_8;
layout(set = 0, binding = 28, std430) readonly buffer IndexData9 { uint values[]; } index_data_9;
layout(set = 0, binding = 29, std430) readonly buffer IndexData10 { uint values[]; } index_data_10;
layout(set = 0, binding = 30, std430) readonly buffer IndexData11 { uint values[]; } index_data_11;
layout(set = 0, binding = 31, std430) readonly buffer IndexData12 { uint values[]; } index_data_12;
layout(set = 0, binding = 32, std430) readonly buffer IndexData13 { uint values[]; } index_data_13;
layout(set = 0, binding = 33, std430) readonly buffer IndexData14 { uint values[]; } index_data_14;
layout(set = 0, binding = 34, std430) readonly buffer IndexData15 { uint values[]; } index_data_15;

struct HitPayload
{
    vec3 position;
    uint hit;
    vec3 normal;
    uint geometry;
    vec3 albedo;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 barycentrics;

uint LoadIndex(uint geometry, uint index)
{
    switch (geometry)
    {
    case 1u: return index_data_1.values[index];
    case 2u: return index_data_2.values[index];
    case 3u: return index_data_3.values[index];
    case 4u: return index_data_4.values[index];
    case 5u: return index_data_5.values[index];
    case 6u: return index_data_6.values[index];
    case 7u: return index_data_7.values[index];
    case 8u: return index_data_8.values[index];
    case 9u: return index_data_9.values[index];
    case 10u: return index_data_10.values[index];
    case 11u: return index_data_11.values[index];
    case 12u: return index_data_12.values[index];
    case 13u: return index_data_13.values[index];
    case 14u: return index_data_14.values[index];
    case 15u: return index_data_15.values[index];
    default: return index_data_0.values[index];
    }
}

vec3 LoadPosition(uint vertex_index)
{
    const uint word = vertex_index * 14u;
    return vec3(uintBitsToFloat(vertices.values[word]),
                uintBitsToFloat(vertices.values[word + 1u]),
                uintBitsToFloat(vertices.values[word + 2u]));
}

void main()
{
    const uint geometry = gl_GeometryIndexEXT;
    const uint index_base = camera.geometry_index_starts[geometry / 4u][geometry % 4u] +
                            gl_PrimitiveID * 3u;
    const vec3 p0 = LoadPosition(LoadIndex(geometry, index_base));
    const vec3 p1 = LoadPosition(LoadIndex(geometry, index_base + 1u));
    const vec3 p2 = LoadPosition(LoadIndex(geometry, index_base + 2u));
    const vec3 world_p0 = gl_ObjectToWorldEXT * vec4(p0, 1.0);
    const vec3 world_p1 = gl_ObjectToWorldEXT * vec4(p1, 1.0);
    const vec3 world_p2 = gl_ObjectToWorldEXT * vec4(p2, 1.0);
    const float weight0 = 1.0 - barycentrics.x - barycentrics.y;
    payload.position = world_p0 * weight0 + world_p1 * barycentrics.x +
                       world_p2 * barycentrics.y;
    payload.normal = normalize(cross(world_p1 - world_p0, world_p2 - world_p0));
    if (dot(payload.normal, -gl_WorldRayDirectionEXT) < 0.0)
        payload.normal = -payload.normal;
    payload.geometry = geometry;
    payload.albedo = geometry == 4u ? vec3(0.445, 0.0, 0.0)
                   : geometry == 5u ? vec3(0.0, 0.32, 0.0)
                                    : vec3(0.80, 0.659, 0.44);
    payload.hit = 1u;
}
