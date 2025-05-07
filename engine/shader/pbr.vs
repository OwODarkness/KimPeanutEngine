#version 460 core
layout(location = 0) in vec3 in_location;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec3 in_tangent;

layout(std140, binding = 0) uniform CameraMatrices{
    mat4 projection;
    mat4 view;
};

uniform mat4 model;

out vec2 texcoord;
out vec3 frag_position;
out vec3 normal;

void main()
{
    gl_Position = projection * view *  model * vec4(in_location, 1.0);
    frag_position = vec3(model * vec4(in_location, 1.f)); 
    normal =   mat3(transpose(inverse(model))) * in_normal;
}