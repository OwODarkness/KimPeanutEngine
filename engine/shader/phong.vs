#version 330 core
layout(location = 0) in vec3 in_location;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;

out vec3 out_normal;
out vec2 out_texcoord;
out vec3 frag_position;
out vec4 frag_pos_light_space;
layout (std140) uniform Matrices{
    mat4 projection;
    mat4 view;
};

uniform mat4 model;
// uniform mat4 view;
uniform mat4 light_space_matrix;

void main()
{
    gl_Position = projection* view*model* vec4(in_location, 1.f);
    frag_position = vec3(model * vec4(in_location, 1.f)); 
    out_normal =   in_normal;
    out_texcoord = in_texcoord;
    frag_pos_light_space = light_space_matrix * vec4(frag_position, 1);
}