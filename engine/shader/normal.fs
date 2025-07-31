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
    gObjectID = object_id;


}