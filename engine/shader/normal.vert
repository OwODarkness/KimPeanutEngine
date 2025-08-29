#version 450

#define IN_LOCATION(n) layout(location = n) in
#define OUT_LOCATION(n) layout(location = n) out

IN_LOCATION(0) vec3 in_position;
IN_LOCATION(1) vec3 in_normal;
IN_LOCATION(2) vec2 in_texcoord;
IN_LOCATION(3) vec3 in_tangent;
IN_LOCATION(4) vec3 in_bitangent;

OUT_LOCATION(0) vec3 out_frag_pos;
OUT_LOCATION(2) vec2 out_texcoord;
OUT_LOCATION(3) vec3 out_T;
OUT_LOCATION(4) vec3 out_B;
OUT_LOCATION(5) vec3 out_N;
OUT_LOCATION(6) vec3 out_ndc_coord;

layout (std140, binding=0) uniform CameraMatrices{
    mat4 projection;
    mat4 view;
};


uniform mat4 model;

void main()
{
    vec4 world_pos = model* vec4(in_position, 1.0);
    
    out_frag_pos = world_pos.xyz;
    out_texcoord = in_texcoord;
    
    mat3 normal_mat = transpose(inverse(mat3(model)));
    out_T = normalize(normal_mat * in_tangent);  // Tangent vector
    out_B = normalize(normal_mat * in_bitangent);              // Bitangent vector
    out_N =  normalize(normal_mat * in_normal);

    vec4 clip_coord = projection * view * world_pos;
    out_ndc_coord = clip_coord.xyz / clip_coord.w;

    gl_Position = clip_coord;
}