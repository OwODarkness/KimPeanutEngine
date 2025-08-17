#version 460 core

in vec3 world_pos;
out vec4 out_frag_color;

uniform samplerCube environment_map;
const float PI = 3.14159265359;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

vec3 CalculateIrradianceRiemann(vec3 normal, float sample_delta)
{
     vec3 irradiance = vec3(0.0);

    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    int sample_count = 0;
    for(float phi = 0.0 ; phi < 2.0 * PI ; phi += sample_delta)
    {
        for(float theta = 0.0 ; theta < 0.5 * PI ; theta += sample_delta)
        {
            // spherical to cartesian (in tangent space)
            vec3 tangent_sample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            vec3 sample_vec = tangent_sample.x * right + tangent_sample.y * up + tangent_sample.z * normal; 

            irradiance += texture(environment_map, sample_vec).rgb * cos(theta) * sin(theta);
            sample_count++;
        }
    }

    return PI * irradiance * (1.0 / float(sample_count));
}

vec3 SampleHemisphereCosine(float u1, float u2)
{
    float r = sqrt(u1);
    float theta = 2.0 * PI * u2;

    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - u1)); // cosine-weighted

    return vec3(x, y, z);
}

// Build orthonormal basis from normal
void BuildOrthonormalBasis(in vec3 n, out vec3 t, out vec3 b)
{
    if (abs(n.z) < 0.999)
        t = normalize(cross(vec3(0.0, 0.0, 1.0), n));
    else
        t = normalize(cross(vec3(0.0, 1.0, 0.0), n));
    b = cross(n, t);
}

vec3 CalculateIrradianceMonteCarlo(vec3 normal, int sample_num)
{
    vec3 irradiance = vec3(0.0);

    vec3 tangent, bitangent;
    BuildOrthonormalBasis(normal, tangent, bitangent);

    for(int i = 0; i < sample_num; i++)
    {
        vec2 rand_uv = Hammersley(i, sample_num);
        float u1 = rand_uv.x;
        float u2 = rand_uv.y; 
        vec3 local_dir = SampleHemisphereCosine(u1, u2);
        vec3 world_dir = local_dir.x * tangent +
                         local_dir.y * bitangent +
                         local_dir.z * normal;

        irradiance += texture(environment_map, world_dir).rgb ; // ← compensate PDF
    }

    return irradiance / float(sample_num);
}

void main()
{
    vec3 normal = normalize(world_pos);
    //vec3 irradiance = CalculateIrradianceRiemann(normal, 0.025);
    vec3 irradiance = CalculateIrradianceMonteCarlo(normal, 80000);
    out_frag_color = vec4(irradiance, 1.0);

}