// from the provided sample code

#pragma once

#include "../../RTG.hpp"
#include <glm/glm.hpp>

struct CubePipeline {
    VkDescriptorSetLayout set0_environment = VK_NULL_HANDLE;
    VkDescriptorSetLayout set1_face = VK_NULL_HANDLE;
    VkDescriptorSetLayout set2_params = VK_NULL_HANDLE;

    struct Face {
        struct {
            float m0, m1, m2, padding0_;
            float m3, m4, m5, padding1_;
            float m6, m7, m8, padding2_;
        } WORLD_FROM_PX;
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