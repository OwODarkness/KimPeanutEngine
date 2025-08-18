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

out vec3 frag_pos;
out vec3 normal;
out vec2 texcoord;
out mat3 TBN;
out vec3 ndc_coord;
void main()
{
    vec4 world_pos = model* vec4(in_location, 1.0);
    
    frag_pos = world_pos.xyz;

    mat3 normal_mat = transpose(inverse(mat3(model)));
    
    normal =  normalize(normal_mat * in_normal);
    texcoord = in_texcoord;
    
    vec3 N = normal;    // Normal vector
    vec3 T = normalize(normal_mat * in_tangent);  // Tangent vector
    vec3 B = normalize(normal_mat * in_bitangent);              // Bitangent vector
    TBN = mat3(T, B, N);               // TBN matrix
    gl_Position = projection * view * world_pos;
    vec4 clip_coord = gl_Position;
    ndc_coord = clip_coord.xyz / clip_coord.w;
}