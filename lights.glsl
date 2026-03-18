struct Light {
    mat4 WORLD_FROM_LIGHT;
    vec3 energy;
    float limit_inv;
    float radius;
    float cos_outer;
    float cos_inner;
    uint type;
};

layout(set=8, binding=0, std430) readonly buffer Lights {
    uint num_lights, _padding0, _padding1, _padding2;
    Light LIGHTS[];
};

// heavily inspired from s72-viewer https://github.com/15-472/s72/blob/main/viewer/s72-viewer.mjs
float horizon_approx(float NoL, float sin_ha) {
    if (NoL > sin_ha)
        return NoL;
    else if (NoL > -sin_ha)
        return mix(sin_ha, 0.0, (NoL - sin_ha) / (-sin_ha - sin_ha));
    else return 0.0;
}

vec3 compute_diffuse_lighting(Light light, vec3 position, vec3 normal) {
    if (light.type == 0) { // SUN
        vec3 L = normalize(light.WORLD_FROM_LIGHT[2].xyz);        
        float NoL = dot(normal, L);
        float half_angle = light.radius / 2.0;
        float sin_ha = sin(half_angle);

        float w = horizon_approx(NoL, sin_ha);
        return w * light.energy / 3.141592653589;
    } else if (light.type == 1) { // SPHERE
        vec3 light_pos = light.WORLD_FROM_LIGHT[3].xyz;
        vec3 light_dir = light_pos - position;
        vec3 L = normalize(light_dir);
        float d = length(light_dir);
        float r = max(.001, light.radius);

        float mdr = max(d, r);
        float nonphysical_falloff = max(0.0, 1.0 - pow(d * light.limit_inv, 4.0));
        float physical_falloff = 1 / (4.0 * 3.141592653589 * mdr * mdr);

        float sin_ha = (d <= r ? 1.0 : r / d);
        float NoL = dot(normal, L);

        float attenuation = physical_falloff * nonphysical_falloff;

        float w = horizon_approx(NoL, sin_ha);
        return w * attenuation * light.energy / 3.141592653589;
        
    } else if (light.type == 2) { // SPOT
        return vec3(0.0);
        vec3 light_pos = light.WORLD_FROM_LIGHT[3].xyz;
        vec3 light_dir = light_pos - position;
        vec3 L = normalize(light_dir);
        float d = length(light_dir);
        float r = max(.001, light.radius);

        float mdr = max(d, r);
        float nonphysical_falloff = max(0.0, 1.0 - pow(d * light.limit_inv, 4.0));
        float physical_falloff = 1 / (4.0 * 3.141592653589 * mdr * mdr);

        float sin_ha = (d <= r ? 1.0 : r / d);
        float NoL = dot(normal, L);

        vec3 light_look_dir = normalize(light.WORLD_FROM_LIGHT[2].xyz);
        float cos_theta = dot(L, light_look_dir);
        float spot_blend = clamp((cos_theta - light.cos_outer) / (light.cos_inner - light.cos_outer), 0.0, 1.0);

        float attenuation = physical_falloff * nonphysical_falloff * spot_blend;

        float w = horizon_approx(NoL, sin_ha);
        return w * attenuation * light.energy / 3.141592653589;
    }

    return vec3(0.0);
}

vec3 forward_pass(vec3 position, vec3 normal, vec3 view, float roughness, vec3 tint) {
    vec3 out_light = vec3(0.0);
    for (int i = 0; i < num_lights; ++i) {
        out_light += compute_diffuse_lighting(LIGHTS[i], position, normal);
    }
    return out_light;
}