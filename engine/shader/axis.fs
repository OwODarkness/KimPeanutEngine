#version 460 core
out vec4 FragColor;

flat in vec3 axis_id;  // <---- Add `flat` to avoid interpolation

uniform int selected_axis;

void main() {
    vec3 base_color = axis_id;

    bool is_selected_axis = (
        (selected_axis == 0 && axis_id.x == 1.0) ||
        (selected_axis == 1 && axis_id.y == 1.0) ||
        (selected_axis == 2 && axis_id.z == 1.0)
    );

    if(is_selected_axis)
        FragColor = vec4(1.0, 1.0, 0.0, 1.0); // yellow highlight
    else
        FragColor = vec4(base_color, 1.0);
}
