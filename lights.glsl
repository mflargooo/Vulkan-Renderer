#extension GL_EXT_nonuniform_qualifier : require

struct Light {
    mat4 CLIP_FROM_WORLD;
    mat4 WORLD_FROM_LIGHT;
    vec3 energy;
    float limit_inv;
    float radius;
    float cos_outer;
    float cos_inner;
    uint flags;
};

layout(set=8, binding=0, std430) readonly buffer Lights {
    uint num_lights, _padding0, _padding1, _padding2;
    Light LIGHTS[];
};

layout(set=9, binding=0) uniform sampler2DShadow SHADOWS[];

// heavily inspired from s72-viewer https://github.com/15-472/s72/blob/main/viewer/s72-viewer.mjs
float horizon_approx(float NoL, float sin_ha) {
    if (NoL > sin_ha)
        return NoL;
    else if (NoL > -sin_ha)
        return mix(sin_ha, 0.0, (NoL - sin_ha) / (-sin_ha - sin_ha));
    else return 0.0;
}

vec3 compute_diffuse_lighting(Light light, vec3 position, vec3 normal) {
    uint type = light.flags & 3;

    if (type == 0) { // SUN
        vec3 L = normalize(light.WORLD_FROM_LIGHT[2].xyz);        
        float NoL = dot(normal, L);
        float half_angle = light.radius * 0.5;
        float sin_ha = sin(half_angle);

        float w = horizon_approx(NoL, sin_ha);
        return w * light.energy / 3.141592653589;

    } else if (type == 1) { // SPHERE
        vec3 light_pos = light.WORLD_FROM_LIGHT[3].xyz;
        vec3 to_light = light_pos - position;
        vec3 L = normalize(to_light);
        float d = length(to_light);
        float r = max(.001, light.radius);

        float mdr = max(d, r);
        float nonphysical_falloff = max(0.0, 1.0 - pow(d * light.limit_inv, 4.0));
        float physical_falloff = 1 / (4.0 * 3.141592653589 * mdr * mdr);
        
        float sin_ha = (d <= r ? 1.0 : r / d);
        float NoL = dot(normal, L);

        float attenuation = physical_falloff * nonphysical_falloff;

        float w = horizon_approx(NoL, sin_ha);
        if (d < r) {
            w = 1.0;
        }
        return w * attenuation * light.energy / 3.141592653589;
        
    } else if (type == 2) { // SPOT
        vec3 light_pos = light.WORLD_FROM_LIGHT[3].xyz;
        vec3 to_light = light_pos - position;
        vec3 L = normalize(to_light);
        float d = length(to_light);
        float r = max(.001, light.radius);

        float mdr = max(d, r);
        float nonphysical_falloff = max(0.0, 1.0 - pow(d * light.limit_inv, 4.0));
        float physical_falloff = 1 / (4.0 * 3.141592653589 * mdr * mdr);

        float sin_ha = (d <= r ? 1.0 : r / d);
        float NoL = dot(normal, L);

        vec3 light_look_dir = normalize(light.WORLD_FROM_LIGHT[2].xyz);
        float cos_theta = dot(L, light_look_dir);
        float spot_blend = 0.0;
        if (abs(light.cos_inner - light.cos_outer) < .0001) {
            spot_blend = cos_theta >= light.cos_outer ? 1.0 : 0.0;
        } else { 
            spot_blend = clamp((cos_theta - light.cos_outer) / (light.cos_inner - light.cos_outer), 0.0, 1.0);
        }

        float attenuation = physical_falloff * nonphysical_falloff * spot_blend;

        float w = horizon_approx(NoL, sin_ha);
        if (d < r) {
            w = 1.0;
        }
        return w * attenuation * light.energy / 3.141592653589;
    }

    return vec3(0.0);
}

#include "ggx.glsl"

