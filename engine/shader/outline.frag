#version 460 core

uniform sampler2D object_id;
uniform vec2 texel_size; // 1.0 / screen resolution
uniform vec3 border_color;

in vec2 TexCoord;
out vec4 FragColor;

void main() {
    // Read object ID of center pixel (rounded to integer)
    int center = int(texture(object_id, TexCoord).r * 255.0 + 0.5); // assuming 8-bit storage in RED channel

    bool edge = false;

    // Iterate 3x3 neighborhood
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            if(x == 0 && y == 0) continue;

            vec2 offset = vec2(x, y) * texel_size;
            int neighbor = int(texture(object_id, TexCoord + offset).r * 255.0 + 0.5);

            if(neighbor != center) {
                edge = true;
            }
        }
    }

    FragColor = edge ? vec4(border_color, 1.0) : vec4(0.0);
}
