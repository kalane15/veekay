#version 450

layout(location = 0) in vec3 f_position;
layout(location = 1) in vec3 f_normal;
layout(location = 2) in vec2 f_uv;

layout(location = 0) out vec4 final_color;

layout(binding = 0, std140) uniform SceneUniforms {
    mat4 view_projection;
    vec3 view_position;
    vec3 ambient_light_intensity;
    vec3 sun_light_direction;
    vec3 sun_light_color;
    uint point_light_count;
};

layout(binding = 1, std140) uniform ModelUniforms {
    mat4 model;
    vec3 albedo_color;
    vec3 specular_color;
    float shininess;
};

struct PointLight {
    vec3 position;
    vec3 color;
    float radius;
};

layout(binding = 2, std430) readonly buffer PointLights {
    PointLight point_lights[];
};

void main() {
    vec3 normal = normalize(f_normal);
    vec3 view_dir = normalize(view_position - f_position);

    // ------------------------------
    // Солнечное освещение
    // ------------------------------
    vec3 sun_dir = normalize(sun_light_direction); // вектор *к сцене*
    float sun_diffuse_factor = max(dot(normal, sun_dir), 0.0);
    vec3 sun_diffuse = albedo_color * sun_diffuse_factor;

    vec3 sun_half = normalize(view_dir + sun_dir);
    float sun_spec_factor = max(dot(normal, sun_half), 0.0);
    vec3 sun_specular = specular_color * pow(sun_spec_factor, shininess);

    vec3 sun_color = ambient_light_intensity + sun_light_color * (sun_diffuse + sun_specular);

    // ------------------------------
    // Точечные источники
    // ------------------------------

//    if (point_light_count == 1){
//        final_color = vec4(1.0, 1.0, 1.0, 1.0);
//        return;
//    }

    vec3 point_light_color = vec3(0.0, 0.0, 0.0);
    for (uint i = 0; i < point_light_count; ++i) {
        PointLight light = point_lights[i];

        vec3 light_dir = normalize(light.position - f_position);
        float distance = length(light.position - f_position);

        // Ослабление света по закону обратного квадрата
        float attenuation = light.radius / (distance * distance);

        // Рассеянное освещение
        float diffuse_factor = max(dot(normal, light_dir), 0.0);
        vec3 diffuse = light.color * diffuse_factor;

        // Зеркальное освещение (Blinn-Phong)
        vec3 half_vec = normalize(light_dir + view_dir);
        float spec_factor = max(dot(normal, half_vec), 0.0);
        vec3 specular = specular_color * pow(spec_factor, shininess);

        point_light_color += attenuation * (diffuse + specular);
    }
    vec3 color = sun_color + point_light_color;


    if (color.x > 1.0){
        color.x = 1.0;
    }
    if (color.y > 1.0){
        color.y = 1.0;
    }
    if (color.z > 1.0){
        color.z = 1.0;
    }

    final_color = vec4(color, 1.0);
}
