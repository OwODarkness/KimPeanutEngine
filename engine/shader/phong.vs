//Blinn-Phong vertex shader
#version 460 core
layout(location = 0) in vec3 in_location;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec3 in_tangent;

layout (std140, binding=0) uniform CameraMatrices{
    mat4 projection;
    mat4 view;
};

out vec3 normal;
out vec2 texcoord;
out vec3 frag_position;
out vec4 frag_pos_light_space;
out mat3 TBN;

uniform mat4 model;
uniform mat4 light_space_matrix;

void main()
{
    gl_Position = projection* view * model* vec4(in_location, 1.f);
    frag_position = vec3(model * vec4(in_location, 1.f)); 
    normal =   mat3(transpose(inverse(model))) * in_normal;
    texcoord = in_texcoord;
    frag_pos_light_space = light_space_matrix * vec4(frag_position, 1);

    vec3 N = normalize(vec3(model * vec4(in_normal, 0.f)));    // Normal vector
    vec3 T = normalize(vec3(model * vec4(in_tangent, 0.f)));  // Tangent vector
    T = normalize(T - dot(T, N) * N);  // Orthogonalize T with respect to N
    vec3 B = cross(N, T);              // Bitangent vector
    TBN = mat3(T, B, N);               // TBN matrix
}