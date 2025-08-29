#version 460 core

in vec4 frag_position;

uniform vec3 light_position;
uniform float far_plane;


void main()
{
    float distance = length(frag_position.xyz - light_position);
    float norm_distance = distance / far_plane;
    gl_FragDepth = norm_distance;
}