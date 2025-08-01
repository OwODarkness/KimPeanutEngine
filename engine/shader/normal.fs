#version 460 core

struct Material{

    bool has_albedo_map;
    bool has_roughness_map;
    bool has_metallic_map;
    bool has_normal_map;
    bool has_ao_map;
    float metallic;
    float roughness;
    float ao;
    vec3 albedo;

    sampler2D albedo_map;
    sampler2D roughness_map;
    sampler2D metallic_map;
    sampler2D normal_map;
    sampler2D ao_map;
};

uniform Material material;
uniform bool is_outline_visible;
uniform int object_id;

in vec3 frag_pos;
in vec3 normal;
in vec2 texcoord;
in mat3 TBN;


layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec3 gAlbedo;
layout (location = 3) out vec3 gMaterial; 
layout (location = 4) out int gObjectID;
layout (location = 5) out vec3 gObjectVisual;

vec3 IDToColor(int id) {
    // Use a pseudo-random hash based on the ID
    uint uid = uint(id);
    uid = (uid ^ 61u) ^ (uid >> 16);
    uid *= 9u;
    uid = uid ^ (uid >> 4);
    uid *= 0x27d4eb2du;
    uid = uid ^ (uid >> 15);

    float r = float((uid & 0xFFu)) / 255.0;
    float g = float((uid >> 8) & 0xFFu) / 255.0;
    float b = float((uid >> 16) & 0xFFu) / 255.0;

    return vec3(r, g, b);
}

void main()
{
    vec3 normal_vec = material.has_normal_map == true ? 
    normalize(TBN * normalize(texture(material.normal_map, texcoord).rgb * 2.0 - 1.0)): 
    normalize(normal);

    vec3 albedo_vec = material.has_albedo_map ? 
    pow(texture(material.albedo_map, texcoord).rgb, vec3(2.2)) : 
    material.albedo;

    float roughness = material.has_roughness_map ? texture(material.roughness_map, texcoord).r : material.roughness;
    float metallic = material.has_metallic_map ? texture(material.metallic_map, texcoord).r : material.metallic;
    float ao = material.has_ao_map ? texture(material.ao_map, texcoord).r : material.ao;

    gPosition = frag_pos;
    gNormal = normal_vec;
    gAlbedo = albedo_vec;
    gMaterial = vec3(roughness, metallic, ao);
    if(is_outline_visible)
    {
    gObjectID = object_id;

    gObjectVisual = IDToColor(object_id);
    }

}