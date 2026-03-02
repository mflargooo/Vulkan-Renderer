// from the provided sample code

#pragma once

#include "../../RTG.hpp"
#include <glm/glm.hpp>

struct CubePipeline {
    VkDescriptorSetLayout set01_face = VK_NULL_HANDLE;
    VkDescriptorSetLayout set2_params = VK_NULL_HANDLE;

    struct Face {
        union {
            glm::mat4x3 data_;
            struct {
                glm::mat3 WORLD_FROM_PX;
                glm::vec3 padding_;
            };
        };
    };
    static_assert(sizeof(Face) == (3*4)*4, "Face descriptor is the expected size");

    struct Params {
        float roughness;
    };
    static_assert(sizeof(Params) == 4, "Params descriptor is the expected size.");

    // no push consts

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline handle = VK_NULL_HANDLE;

    void create(RTG &);
    void destroy(RTG &);
};