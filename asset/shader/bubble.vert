#version 450

layout(std140, binding = 0) uniform BubbleDrawData
{
    mat4 placement;
    vec4 quad_size;
    vec4 body;
    vec4 text;
    vec4 shape;
    vec4 fill_color;
    vec4 outline_color;
    vec4 dot_color;
    vec4 tail[8];
    vec4 ink_uv;
    vec4 grid;
    vec4 bezel_color;
} draw_data;

layout(location = 0) in vec2 inPosition;

layout(location = 0) out vec2 fragUV;

void main()
{
    // Derived from the untransformed position, before the placement is applied:
    // moving the bubble must not move the text within it. V is flipped because
    // the dot mask's first row is its top.
    fragUV = vec2(inPosition.x * 0.5 + 0.5, 0.5 - inPosition.y * 0.5);
    gl_Position = draw_data.placement * vec4(inPosition, 0.0, 1.0);
}
