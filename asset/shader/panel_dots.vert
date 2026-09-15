#version 450

layout(location = 0) in vec2 inPosition;

layout(location = 0) out vec2 fragUV;

void main()
{
    // The quad is authored directly in clip space, so the texture coordinate is
    // derived instead of carried in a second vertex stream.
    //
    // V is flipped because the dot mask's first row is its top: the panel's dot
    // row 0 must land at the top of the output image.
    fragUV = vec2(inPosition.x * 0.5 + 0.5, 0.5 - inPosition.y * 0.5);
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
