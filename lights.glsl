struct Light {
    mat4 WORLD_FROM_LIGHT;
    vec3 radiance;
    float attenuation;
    float radius;
    float cos_outer;
    float cos_inner;
    uint type;
};

layout(set=8, binding=0, std430) readonly buffer Lights {
    uint num_lights, _padding0, _padding1, _padding2;
    Light LIGHTS[];
};

// lecture 11 slides and https://graphics.cs.cmu.edu/courses/15-468/lectures/lecture_10.pdf
vec3 compute_lighting(Light light, vec3 position, vec3 normal, vec3 view, float roughness, vec3 tint) {
    if (light.type == 0) { // SUN
        vec3 dir = normalize(-light.WORLD_FROM_LIGHT[2].xyz);        
        float NoL = dot(normal, dir);
        if (NoL <= 0.0) return vec3(0.f);
        return NoL * light.radiance;
    } else if (light.type == 1) { // SPHERE

    } else if (light.type == 2) { // SPOT

    }
    return vec3(0.0);
}

vec3 forward_pass(vec3 position, vec3 normal, vec3 view, float roughness, vec3 tint) {
    vec3 out_light = vec3(0.0);
    for (int i = 0; i < num_lights; ++i) {
        out_light += compute_lighting(LIGHTS[i], position, normal, view, roughness, tint);
    }
    return out_light;
}