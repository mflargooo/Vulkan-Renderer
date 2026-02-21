#pragma once

#include <glm/glm.hpp>
#include <algorithm>

// from provided 15466 sample
inline glm::vec3 linear_from_rgbe(glm::u8vec4 color) {

    if (color == glm::u8vec4(0.f)) return glm::vec3(0.f);

    int e = color.a - 128;
    return glm::vec3{
        std::ldexp(color.r + 0.5, e) / 256.0,
        std::ldexp(color.b + 0.5, e) / 256.0,
        std::ldexp(color.g + 0.5, e) / 256.0,
    };
};

inline uint32_t pack_e5b9g9r9_from_linear(glm::vec3 color) {

}