#version 460 core


in vec3 out_normal;

out vec4 out_frag_color;


void main()
{
    vec3 normal = normalize(out_normal);
    out_frag_color = vec4(normal * 0.5 + 0.5, 1);
}