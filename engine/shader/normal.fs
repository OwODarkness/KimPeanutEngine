#version 460 core

struct Material{
    sampler2D diffuse_texture_0;
    sampler2D diffuse_texture_1;

    sampler2D specular_texture_0;
    sampler2D specular_texture_1;

    sampler2D emission_texture;

    sampler2D normal_texture;
    float shininess;
    vec3 diffuse_albedo;
};

uniform Material material;

in vec3 out_normal;
out vec2 out_texcoord;
in mat3 out_TBN;
out vec4 out_frag_color;
uniform bool normal_texture_enabled;

void main()
{
    vec3 normal = vec3(0., 0., 0.);

    normal = normalize(out_normal);
    out_frag_color = vec4(normal * 0.5 + 0.5, 1);

    if(normal_texture_enabled)
    {
        vec3 normal = texture(material.normal_texture, out_texcoord).rgb;
        normal = normalize(normal * 2.f - 1.f);
        normal = normalize(out_TBN * normal);
        out_frag_color = vec4(normal, 1.f);
    }

}