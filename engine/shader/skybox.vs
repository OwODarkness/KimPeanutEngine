#version 460 core
layout (location = 0) in vec3 in_location;

out vec3 texcoord;

layout (std140) uniform Matrices{
    mat4 projection;
    mat4 view;
};

void main()
{
    mat3 view_mat3 = mat3(view);
    mat4 view_trans = mat4(
        vec4(view_mat3[0], 0.f),
        vec4(view_mat3[1], 0.f),
        vec4(view_mat3[2], 0.f),
        vec4(0.f, 0.f, 0.f, 1.f)
    );
    texcoord = in_location;
    vec4 pos = projection * view_trans * vec4(in_location, 1.f);
    gl_Position = pos.xyww;
}