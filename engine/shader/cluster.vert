#version 460 core
layout(location = 0) in vec3 in_location;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in mat4 in_instance_matrix;

out vec3 out_normal;
out vec2 out_texcoord;
out vec3 frag_position;

layout (std140) uniform Matrices{
    mat4 projection;
    mat4 view;
};

//uniform mat4 model;
// uniform mat4 view;


void main()
{
    // mat4 scale_transformation = mat4(
    //     vec4(in_scale.x, 0.f, 0.f, 0.f),
    //     vec4(0.f, in_scale.y, 0.f,0.f),
    //     vec4(0.f, 0.f, in_scale.z,0.f),
    //     vec4(0.f, 0.f, 0.f, 1.f)
    // );
    // mat4 rotation_transformation_roll = mat4(
    //     vec4(1.f, 0.f, 0.f, 0.f),
    //     vec4(0.f, cos(in_rotation.x), -sin(in_rotation.x),0.f),
    //     vec4(0.f, sin(in_rotation.x), cos(in_rotation.x), 0.f),
    //     vec4(0.f, 0.f, 0.f, 1.f)
    // );

    // mat4 rotation_transformation_pitch = mat4(
    //     vec4(cos(in_rotation.y), 0.f, -sin(in_rotation.y), 0.f),
    //     vec4(0.f, 1.f, 0.f, 0.f),
    //     vec4(sin(in_rotation.y), 0.f, cos(in_rotation.y), 0.f),
    //     vec4(0.f, 0.f, 0.f, 1.f)
    // );

    // mat4 rotation_transformation_yaw = mat4(
    //     vec4(cos(in_rotation.z), -sin(in_rotation.z), 0.f, 0.f),
    //     vec4(sin(in_rotation.z), cos(in_rotation.z), 0.f, 0.f),
    //     vec4(0.f, 0.f, 1.f, 0.f),
    //     vec4(0.f, 0.f, 0.f, 1.f)
    // );
    // mat4 rotation_transformation = rotation_transformation_yaw * rotation_transformation_pitch * rotation_transformation_roll;
    
    // mat4 translation_transformation = mat4(
    //     vec4(0.f, 0.f, 0.f, in_translation.x),
    //     vec4(0.f, 0.f, 0.f, in_translation.y),
    //     vec4(0.f, 0.f, 0.f, in_translation.z),
    //     vec4(0.f, 0.f, 0.f, 1.f)
    // );
    //mat4 model = translation_transformation * scale_transformation * rotation_transformation;
    gl_Position = projection* view* in_instance_matrix* vec4(in_location, 1.f);
    frag_position = vec3(in_instance_matrix * vec4(in_location, 1.f)); 
    out_normal =  in_normal;
    out_texcoord = in_texcoord;
}