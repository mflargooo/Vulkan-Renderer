#include "Viewer.hpp"

#include <glm/glm.hpp>

void Viewer::ObjectsPipeline::Light::pack_light(Viewer::ObjectsPipeline::Light &light, S72::Light& s72_light) {
    if (auto p_sun = std::get_if< S72::Light::Sun >(&s72_light.source)) {
        light.radiance = s72_light.tint * p_sun->strength;
        light.radius = p_sun->angle;
        light.type = 0;
    } else if (auto p_sphere = std::get_if< S72::Light::Sphere >(&s72_light.source)) {
        light.radiance = s72_light.tint * p_sphere->power;
        light.attenuation = std::isinf(p_sphere->limit) ? 0.f : 1.f / (p_sphere->limit * p_sphere->limit);
        light.radius = p_sphere->radius;
        light.type = 1;
    } else if (auto p_spot = std::get_if< S72::Light::Spot >(&s72_light.source)) {
        light.radiance = s72_light.tint * p_spot->power;
        light.cos_inner = std::cosf(p_spot->fov * .5f * (1.f - p_spot->blend));
        light.cos_outer = std::cosf(p_spot->fov * .5f);
        light.attenuation = std::isinf(p_spot->limit) ? 0.f : 1.f / (p_spot->limit * p_spot->limit);
        light.radius = p_spot->radius;
        light.type = 2;
    } 
};