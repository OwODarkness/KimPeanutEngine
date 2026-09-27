#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT uint visibility_payload;

void main()
{
    visibility_payload = 0u;
}
