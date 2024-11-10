#version 330 core

struct Material{
    sampler2D diffuse_texture_0;
    sampler2D diffuse_texture_1;

    sampler2D specular_texture_0;
    sampler2D specular_texture_1;

    float diffuse;
    float specular;
};

struct PointLight{
    vec3 position;
    vec3 color;
};

in vec3 out_normal;
in vec2 out_texcoord;
in vec3 frag_position;

out vec4 out_frag_color;

//uniform sampler2D texture1;
uniform PointLight point_light;
uniform vec3 ambient;
uniform Material material;
uniform vec3 view_position;

void main()
{
    vec3 light_dir = normalize(point_light.position - frag_position);
    vec3 normal = normalize(out_normal);

    out_frag_color = vec4(normal * 0.5 + 0.5, 1);
}