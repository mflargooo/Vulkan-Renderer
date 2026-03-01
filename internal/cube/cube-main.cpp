#include "../../RTG.hpp"
#include "CubePipeline.hpp"
#include "VK.hpp"

#include <glm/glm.hpp>

#include <iostream>

struct GPUFACE {
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