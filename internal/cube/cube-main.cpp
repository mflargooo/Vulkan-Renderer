#include "../../RTG.hpp"
#include "CubePipeline.hpp"
#include "../../VK.hpp"

#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../external/stb/stb_image_write.h"

#include <glm/glm.hpp>

#include <iostream>

struct GPUFace {
    Helpers::AllocatedImage image;
    Helpers::AllocatedBuffer buffer;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptors = VK_NULL_HANDLE;
    void create(RTG &rtg, VkDescriptorPool descriptor_pool, CubePipeline const &pipeline, uint32_t const size, glm::vec3 * const data) {
        std::vector< glm::vec4 > data_padded;
        data_padded.reserve(size*size);
        for (uint32_t i = 0; i < size*size; ++i) {
            data_padded.emplace_back(glm::vec4(data[i], 0.0f));
        }

        image = rtg.helpers.create_image(
            VkExtent2D{ .width = size, .height = size },
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            Helpers::Unmapped
        );

        rtg.helpers.transfer_to_image(data_padded.data(), sizeof(data_padded[0])*size*size, image, VK_IMAGE_LAYOUT_GENERAL);

        { // buffer
            buffer = rtg.helpers.create_buffer(
                sizeof(CubePipeline::Face),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                Helpers::Unmapped
            );

            CubePipeline::Face face_info{};

            glm::vec3 s = glm::vec3(0.f, 0.f, -1.f);
            glm::vec3 t = glm::vec3(0.f, -1.f, -0.f);
            glm::vec3 center = glm::vec3(1.f, 0.f, 0.f);

            face_info.WORLD_FROM_PX[0][0] = 2.f * s.x / float(size);
            face_info.WORLD_FROM_PX[0][1] = 2.f * s.y / float(size);
            face_info.WORLD_FROM_PX[0][2] = 2.f * s.z / float(size);

            face_info.WORLD_FROM_PX[1][0] = 2.f * t.x / float(size);
            face_info.WORLD_FROM_PX[1][1] = 2.f * t.y / float(size);
            face_info.WORLD_FROM_PX[1][2] = 2.f * t.z / float(size);

            float corner = 1.f - 2.f / float(size) * .5f;
            face_info.WORLD_FROM_PX[2][0] = center.x - corner * s.x - corner * t.x;
            face_info.WORLD_FROM_PX[2][1] = center.y - corner * s.y - corner * t.y;
            face_info.WORLD_FROM_PX[2][2] = center.z - corner * s.z - corner * t.z;

            rtg.helpers.transfer_to_buffer( &face_info, sizeof(face_info), buffer );
        }

        { // image view
            VkImageViewCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .flags = 0,
                .image = image.handle,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = image.format,
                // .components auto initialized
                .subresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };
            VK( vkCreateImageView(rtg.device, &create_info, nullptr, &view) );
        }

        { // descriptor set w world_from_px and storage image
            { // allocate
                VkDescriptorSetAllocateInfo alloc_info{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = descriptor_pool,
                    .descriptorSetCount = 1,
                    .pSetLayouts = &pipeline.set01_face,
                };
                VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptors) );
            }

            // write
            VkDescriptorBufferInfo buffer_info{
                .buffer = buffer.handle,
                .offset = 0,
                .range = buffer.size,
            };
            VkDescriptorImageInfo image_info{
                .imageView = view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };
            std::array< VkWriteDescriptorSet, 2 > writes{
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptors,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &buffer_info,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptors,
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = &image_info,
                },
            };
            
            vkUpdateDescriptorSets(rtg.device, (uint32_t)(writes.size()), writes.data(), 0, nullptr);
        }
    }
    void destroy(RTG &rtg) {
        vkDestroyImageView(rtg.device, view, nullptr);
        view = VK_NULL_HANDLE;
        rtg.helpers.destroy_buffer(std::move(buffer));
        rtg.helpers.destroy_image(std::move(image));
    }
};

