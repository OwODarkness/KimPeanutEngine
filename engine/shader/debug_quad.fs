#version 460 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D depthMap;
uniform float near_plane;
uniform float far_plane;

float LinearizeDepth(float depth)
{
    float z = depth * 2.f - 1.f;
    return (2.0 * near_plane * far_plane) / (far_plane + near_plane - z * (far_plane - near_plane));
}

void main()
{
    float depth_value = texture(depthMap, TexCoords).r;
    FragColor = vec4(vec3(depth_value), 1.f);
}