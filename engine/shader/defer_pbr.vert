#version 450

#define IN_LOCATION(n) layout(location = n) in
#define OUT_LOCATION(n) layout(location = n) out

IN_LOCATION(0)  vec2 in_position;
OUT_LOCATION(0) vec2 out_uv; 

void main()
{
    // Convert from NDC [-1, 1] to UV [0, 1]
    out_uv = in_position * 0.5 + 0.5;
    gl_Position = vec4(in_position.xy, 0.0, 1.0);
}