int main(int argc, char **argv) {
    try {
        RTG::Configuration configuration;
        
        configuration.application_info = VkApplicationInfo{
            .pApplicationName = "Cube Utility",
            .applicationVersion = VK_MAKE_VERSION(0,0,0),
            .pEngineName = "Unknown",
            .engineVersion = VK_MAKE_VERSION(0,0,0),
            .apiVersion = VK_API_VERSION_1_3
        };

        bool print_usage = false;

        try {
            configuration.parse(argc, argv);
        } catch (std::runtime_error &e) {
            std::cerr << "Failed to parse arguments:\n" << e.what() << std::endl;
            print_usage = true;
        }

        if (print_usage) {
            std::cerr << "Usage:" << std::endl;
            RTG::Configuration::usage( [](const char *arg, const char *desc){
                std::cerr << "    " << arg << "\n        " << desc << std::endl;
            });
            return 1;
        }

        configuration.headless = true;

        RTG rtg(configuration);

        // TODO: create comp pipeline
        CubePipeline pipeline;
        pipeline.create(rtg);

        // TODO: load imgs / create desc

        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        { // create dp
            std::array< VkDescriptorPoolSize, 2 > pool_sizes{
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 6*2 + 1, // one for each iinput and output cube face + one for params
                },
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .descriptorCount = 6*2, // one for each input and output cube face
                },
            };

            VkDescriptorPoolCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = 0,
                .maxSets = 12 + 1,
                .poolSizeCount = (uint32_t)(pool_sizes.size()),
                .pPoolSizes = pool_sizes.data(),
            };

            VK( vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool) );
        }

        VkCommandPool command_pool = VK_NULL_HANDLE;
        { // create cmd pool
            VkCommandPoolCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = rtg.graphics_queue_family.value(),
            };
            VK( vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool) );
        }

        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        { // alloc cmd buffer from cmd pool
            VkCommandBufferAllocateInfo alloc_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            VK( vkAllocateCommandBuffers(rtg.device, &alloc_info, &command_buffer) );
        }

        size_t size = 128;
        std::vector< glm::vec3 > data(size * size, glm::vec3(1.f, 0.f, 1.f));

        GPUFace in_face;
        in_face.create(rtg, descriptor_pool, pipeline, (uint32_t)size, data.data());
        GPUFace out_face;
        out_face.create(rtg, descriptor_pool, pipeline, (uint32_t)size, data.data());

        { // run pipeline
            VK( vkResetCommandBuffer(command_buffer, 0) );

            { // begin rec
                VkCommandBufferBeginInfo begin_info{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                VK( vkBeginCommandBuffer(command_buffer, &begin_info) );
            }

            // use cube pipeline
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);

            { // bind descriptor sets
                std::array< VkDescriptorSet, 2 > descriptor_sets{
                    in_face.descriptors,
                    out_face.descriptors,
                    // params_descriptors,
                };
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.layout,
                    0,
                    (uint32_t)(descriptor_sets.size()), descriptor_sets.data(),
                    0, nullptr
                );
            }

            // run
            vkCmdDispatchBase(command_buffer, 0, 0, 1, (uint32_t)size, (uint32_t)size, 1);

            // finish rec
            VK( vkEndCommandBuffer(command_buffer) );

            { // submit cmd buf
                VkSubmitInfo submit_info{
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &command_buffer,
                };

                VK( vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, nullptr) );
            }

            VK( vkDeviceWaitIdle(rtg.device) );
        }

        std::cout << "Computing: done." << std::endl;

        stbi_write_png("compute_shader_test.png", 128, 128, 3, data.data(), sizeof(glm::vec3));
        std::cout << "Saved image." << std::endl;

        in_face.destroy(rtg);
        out_face.destroy(rtg);

        vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;

        vkDestroyCommandPool(rtg.device, command_pool, nullptr);
        command_pool = VK_NULL_HANDLE;

        pipeline.destroy(rtg);

    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}