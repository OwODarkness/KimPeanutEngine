#version 330 core
in vec3 world_pos;
out vec4 out_frag_color;

uniform sampler2D equiretangular_map;
const vec2 inv_atan = vec2(0.1591, 0.3183);

vec2 SampleShericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= inv_atan;
    uv += 0.5;
    return uv;
}

void main()
{
    vec2 uv = SampleShericalMap(normalize(world_pos));
    vec3 color = texture(equiretangular_map, uv).rgb;
    out_frag_color = vec4(color, 1.0);
}

