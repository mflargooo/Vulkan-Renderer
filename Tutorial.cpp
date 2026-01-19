#include "Tutorial.hpp"

#include "VK.hpp"
#include "refsol.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{ // create descriptor pool
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); // for easier-to-read counting

		std::array< VkDescriptorPoolSize, 1 > pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1 * per_workspace, // one descriptor per set, one set per workspace
			},
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, // omission of CREATE_FREE_DESCRIPTOR_SET_BIT, prevent individual descriptors allocated from this pool
			.maxSets = 1 * per_workspace, // one set per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK( vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool) );
	}

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);

		workspace.Camera_src = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // GPU to copy from this mem
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible mem coherent (no special sync needed)
			Helpers::Mapped
		);
		workspace.Camera = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // to use as uniform buf, GPU to copy into here
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // GPU-local mem
			Helpers::Unmapped // no pointer to this mem 
		);

		{ // allocate descriptor set for Camera descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lines_pipeline.set0_Camera,
			};

			VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Camera_descriptors) );
		}

		{ // point descriptor to camera buffer
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			std::array< VkWriteDescriptorSet, 1 > writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device, // device
				uint32_t(writes.size()), // descriptorWriteCount
				writes.data(), // pDescriptorWrites
				0, // descriptorCopyCount
				nullptr // pDescriptorCopies
			);
		}
	}

	{ // create obj vertices
		std::vector< PosNorTexVertex > vertices;

		{
			plane_vertices.first = uint32_t(vertices.size());
			// [-1,1]x[-1,1]x{0} quad
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.f, .y = -1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 0.f, .t = 0.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.f, .y = -1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 1.f, .t = 0.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.f, .y = 1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 0.f, .t = 1.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.f, .y = 1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 1.f, .t = 1.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.f, .y = 1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 0.f, .t = 1.f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.f, .y = -1.f, .z = 0.f },
				.Normal{ .x = 0.f, .y = 0.f, .z = 1.f },
				.TexCoord{ .s = 1.f, .t = 0.f },
			});

			plane_vertices.count = uint32_t(vertices.size() - plane_vertices.first);
		}

		{
			torus_vertices.first = uint32_t(vertices.size());

			// (u, v) parameterization with
			// - u is angle around main axis (+z)
			// - v is angle around tube

			constexpr float R1 = .75f; // main radius
			constexpr float R2 = .15f; // tube radius

			constexpr uint32_t U_STEPS = 20;
			constexpr uint32_t V_STEPS = 16;

			// texture repeats around torus
			constexpr float V_REPEATS = 2.f;
			constexpr float U_REPEATS = int(V_REPEATS / R2 * R1 + .999f); // approx sqr rounded up

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				// convert steps to angles
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.f * float (M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.f * float (M_PI);

				vertices.emplace_back( PosNorTexVertex{
					.Position {
						.x = (R1 + R2 * std::cos(va)) * std::cos(ua),
						.y = (R1 + R2 * std::cos(va)) * std::sin(ua),
						.z = R2 * std::sin(va),
					},
					.Normal {
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord {
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					emplace_vertex(ui, vi);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui, vi+1);

					emplace_vertex(ui, vi+1);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui+1, vi+1);

				}
			}

			torus_vertices.count = uint32_t(vertices.size() - torus_vertices.first);
		}

		{
			sphere_vertices.first = uint32_t(vertices.size());

			constexpr float A = 1.25f;
			constexpr float B = .75f;
			constexpr float C = .5f;
			constexpr float C_x = 0.f;
			constexpr float C_y = 0.f;
			constexpr float C_z = .5f;

			constexpr uint32_t V_STEPS = 40; // phi
			constexpr uint32_t U_STEPS = 2 * V_STEPS; // theta

			constexpr float V_REPEATS = 4.f;
			constexpr float U_REPEATS = 2 * V_REPEATS;

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * float(M_PI) * 2.f;

				vertices.emplace_back( PosNorTexVertex{
					.Position {
						.x = A * std::sin(va) * std::cos(ua) + C_x,
						.y = B * std::sin(va) * std::sin(ua) + C_y,
						.z = C * std::cos(va) + C_z,
					},
					.Normal {
						.x = std::sin(va) * std::cos(ua),
						.y = std::sin(va) * std::sin(ua),
						.z = std::cos(va),
					},
					.TexCoord {
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					emplace_vertex(ui, vi);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui, vi+1);

					emplace_vertex(ui, vi+1);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui+1, vi+1);
				}
			}

			sphere_vertices.count = uint32_t(vertices.size() - sphere_vertices.first);
		}

		size_t bytes = vertices.size() * sizeof(vertices[0]);

		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		// copy data to buf
		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}
}

