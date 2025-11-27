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
    uint spot_light_count;
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
    float intensity;
};

struct SpotLight {
    vec3 position;
    float intensity;
    vec3 direction;
    float angle;
    vec3 color;
};

layout(binding = 2, std430) readonly buffer PointLights {
    PointLight point_lights[];
};

layout(binding = 3, std430) readonly buffer SpotLights {
    SpotLight spot_lights[];
};

layout (binding = 4) uniform sampler2D albedo_texture;
layout (binding = 5) uniform sampler2D specular_texture;
layout (binding = 6) uniform sampler2D emissive_texture;

void main() {
    vec3 normal = normalize(f_normal);
    vec3 view_dir = normalize(view_position - f_position);

    vec2 my_fuv = f_uv;
    float checker = mod(floor(f_uv.x * 10.0) + floor(f_uv.y * 10.0), 2.0);

    if (checker == 0.0) {
        my_fuv = f_uv;
    } else {
        my_fuv = f_uv+0.1;
    }
    vec4 texel = texture(albedo_texture, my_fuv);

    vec3 spec_tex = texture(specular_texture, f_uv).rgb;
    vec3 emissive_tex = texture(emissive_texture, f_uv).rgb;

    // ------------------------------
    // Солнечное освещение
    // ------------------------------
    vec3 sun_dir = normalize(sun_light_direction); // вектор *к сцене*
    float sun_diffuse_factor = max(dot(normal, sun_dir), 0.0);

    // diffuse
    vec3 sun_diffuse = texel.rgb * sun_light_color * sun_diffuse_factor;

    // specular
    vec3 sun_half = normalize(view_dir + sun_dir);
    float sun_spec_factor = max(dot(normal, sun_half), 0.0);

    vec3 sun_specular =
        spec_tex *
        sun_light_color *
        pow(sun_spec_factor, shininess) *
        sun_diffuse;

    vec3 sun_color = sun_diffuse + sun_specular;

    // ------------------------------
    // Точечные источники
    // ------------------------------

    vec3 point_light_color = vec3(0.0);
    for (uint i = 0; i < point_light_count; ++i) {
        PointLight light = point_lights[i];

        vec3 light_dir = normalize(light.position - f_position);
        float distance = length(light.position - f_position);
        float attenuation = light.intensity / (distance * distance);

        float diffuse_factor = max(dot(normal, light_dir), 0.0);

        // diffuse
        vec3 diffuse = texel.rgb * light.color * diffuse_factor;

        // specular
        vec3 half_vec = normalize(light_dir + view_dir);
        float spec_factor = max(dot(normal, half_vec), 0.0);

        vec3 specular =
            spec_tex *
            light.color *
            pow(spec_factor, shininess) *
            diffuse_factor;

        point_light_color += attenuation * (diffuse + specular);
    }

    // ------------------------------
    // Прожекторы
    // ------------------------------

    vec3 spot_light_color = vec3(0.0);

    for (uint i = 0; i < spot_light_count; ++i) {
        SpotLight light = spot_lights[i];

        vec3 light_dir = normalize(light.position - f_position);
        float distance = length(light.position - f_position);
        float attenuation = light.intensity / (distance * distance);

        float diffuse_factor = max(dot(normal, light_dir), 0.0);

        // diffuse
        vec3 diffuse = texel.rgb * light.color * diffuse_factor;

        // specular
        vec3 half_vec = normalize(light_dir + view_dir);
        float spec_factor = max(dot(normal, half_vec), 0.0);

        vec3 specular =
            spec_tex *
            light.color *
            pow(spec_factor, shininess) *
            diffuse_factor;

        vec3 lighting = attenuation * (diffuse + specular);

        // угол прожектора
        float theta = dot(light_dir, normalize(light.direction));
        float outer_angle = light.angle;
        float inner_angle = light.angle * 0.9;

        float spot_factor = clamp(
            (outer_angle - theta) / (inner_angle - outer_angle),
            0.0, 1.0
        );

        spot_light_color += lighting * spot_factor;
    }

    // ------------------------------
    // Финальный цвет
    // ------------------------------
    vec3 color =
        ambient_light_intensity +
        sun_color +
        point_light_color +
        spot_light_color +
        emissive_tex;

    final_color = vec4(color, texel.a);
}
