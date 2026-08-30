#version 450
// Fullscreen triangle for the G-buffer debug composite. Both backends preserve
// this common CCW clip-space winding, so ordinary back-face culling is valid.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec2 TexCoord;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
