#version 450

layout(binding = 0) uniform PerPassData{
	mat4 view;
	mat4 proj;
}pass_data;

layout(binding = 1) uniform PerObjectData{
	mat4 model;
}object_data;

layout(location = 0) in vec3 inPosition; // triangle vertex position
layout(location = 1) in vec2 inUV;    // 



layout(location = 0) out vec2 fragTexCoord;

void main() {
    gl_Position = pass_data.proj * pass_data.view * object_data.model * vec4(inPosition, 1.0);
    fragTexCoord = inUV;
}
