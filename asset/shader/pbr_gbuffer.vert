#version 450

// Deferred G-buffer vertex stage. The pipeline declares the canonical
// 5-attribute layout (position/normal/texcoord/tangent/bitangent) that
// data::Vertex carries; see render_resource_resolver.cpp. World-space TBN is
// the audited convention: transpose(inverse(mat3(model))) + mat3(T,B,N).

layout(binding = 0) uniform PerPassData{
	mat4 view;
	mat4 proj;
}pass_data;

layout(binding = 1) uniform PerObjectData{
	mat4 model;
}object_data;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec3 in_tangent;
layout(location = 4) in vec3 in_bitangent;

layout(location = 0) out vec2 frag_texcoord;
layout(location = 1) out vec3 frag_T;
layout(location = 2) out vec3 frag_B;
layout(location = 3) out vec3 frag_N;

void main() {
    mat3 normal_mat = transpose(inverse(mat3(object_data.model)));
    vec3 world_tangent = normal_mat * in_tangent;
    vec3 world_bitangent = normal_mat * in_bitangent;
    vec3 world_normal = normal_mat * in_normal;
    frag_T = dot(world_tangent, world_tangent) > 1e-8
                 ? normalize(world_tangent)
                 : vec3(0.0);
    frag_B = dot(world_bitangent, world_bitangent) > 1e-8
                 ? normalize(world_bitangent)
                 : vec3(0.0);
    frag_N = dot(world_normal, world_normal) > 1e-8
                 ? normalize(world_normal)
                 : vec3(0.0, 0.0, 1.0);
    frag_texcoord = in_texcoord;

    vec4 world_pos = object_data.model * vec4(in_position, 1.0);
    gl_Position = pass_data.proj * pass_data.view * world_pos;
#if KP_GRAPHICS_API_VULKAN
    // Shared camera matrices use [-W,+W] clip depth; Vulkan requires [0,+W].
    gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);
#endif
}
