#include "Viewer.hpp"

#include <glm/glm.hpp>

void Viewer::ObjectsPipeline::Light::pack_light(Viewer::ObjectsPipeline::Light &light, S72::Light& s72_light) {
    if (auto p_sun = std::get_if< S72::Light::Sun >(&s72_light.source)) {
        light.energy = s72_light.tint * p_sun->strength;
        light.radius = p_sun->angle;
        light.flags = 0;
    } else if (auto p_sphere = std::get_if< S72::Light::Sphere >(&s72_light.source)) {
        light.energy = s72_light.tint * p_sphere->power;
        light.limit_inv = std::isinf(p_sphere->limit) ? 0.f : 1.f / p_sphere->limit;
        light.radius = p_sphere->radius;
        light.flags = 1;
    } else if (auto p_spot = std::get_if< S72::Light::Spot >(&s72_light.source)) {
        light.energy = s72_light.tint * p_spot->power;
        light.cos_inner = glm::clamp(std::cosf(p_spot->fov * .5f * (1.f - p_spot->blend)), -1.f, 1.f);
        light.cos_outer = glm::clamp(std::cosf(p_spot->fov * .5f), -1.f, 1.f);
        light.limit_inv = std::isinf(p_spot->limit) ? 0.f : 1.f / p_spot->limit;
        light.radius = p_spot->radius;
        light.flags = 2 | (s72_light.shadow > 0 ? 4 : 0); // type | has_shadow_map
    } 
};