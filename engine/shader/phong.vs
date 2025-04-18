#version 460 core
layout(location = 0) in vec3 in_location;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec3 in_tangent;
out vec3 out_normal;
out vec2 out_texcoord;
out vec3 frag_position;
out vec4 frag_pos_light_space;
out mat3 out_TBN;
layout (std140, binding=0) uniform CameraMatrices{
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
    out_normal =   mat3(transpose(inverse(model))) * in_normal;
    out_texcoord = in_texcoord;
    frag_pos_light_space = light_space_matrix * vec4(frag_position, 1);

    vec3 T = normalize(vec3(model * vec4(in_normal, 0.f)));
    vec3 N = normalize(vec3(model * vec4(in_tangent, 0.f)));
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(T, N);
    out_TBN = mat3(T, B, N);
}