#version 460 core
layout(location = 0) in vec3 in_location;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec3 in_tangent;
layout(location = 4) in vec3 in_bitangent;

layout (std140, binding=0) uniform CameraMatrices{
    mat4 projection;
    mat4 view;
};
uniform mat4 model;
void main()
{
    gl_Position = projection * view * model * vec4(in_location, 1.f);
}