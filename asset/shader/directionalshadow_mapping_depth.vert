#version 450

layout(binding = 0) uniform PerPassData {
    mat4 view;
    mat4 proj;
} pass_data;

layout(binding = 1) uniform PerObjectData {
    mat4 model;
} object_data;

layout(location = 0) in vec3 position;

void main()
{
    gl_Position = pass_data.proj * pass_data.view * object_data.model * vec4(position, 1.0);
#if KP_GRAPHICS_API_VULKAN
    // Engine projection matrices use the OpenGL-style [-W,+W] depth range.
    // Vulkan clips to [0,+W], so translate depth without changing shared math.
    gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);
#endif
}
