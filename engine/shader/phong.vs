#version 330 core
layout(location = 0) in vec3 in_location;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;

out vec3 out_normal;
out vec2 out_texcoord;
out vec3 frag_position;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection* view*model* vec4(in_location, 1.f);
    frag_position = vec3(model * vec4(in_location, 1.f)); 
    out_normal = mat3(transpose(inverse(model))) * in_normal;
    out_texcoord = in_texcoord;
}