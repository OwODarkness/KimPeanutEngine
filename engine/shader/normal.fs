#version 460 core

struct Material{
    sampler2D diffuse_map;
    sampler2D specular_map;
    sampler2D normal_map;

    vec3 albedo;

    float shininess;
    bool has_diffuse_map;
    bool has_specular_map;
    bool has_normal_map;
};


uniform Material material;
uniform int object_id;
in vec3 normal;
in vec2 texcoord;
in mat3 TBN;

out vec4 out_frag_color;

layout (location = 0) out vec3 out_normal;
layout (location = 1) out int out_object_id;

void main()
{
    vec3 normal_vec = material.has_normal_map == true ? 
    normalize(TBN * normalize(texture(material.normal_map, texcoord).rgb * 2.0 - 1.0)): normalize(normal);

    out_frag_color = vec4(normal_vec, 1.0);
    out_normal = normal_vec;
    out_object_id = object_id;

}