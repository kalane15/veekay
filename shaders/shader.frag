#version 450

layout (location = 0) in vec3 f_position;
layout (location = 1) in vec3 f_normal;
layout (location = 2) in vec2 f_uv;

layout (location = 0) out vec4 final_color;

layout (binding = 0, std140) uniform SceneUniforms {
    mat4 view_projection;
    vec3 view_position;
    vec3 ambient_light_intensity;
    vec3 sun_light_direction;
    vec3 sun_light_color;
    uint point_light_count;
};

layout (binding = 1, std140) uniform ModelUniforms {
    mat4 model;
    vec3 albedo_color;
    vec3 specular_color;
    float shininess;
};

struct PointLight {
    vec3 position;
    float radius;
    vec3 color;
};

layout(binding = 2, std430) readonly buffer PointLights {
	PointLight point_lights[];
};


void main() {
    vec3 normal = normalize(f_normal);
    vec3 view_dir = normalize(view_position - f_position);

    //Sun diffuse
    vec3 sun_light_dir = normalize(sun_light_direction);
    float sun_diffuse = max(0.0f, dot(normal, sun_light_dir));
    vec3 sun_diffuse_intensity = albedo_color;


    //Sun specular
    vec3 half_vector = normalize(view_dir + sun_light_direction);
    vec3 sun_specular = specular_color *
                        pow(max(0.0f, dot(normal, half_vector)),
                            shininess);

    vec3 color = ambient_light_intensity + (sun_diffuse_intensity + sun_specular) * sun_light_color * sun_diffuse;

    if (point_light_count == 1488){
        color = vec3(1.0f, 1.0f, 1.0f);
    }
    final_color = vec4(color, 1.0f);

    for (uint i = 0; i < point_light_count; ++i) {
        PointLight light = point_lights[i];
        vec3 normal = normalize(f_normal);
        //color = vec3(1.0f, 1.0f, 1.0f);
        //continue;
        vec3 light_position = light.position;
        float light_radius = light.radius;
        vec3 light_dir = light_position - f_position;
        float distance = length(light_dir);
        light_dir = normalize(light_dir);

        // Ослабление света по квадрату расстояния (инвертируемое)
        float attenuation = light_radius / (distance * distance);

        // Рассеянное освещение (Lambertian diffuse)
        float light_diffuse = max(0.0f, dot(normal, light_dir));
        vec3 light_diffuse_color = light.color * light_diffuse;

        // Спекулярное освещение (Blinn-Phong model)
        vec3 view_dir = normalize(view_position - f_position);
        vec3 half_vector = normalize(light_dir + view_dir);
        float light_spec_factor = max(0.0f, dot(normal, half_vector));
        vec3 light_spec_color = specular_color * pow(light_spec_factor, shininess);

        vec3 light_intensity = (light_diffuse_color + light_spec_color) * attenuation;
        if (light_intensity.x < 0){
           color = vec3(0.0f, 1.0f, 1.0f);
        }
        color += light_intensity;
    }





  final_color = vec4(color, 1.0f);
}
