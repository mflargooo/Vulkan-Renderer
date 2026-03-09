#pragma once

#include "../../RTG.hpp"
#include "CubePipeline.hpp"
#include "../../VK.hpp"
#include "../../rgbe.hpp"

#include "../../external/stb/stb_image.h"

#include "../../external/stb/stb_image_write.h"

#include <glm/glm.hpp>

#include <iostream>

// from the provided sample code
struct GPUFace {
    Helpers::AllocatedImage image;
    Helpers::AllocatedBuffer buffer;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptors = VK_NULL_HANDLE;
    void create(RTG &rtg, VkDescriptorPool descriptor_pool, CubePipeline const &pipeline, uint32_t const size, glm::vec3 * const data, uint32_t face_id) {
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

            static const glm::vec3 centers[6] = {
                glm::vec3( 1.f,  0.f,  0.f), // +x
                glm::vec3(-1.f,  0.f,  0.f), // -x
                glm::vec3( 0.f,  1.f,  0.f), // +y
                glm::vec3( 0.f, -1.f,  0.f), // -y
                glm::vec3( 0.f,  0.f,  1.f), // +z
                glm::vec3( 0.f,  0.f, -1.f)  // -z
            };
            static const glm::vec3 basis_s[6] = {
                glm::vec3( 0.f,  0.f, -1.f), // +x
                glm::vec3( 0.f,  0.f,  1.f), // -x
                glm::vec3( 1.f,  0.f,  0.f), // +y
                glm::vec3( 1.f,  0.f,  0.f), // -y
                glm::vec3( 1.f,  0.f,  0.f), // +z
                glm::vec3(-1.f,  0.f,  0.f)  // -z
            };
            static const glm::vec3 basis_t[6] = {
                glm::vec3( 0.f, -1.f,  0.f), // +x
                glm::vec3( 0.f, -1.f,  0.f), // -x
                glm::vec3( 0.f,  0.f,  1.f), // +y
                glm::vec3( 0.f,  0.f, -1.f), // -y
                glm::vec3( 0.f, -1.f,  0.f), // +z
                glm::vec3( 0.f, -1.f,  0.f)  // -z
            };

            glm::vec3 s = basis_s[face_id];
            glm::vec3 t = basis_t[face_id];
            glm::vec3 center = centers[face_id];

			face_info.WORLD_FROM_PX.m0 = 2.f * s.x / float(size);
			face_info.WORLD_FROM_PX.m1 = 2.f * s.y / float(size);
			face_info.WORLD_FROM_PX.m2 = 2.f * s.z / float(size);

			face_info.WORLD_FROM_PX.m3 = 2.f * t.x / float(size);
			face_info.WORLD_FROM_PX.m4 = 2.f * t.y / float(size);
			face_info.WORLD_FROM_PX.m5 = 2.f * t.z / float(size);

			float corner = 1.f - 2.f / float(size) * .5f;
			face_info.WORLD_FROM_PX.m6 = center.x - corner * s.x - corner * t.x;
			face_info.WORLD_FROM_PX.m7 = center.y - corner * s.y - corner * t.y;
			face_info.WORLD_FROM_PX.m8 = center.z - corner * s.z - corner * t.z;

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
                    .pSetLayouts = &pipeline.set1_face,
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

void ggx(const char *infile, const char *outfile) {
    try {
        RTG::Configuration configuration;
        
        configuration.application_info = VkApplicationInfo{
            .pApplicationName = "Cube Utility",
            .applicationVersion = VK_MAKE_VERSION(0,0,0),
            .pEngineName = "Unknown",
            .engineVersion = VK_MAKE_VERSION(0,0,0),
            .apiVersion = VK_API_VERSION_1_3
        };

        /* bool print_usage = false;

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
        } */

        configuration.headless = true;

        RTG rtg(configuration);

        CubePipeline pipeline;
        pipeline.create(rtg);

        std::string s_outfile = outfile;

        int cubemap_width, cubemap_height, cubemap_channels;
        void *src_img = (void *)stbi_load(infile, &cubemap_width, &cubemap_height, &cubemap_channels, 4);
        if (!src_img) {
            std::cerr << "Failed to load " << infile << std::endl;
            exit(1);
        }
        
        if (cubemap_height < cubemap_width && cubemap_width == cubemap_height / 6) {
            std::cerr << "Expected cubemap with 6 equal sized square faces as a vertical strip!" << std::endl;
            exit(1);
        }

        int face_width = cubemap_width, face_height = cubemap_width;
        face_height;

        // smallest mip size is 32x32
        uint32_t mips = (uint32_t)std::floor(std::log2f((float)face_width) - std::log2f(32.f)) + 1;


        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        { // create dp
            std::array< VkDescriptorPoolSize, 3 > pool_sizes{
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1, // one for the env
                },
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 6 + 1, // one for each iinput and output cube face + one for params
                },
                VkDescriptorPoolSize{
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .descriptorCount = 6, // one for each input and output cube face
                },
            };

            VkDescriptorPoolCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = 0,
                .maxSets = (1 + 6 + 1) * mips,
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

        Helpers::AllocatedImage environment = rtg.helpers.create_cubemap(
            VkExtent2D{ .width = (uint32_t)face_width, .height = (uint32_t)face_width },
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            Helpers::Unmapped
        );

        { // upload environment
            std::vector< uint64_t > pixels;
            pixels.reserve(cubemap_width * cubemap_height);
            for (size_t i = 0; i < cubemap_width * cubemap_height; ++i) {
                // switch from cubemap render -> vulkan convention
                // rotate cubemap face conventions to upright
                size_t f = i / (face_width * face_height); // current face
                size_t j = i % (face_width * face_height); // jth pixel of face

                glm::u8vec4 pixel = static_cast< glm::u8vec4 * >(src_img)[f * face_height * face_width + j];

                glm::vec3 rgb = linear_from_rgbe(pixel);

                pixels.emplace_back(pack_r16g16b16a16_from_linear(rgb));
            }
            rtg.helpers.transfer_to_cubemap(pixels.data(), sizeof(uint64_t) * cubemap_width * cubemap_height, environment);
        }

        VkImageView environment_view = VK_NULL_HANDLE;
        VkSampler environment_sampler = VK_NULL_HANDLE;
        VkDescriptorSet environment_descriptor = VK_NULL_HANDLE;

        { // make image views for the cubemaps
            VkImageViewCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .flags = 0,
                .image = environment.handle,
                .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
                .format = environment.format,
                // .components sets swizzling and is fine when zero inited
                .subresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 6,
                },
            };

            VK( vkCreateImageView(rtg.device, &create_info, nullptr, &environment_view) );
        }

