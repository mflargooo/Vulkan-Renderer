#pragma once

#include <string>
#include <glm/glm.hpp>

struct Scene {

    struct Camera {
        glm::mat4 transform;
        
        float aspect = 1.7778f;
        float vfov = 60.f / 180.f * float(M_PI);
        float near = .1f;
        float far = 1000.f;
    };

    struct Material {
        int albedo_id = -1;
        int normal_id = -1;
        int displacement_id = -1;
    };

    const Scene load_scene(const std::string &file);
};

