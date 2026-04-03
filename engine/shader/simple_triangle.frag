#version 450

layout(location = 0) in vec2 fragTexCoord;  // input from vertex shader
layout(location = 0) out vec4 outColor;  // final output color

layout(binding = 2) uniform sampler2D texSampler;

void main()
{
    outColor = texture(texSampler, fragTexCoord);

}