        { // make a sampler for the cubemaps
            VkSamplerCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .flags = 0,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .mipLodBias = 0.f,
                .anisotropyEnable  = VK_FALSE,
                .maxAnisotropy = 0.f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.f,
                .maxLod = 0.f,
                .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                .unnormalizedCoordinates = VK_FALSE,
            };
            VK( vkCreateSampler(rtg.device, &create_info, nullptr, &environment_sampler) );
        }

        { // allocate and write the cubemap descriptor sets
            // allocate the descriptors (using same alloc_info)
            VkDescriptorSetAllocateInfo alloc_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &pipeline.set0_environment,
            };
            VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &environment_descriptor) );

            // write descriptor for cubemaps

            VkDescriptorImageInfo image_info{
                .sampler = environment_sampler,
                .imageView = environment_view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            VkWriteDescriptorSet write_info{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = environment_descriptor,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_info,
            };

            vkUpdateDescriptorSets( rtg.device, 1, &write_info, 0, nullptr );
        }
            
        VkDescriptorSet params_descriptors;
        Helpers::AllocatedBuffer params_buffer;
        CubePipeline::Params params { 
            .roughness = 1.0f 
        };

        { // create params descriptors
            params_buffer = rtg.helpers.create_buffer(
                sizeof(CubePipeline::Params),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                Helpers::Mapped  
            );

            VkDescriptorSetAllocateInfo alloc_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &pipeline.set2_params
            };

            vkAllocateDescriptorSets(rtg.device, &alloc_info, &params_descriptors);

            VkDescriptorBufferInfo buffer_info{
                .buffer = params_buffer.handle,
                .offset = 0,
                .range = sizeof(CubePipeline::Params),
            };

            VkWriteDescriptorSet write_info{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = params_descriptors,
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_info,
            };
            vkUpdateDescriptorSets(rtg.device, 1, &write_info, 0, nullptr);
        }

        for (size_t m = 1; m < mips; ++m) {
            size_t out_face_size = face_width / (1 << m);
            std::vector< glm::vec3 > data(out_face_size * out_face_size, glm::vec3(1.f, 0.f, 1.f));

            GPUFace out_faces[6];
        
            for (uint32_t i = 0; i < 6; ++i) {
                out_faces[i].create(rtg, descriptor_pool, pipeline, (uint32_t)out_face_size, data.data(), i);
            }

            params.roughness = (float)m / (float)mips;
            std::memcpy(params_buffer.allocation.mapped, &params, sizeof(CubePipeline::Params));

            Helpers::AllocatedBuffer results = rtg.helpers.create_buffer(
                out_face_size * out_face_size * 6 * sizeof(glm::vec4),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                Helpers::Mapped
            );

            for (size_t f = 0; f < 6; ++f) { // run pipeline
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

                GPUFace &out_face = out_faces[f]; 

                { // bind descriptor sets
                    std::array< VkDescriptorSet, 3 > descriptor_sets{
                        environment_descriptor,
                        out_face.descriptors,
                        params_descriptors,
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
                vkCmdDispatchBase(command_buffer, 0, 0, 1, (uint32_t)out_face_size/8, (uint32_t)out_face_size/8, 1);

                { // pull results
                    VkImageMemoryBarrier barrier{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // for GPU -> CPU
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = out_face.image.handle,
                        .subresourceRange{
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                    };

                    vkCmdPipelineBarrier(
                        command_buffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, // dependencyFlags
                        0, nullptr, // mem barrier count, ptr
                        0, nullptr, // buf mem barrier count, ptr
                        1, &barrier // img mem barrier count, ptr
                    );
                } 

                {
                    VkBufferImageCopy region{
                        .bufferOffset = out_face_size * out_face_size * sizeof(glm::vec4) * f,
                        .bufferRowLength = 0,
                        .bufferImageHeight = 0,
                        .imageSubresource{
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel = 0,
                            .baseArrayLayer = 0,
                            .layerCount = 1
                        },
                        .imageOffset{ .x = 0, .y = 0, .z = 0 }, // TODO: compute in blocks
                        .imageExtent{
                            .width = out_face.image.extent.width,
                            .height = out_face.image.extent.height,
                            .depth = 1,
                        },
                    };

                    vkCmdCopyImageToBuffer(
                        command_buffer,
                        out_face.image.handle,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        results.handle,
                        1,
                        &region
                    );
                }

                {
                    VkBufferMemoryBarrier barrier{
                        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                        .buffer = results.handle,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE
                    };

                    vkCmdPipelineBarrier(
                        command_buffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT,
                        0, 
                        0, nullptr, 
                        1, &barrier, 
                        0, nullptr
                    );
                }

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

            std::cout << "Mip " << m << "/" << mips-1 << " done." << std::endl;

            // convert RGBA 32 to RGBA 8
            std::vector< glm::u8vec4 > rgbe(out_face_size * out_face_size * 6);
            for (size_t i = 0; i < out_face_size * out_face_size * 6; ++i) {
                rgbe[i] = rgbe_from_linear(static_cast< glm::vec4 * >(results.allocation.mapped)[i]);
            }

            std::string mip_outfile = s_outfile.substr(0, s_outfile.size() - 3) + std::to_string(m) + ".png";
            stbi_write_png(mip_outfile.data(), (int)out_face_size, (int)out_face_size * 6, 4, rgbe.data(), (int)(out_face_size * sizeof(glm::u8vec4)));        
            rtg.helpers.destroy_buffer(std::move(results));

            for (size_t f = 0; f < 6; ++f) {
                out_faces[f].destroy(rtg);
            }
        } 

        stbi_image_free(src_img);
        rtg.helpers.destroy_buffer(std::move(params_buffer));
        rtg.helpers.destroy_cubemap(std::move(environment));

        if (environment_sampler) {
            vkDestroySampler(rtg.device, environment_sampler, nullptr);
            environment_sampler = VK_NULL_HANDLE;
        }
        vkDestroyImageView(rtg.device, environment_view, nullptr);
        environment_view = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;

        vkDestroyCommandPool(rtg.device, command_pool, nullptr);
        command_pool = VK_NULL_HANDLE;

        pipeline.destroy(rtg);

    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        exit(1);
    }
}