#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload
{
    vec3 position;
    uint hit;
    vec3 normal;
    uint geometry;
    vec3 albedo;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main()
{
    payload.hit = 0u;
}
