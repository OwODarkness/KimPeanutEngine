#version 460 core

layout(location = 0) in vec3 in_location;
layout(location = 1) in vec3 in_normal;

layout (std140, binding=0) uniform CameraMatrices{
    mat4 projection;
    mat4 view;
};
uniform mat4 model;

out vec3 out_normal;

void main()
{
    gl_Position = projection* view*model* vec4(in_location, 1.f);
    out_normal =   mat3(transpose(inverse(model))) * in_normal;

}