Tutorial::~Tutorial() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	rtg.helpers.destroy_buffer(std::move(object_vertices));

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);

		if (workspace.lines_vertices_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
		}
		if (workspace.lines_vertices.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
		}

		if (workspace.Camera_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
		}
		if (workspace.Camera.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}
	}
	workspaces.clear();

	if (descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = nullptr; 
		// also frees descriptor sets allocated from pool
	}

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	objects_pipeline.destroy(rtg);

	refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
}

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	//[re]create framebuffers:
	refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}

void Tutorial::destroy_framebuffers() {
	refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}


void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	//get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];

	VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	// reset the command buffer (clear old commands)
	VK ( vkResetCommandBuffer(workspace.command_buffer, 0) );
	{ // record
		VkCommandBufferBeginInfo begin_info {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		VK( vkBeginCommandBuffer(workspace.command_buffer, &begin_info) );
	}

	if (!lines_vertices.empty()) { // upload lines vertices
		// [re-]allocate line bufs in needed
		size_t needed_bytes = lines_vertices.size() * sizeof(lines_vertices[0]);
		if (workspace.lines_vertices_src.handle == VK_NULL_HANDLE || workspace.lines_vertices_src.size < needed_bytes) {
			// round to next mult of 4k to avoid realloc continuously if vert count grows slowly
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
			if (workspace.lines_vertices_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
			}
			if (workspace.lines_vertices.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
			}

			workspace.lines_vertices_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // going to have GPU copy from this mem
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible mem, coherent (no special sync needed)
				Helpers::Mapped // get a pointer to the mem
			);
			workspace.lines_vertices = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as vertex buf, also going to have GPU into this mem
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // GPU-local mem
				Helpers::Unmapped // don't get a pointer to the mem
			);

			std::cout << "Re-allocated lines buffers to " << new_bytes << " bytes." << std::endl;
		}

		assert(workspace.lines_vertices_src.size == workspace.lines_vertices.size);
		assert(workspace.lines_vertices_src.size >= needed_bytes);
		
		{
			// host-side copy into lines_vertices_src
			assert(workspace.lines_vertices_src.allocation.mapped);
			std::memcpy(workspace.lines_vertices_src.allocation.data(), lines_vertices.data(), needed_bytes);
			// device-side copy from lines_vertices_src -> lines_vertices
			VkBufferCopy copy_region{
				.srcOffset = 0,
				.dstOffset = 0,
				.size = needed_bytes,
			};
			vkCmdCopyBuffer(workspace.command_buffer, workspace.lines_vertices_src.handle, workspace.lines_vertices.handle, 1, &copy_region);
		}
		
		{ // upload cam info
			LinesPipeline::Camera camera{
				.CLIP_FROM_WORLD = CLIP_FROM_WORLD
			};
			assert(workspace.Camera_src.size == sizeof(camera));

			// host-side copy into Camera_src
			memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));
			
			// add device-side copy from Camera_src -> Camera
			assert(workspace.Camera_src.size == workspace.Camera.size);
			VkBufferCopy copy_region{
				.srcOffset = 0,
				.dstOffset = 0,
				.size = workspace.Camera_src.size,
			};

			vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &copy_region);
		}
	}

	{ // mem barrier to make sure copies complete before rendering happens
		VkMemoryBarrier memory_barrier{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		};

		vkCmdPipelineBarrier( workspace.command_buffer, 
			VK_PIPELINE_STAGE_TRANSFER_BIT, // srcStageMask
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // dstStageMask
			0, // dependencyFlags
			1, &memory_barrier, // memoryBarriers (count, data)
			0, nullptr, // bufferMemoryBarriers (count, data)
			0, nullptr // imageMemoryBarriers (count, data)
		);
	}

	{ // render pass
		std::array< VkClearValue, 2 > clear_values {
			VkClearValue{ .color{ .float32{.5f, .25f, 1.f, 1.f} } },
			VkClearValue{ .depthStencil{ .depth = 1.f, .stencil = 0 } },
		};

		VkRenderPassBeginInfo begin_info {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = render_pass,
			.framebuffer = framebuffer,
			.renderArea {
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			},
			.clearValueCount = uint32_t(clear_values.size()),
			.pClearValues = clear_values.data(),
		};

		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	
		{ // set scissor rect
			VkRect2D scissor {
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
		}		

		{ // configure viewport transform
			VkViewport viewport {
				.x = 0.f,
				.y = 0.f,
				.width = float(rtg.swapchain_extent.width),
				.height = float(rtg.swapchain_extent.height),
				.minDepth = 0.f,
				.maxDepth = 1.f,
			};
			vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
		}

		{ // draw w background pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);
			
			{ // push time
				BackgroundPipeline::Push push {
					.time = float(time)
				};
				vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
			}
			
			vkCmdDraw(workspace.command_buffer, 3, 1, 0 ,0);
		}

		{ // draw w lines pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);
			{ // use lines_vertices (offset 0) as vert buffer binding 0
				std::array< VkBuffer, 1 > vertex_buffers{ workspace.lines_vertices.handle };
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ // bind Camera descriptor set
				std::array< VkDescriptorSet, 1 > descriptor_sets{
					workspace.Camera_descriptors, // 0: Camera
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer, // command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
					lines_pipeline.layout, // pipeline layout
					0, // first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), // descriptor sets count, ptr
					0, nullptr // dynamic sets count, ptr
				);
			}

			// draw lines vertices
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}

		{ // draw w objects pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);

			{ // use object_vertices (offset 0) as vert buf binding 0
				std::array< VkBuffer, 1 > vertex_buffers{ object_vertices.handle };
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			// cam descriptor set still bound

			// draw verts
			vkCmdDraw(workspace.command_buffer, uint32_t(object_vertices.size / sizeof(ObjectsPipeline::Vertex)), 1, 0, 0);
		}

		vkCmdEndRenderPass(workspace.command_buffer);
	}

	//end rec
	VK ( vkEndCommandBuffer(workspace.command_buffer) );

	//submit `workspace.command buffer` for the GPU to run:
	refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
}


void Tutorial::update(float dt) {
	time = std::fmod(time + dt, 60.f);

	{ //camera orbiting the origin:
		float ang = float(M_PI) * 2.0f * 10.0f * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
			60.0f * float(M_PI) / 180.0f, //vfov
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
			0.1f, //near
			1000.0f //far
		) * look_at(
			3.0f * std::cos(ang), 3.0f * std::sin(ang), 1.0f, //eye
			0.0f, 0.0f, 0.5f, //target
			0.0f, 0.0f, 1.0f //up
		);
	}

	{ // make cylinder
		lines_vertices.clear();
		static size_t max_count = 0;
		constexpr int lines = 20;

		int side_lines = lines;
		if (side_lines < 0) side_lines = 0;

		int face_lines = 2 * lines;
		if (face_lines < 0) face_lines = 0;

		size_t count = size_t(2 * side_lines + 2 * face_lines * 2);

		if (count > max_count) max_count = count;
		lines_vertices.reserve(max_count);

		constexpr float Line_len = 3.f;
		// cylinder side_lines
		for (int i = 0; i < side_lines; ++i) {
			float angle = 2.f * (float)M_PI / (float)side_lines * i; 
			float x = std::cos(angle);
			float y = std::sin(angle);
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = Line_len *.5f},
				.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff },
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = -Line_len * .5f},
				.Color{ .r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff },
			});
		}

		for (int i = 0; i < face_lines; ++i) {
			float angle = 2.f * (float)M_PI / (float)side_lines * i; 
			float x = std::cos(angle);
			float y = std::sin(angle);

			// circle face 1
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = -Line_len * .5f},
				.Color{ .r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff },
				//.Color{ .r = uint8_t(0xff * std::sin(2.f * time)), .g = uint8_t(0xff * sinT), .b = uint8_t(0xff * std::cos(2.f * time)), .a = 0xff },
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = -x, .y = y, .z = -Line_len * .5f},
				.Color{ .r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff },
				//.Color{ .r = uint8_t(0xff * std::sin(2.f * time)), .g = uint8_t(0xff * sinT), .b = uint8_t(0xff * std::cos(2.f * time)), .a = 0xff },
			});

			// circle face 2
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = Line_len * .5f},
				.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff },
				//.Color{ .r = uint8_t(0xff * std::sin(2.f * time)), .g = uint8_t(0xff * sinT), .b = uint8_t(0xff * std::cos(2.f * time)), .a = 0xff },
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = -x, .y = y, .z = Line_len * .5f},
				.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff },
				//.Color{ .r = uint8_t(0xff * std::sin(2.f * time)), .g = uint8_t(0xff * sinT), .b = uint8_t(0xff * std::cos(2.f * time)), .a = 0xff },
			});
		}

		assert(lines_vertices.size() == count);
	}
}


void Tutorial::on_input(InputEvent const &) {
}