vec3 compute_specular_lighting(Light light, vec3 position, vec3 normal, vec3 view, float roughness, vec3 tint) {
    uint type = light.flags & 3;
    
    if (type == 0) { // SUN
        vec3 L = normalize(light.WORLD_FROM_LIGHT[2].xyz);        
        float NoL = dot(normal, L);
        float half_angle = light.radius * 0.5;
        float sin_ha = sin(half_angle);
        float cos_ha = cos(half_angle);

        vec3 R = reflect(-view, normal);
        if (dot(R, L) < cos_ha) { 
            // if r outside the disc, rescale perpendicular direction to disc size
            vec3 p = R - dot(R, L) * L;
            R = normalize(L * cos_ha + normalize(p) * sin_ha);
        }

        float NoR = dot(normal, R);
        if (NoR <= 0.0) return vec3(0.0);

        vec3 h = normalize(view + R);
        float NoV = dot(normal, view);
        float NoH = dot(normal, h);
        float VoH = dot(view, h);

        float OneMinusVoH = 1.0 - VoH;
        float Fc = OneMinusVoH * OneMinusVoH * OneMinusVoH * OneMinusVoH * OneMinusVoH;
        vec3 F = tint + (1.0 - tint) * Fc;

        float a = roughness * roughness;
        float a_prime = clamp(a + sin_ha * .5, 0.0, 1.0);
        float norm = a / a_prime;
        norm *= norm;

        float a2_prime = a_prime * a_prime;
        float c = (NoH * NoH * (a2_prime - 1.0) + 1.0);
        float D_prime = a2_prime / (3.141592653589 * c * c);

        float w = horizon_approx(NoL, sin_ha);

        float k = a2_prime * 0.5;
        float G_smith_prime = G_1(NoR, k) * G_1(NoV, k);
        
        return w * norm * D_prime * G_smith_prime * F * light.energy / (4.0 * NoV);
    } else if (type == 1) { // SPHERE
        vec3 light_pos = light.WORLD_FROM_LIGHT[3].xyz;  
        vec3 to_light = light_pos - position;
        vec3 L = normalize(to_light);

        float d = length(to_light);
        float r = max(.001, light.radius);

        // per epics notes
        vec3 R = reflect(-view, normal);
        vec3 center_to_ray = dot(to_light, R) * R - to_light; 
        vec3 closest_point = to_light + center_to_ray * clamp( r / length(center_to_ray), 0.0, 1.0 );

        R = normalize(closest_point);

        float NoR = dot(normal, R);
        if (NoR <= 0.0) return vec3(0.0);
        
        float mdr = max(d, r);
        float nonphysical_falloff = max(0.0, 1.0 - pow(d * light.limit_inv, 4.0));
        float physical_falloff = 1 / (4.0 * 3.141592653589 * mdr * mdr);

        float sin_ha = (d <= r ? 1.0 : r / d);
        float NoL = dot(normal, L);

        float attenuation = physical_falloff * nonphysical_falloff;

        vec3 h = normalize(view + R);
        float NoV = dot(normal, view);
        float NoH = dot(normal, h);
        float VoH = dot(view, h);

        float OneMinusVoH = 1.0 - VoH;
        float Fc = OneMinusVoH * OneMinusVoH * OneMinusVoH * OneMinusVoH * OneMinusVoH;
        vec3 F = tint + (1.0 - tint) * Fc;

        float a = roughness * roughness;
        float a_prime = clamp(a + sin_ha * .5, 0.0, 1.0);
        float norm = a / a_prime;
        norm *= norm;

        float a2_prime = a_prime * a_prime;
        float c = (NoH * NoH * (a2_prime - 1.0) + 1.0);
        float D_prime = a2_prime / (3.141592653589 * c * c);

        float k = a2_prime * 0.5;
        float G_smith_prime = G_1(NoR, k) * G_1(NoV, k);

        float w = horizon_approx(NoL, sin_ha);
        if (d < r) {
            w = 1.0;
        }
        return w * attenuation * norm * D_prime * G_smith_prime * F * light.energy / (4.0 * NoV);
    } else if (type == 2) { // SPOT
        vec3 light_pos = light.WORLD_FROM_LIGHT[3].xyz;  
        vec3 to_light = light_pos - position;
        vec3 L = normalize(to_light);

        float d = length(to_light);
        float r = max(.001, light.radius);

        // per epics notes
        vec3 R = reflect(-view, normal);
        vec3 center_to_ray = dot(to_light, R) * R - to_light; 
        vec3 closest_point = to_light + center_to_ray * clamp( r / length(center_to_ray), 0.0, 1.0 );

        R = normalize(closest_point);

        float NoR = dot(normal, R);
        if (NoR <= 0.0) return vec3(0.0);
        
        float mdr = max(d, r);
        float nonphysical_falloff = max(0.0, 1.0 - pow(d * light.limit_inv, 4.0));
        float physical_falloff = 1 / (4.0 * 3.141592653589 * mdr * mdr);

        float sin_ha = (d <= r ? 1.0 : r / d);
        float NoL = dot(normal, L);

        vec3 light_look_dir = normalize(light.WORLD_FROM_LIGHT[2].xyz);
        float cos_theta = dot(L, light_look_dir);

        float spot_blend = 0.0;
        if (abs(light.cos_inner - light.cos_outer) < .00001) {
            spot_blend = cos_theta >= light.cos_outer ? 1.0 : 0.0;
        } else { 
            spot_blend = clamp((cos_theta - light.cos_outer) / (light.cos_inner - light.cos_outer), 0.0, 1.0);
        }

        float attenuation = physical_falloff * nonphysical_falloff * spot_blend;

        vec3 h = normalize(view + R);
        float NoV = dot(normal, view);
        float NoH = dot(normal, h);
        float VoH = dot(view, h);

        float OneMinusVoH = 1.0 - VoH;
        float Fc = OneMinusVoH * OneMinusVoH * OneMinusVoH * OneMinusVoH * OneMinusVoH;
        vec3 F = tint + (1.0 - tint) * Fc;

        float a = roughness * roughness;
        float a_prime = clamp(a + sin_ha * .5, 0.0, 1.0);
        float norm = a / a_prime;
        norm *= norm;

        float a2_prime = a_prime * a_prime;
        float c = (NoH * NoH * (a2_prime - 1.0) + 1.0);
        float D_prime = a2_prime / (3.141592653589 * c * c);


        float k = a2_prime * 0.5;
        float G_smith_prime = G_1(NoR, k) * G_1(NoV, k);

        float w = horizon_approx(NoL, sin_ha);
        if (d < r) {
            w = 1.0;
        }

        return w * attenuation * norm * D_prime * G_smith_prime * F * light.energy / (4.0 * NoV);
    }

    return vec3(1.0, 0.0, 1.0);
}

void forward_pass(vec3 position, vec3 normal, vec3 view, float roughness, vec3 tint, out vec3 direct_diffuse, out vec3 direct_specular) {
    direct_diffuse = vec3(0.0);
    direct_specular = vec3(0.0);
    for (int i = 0; i < num_lights; ++i) {
        if (LIGHTS[i].flags == 6) {
            vec4 light_clip_pos = LIGHTS[i].CLIP_FROM_WORLD * vec4(position, 1.0);
            vec3 uvw = light_clip_pos.xyz / light_clip_pos.w * 0.5 + 0.5;
            if (uvw.x > 1.0 || uvw.x < 0.0 || uvw.y > 1.0 || uvw.y < 0.0) continue; // outside of light fov
            float closest = textureProj(SHADOWS[nonuniformEXT(i)], vec4(uvw.xy, light_clip_pos.z / light_clip_pos.w, 1.0));
            if (uvw.z > closest) continue;
        }

        direct_diffuse += compute_diffuse_lighting(LIGHTS[i], position, normal);
        direct_specular += compute_specular_lighting(LIGHTS[i], position, normal, view, roughness, tint);
    }
}