#version 330 core

struct Material{
    sampler2D diffuse_texture_0;
    sampler2D diffuse_texture_1;

    sampler2D specular_texture_0;
    sampler2D specular_texture_1;

    sampler2D emission_texture;
    float shininess;
};

struct PointLight{
    vec3 position;
    vec3 color;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    float constant;
    float linear;
    float quadratic;
};

struct DirectionalLight{
    vec3 direction;
    vec3 color;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

struct SpotLight{
    vec3 position;
    vec3 direction;
    float cutoff;
    float outer_cutoff;
    vec3 color;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    float constant;
    float linear;
    float quadratic;
};

in vec3 out_normal;
in vec2 out_texcoord;
in vec3 frag_position;

out vec4 out_frag_color;

uniform PointLight point_light;
uniform DirectionalLight directional_light;
uniform SpotLight spot_light;
uniform vec3 ambient;
uniform Material material;
uniform vec3 view_position;


vec3 CalculatePointRender(PointLight light)
{
    vec3 light_direction = normalize(light.position - frag_position);
    vec3 light_color = light.color;
    vec3 normal = normalize(out_normal);

    vec3 ambient_render = light.ambient * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color;

    float diff = max(dot(normal, light_direction), 0);
    vec3 diffuse_render = light.diffuse * diff  * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color;

    vec3 view_direction = normalize(view_position - frag_position);
    vec3 reflect_direction = reflect(-light_direction, normal);

    float spec = pow(max(dot(view_direction, reflect_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * vec3(texture(material.specular_texture_0, out_texcoord)) * light_color ;

    float distance    = length(light.position - frag_position);
    float attenuation = 1.0 / (light.constant + light.linear * distance + 
                light.quadratic * (distance * distance));
    return attenuation * vec3(ambient_render+ diffuse_render + specular_render);   
}

vec3 CalculateDirectionalLightRender(DirectionalLight light)
{
    vec3 light_direction = normalize(-light.direction);
    vec3 light_color = light.color;
    vec3 normal = normalize(out_normal);

    vec3 ambient_render = light.ambient * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color ;

    float diff = max(dot(normal, light_direction), 0);
    vec3 diffuse_render = light.diffuse * diff * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color;

    vec3 view_direction = normalize(view_position - frag_position);
    vec3 reflect_direction = reflect(-light_direction, normal);

    float spec = pow(max(dot(view_direction, reflect_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * vec3(texture(material.specular_texture_0, out_texcoord)) * light_color ;
    return vec3(ambient_render + diffuse_render + specular_render);  
}

vec3 CalculateSpotLightRender(SpotLight light)
{
    vec3 light_direction = normalize(light.position - frag_position);
    float theta = dot(light_direction, -light.direction);
    float epsilon   = light.cutoff - light.outer_cutoff;
    float intensity = clamp((theta - light.outer_cutoff) / epsilon, 0.0, 1.0);

    vec3 light_color = light.color;
    vec3 normal = normalize(out_normal);

    vec3 ambient_render = light.ambient * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color ;

    float diff = max(dot(normal, light_direction), 0);
    vec3 diffuse_render = light.diffuse * diff * vec3(texture(material.diffuse_texture_0, out_texcoord)) * light_color;

    vec3 view_direction = normalize(view_position - frag_position);
    vec3 reflect_direction = reflect(-light_direction, normal);

    float spec = pow(max(dot(view_direction, reflect_direction), 0.f), material.shininess);
    vec3 specular_render = light.specular * spec  * vec3(texture(material.specular_texture_0, out_texcoord)) * light_color ;
    
    float distance    = length(light.position - frag_position);
    float attenuation = 1.0 / (light.constant + light.linear * distance + 
                light.quadratic * (distance * distance));
        
        return  vec3(attenuation *ambient_render + intensity * attenuation *diffuse_render + intensity * attenuation *specular_render);  

}

void main()
{
    vec3 point_light_res = CalculatePointRender(point_light);
    vec3 directional_light_res = CalculateDirectionalLightRender(directional_light);
    vec3 spot_light_res = CalculateSpotLightRender(spot_light);
    out_frag_color = vec4(point_light_res + directional_light_res + spot_light_res, 1 );

    // if(gl_FragCoord.x < 600)
    //     out_frag_color = vec4(ambient_res + diffuse_res + specular_res, 1 );
    // else
    // {
    //     //float dist = distance(frag_position, view_position);
    //     //vec3 deep_color = vec3( dist /5.f );
    //     //out_frag_color = vec4(deep_color, 1);
    //     out_frag_color = vec4(normal * 0.5 + 0.5, 1);

    // }
    
}