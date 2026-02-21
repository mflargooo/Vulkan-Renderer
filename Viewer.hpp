#pragma once

#include "PosColVertex.hpp"
#include "PosNorTexVertex.hpp"
#include "PosNorTanTexVertex.hpp"

#include "RTG.hpp"

#include <s72/S72.hpp>

struct Viewer : RTG::Application {

	Viewer(RTG &);
	Viewer(Viewer const &) = delete; //you shouldn't be copying this object
	~Viewer();

	//kept for use in destructor:
	RTG &rtg;

	//--------------------------------------------------------------------
	//Resources that last the lifetime of the application:

	//chosen format for depth buffer:
	VkFormat depth_format{};
	//Render passes describe how pipelines write to images:
	VkRenderPass render_pass = VK_NULL_HANDLE;

	//Pipelines:
	struct BackgroundPipeline {
		// no descriptor set layouts

		struct Push {
			float time;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// no vertex bindings

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} background_pipeline;

	struct LinesPipeline {
		// descriptor set layouts
		VkDescriptorSetLayout set0_Camera = VK_NULL_HANDLE;

		// types for descriptors
		struct Camera {
			glm::mat4 CLIP_FROM_WORLD;
			glm::vec3 EYE_WORLD;
			float padding_;
		};
		static_assert(sizeof(Camera) == 16*4 + 4*3 + 4, "camera buffer structure is packed.");

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosColVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} lines_pipeline;

	struct ObjectsPipeline {
		// descriptor set layouts
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_TEXTURE = VK_NULL_HANDLE;
		VkDescriptorSetLayout set3_CUBEMAPS = VK_NULL_HANDLE;

		// types for descriptors
		struct World {
			struct { float x, y, z, padding_; } SKY_DIRECTION;
			struct { float r, g, b, padding_; } SKY_ENERGY;
			struct { float x, y, z, padding_; } SUN_DIRECTION;
			struct { float r, g, b, padding_; } SUN_ENERGY;
		};
		static_assert(sizeof(World) == 4*4 + 4*4 + 4*4 + 4*4, "World is the expected size.");
		
		struct Transform {
			glm::mat4 CLIP_FROM_LOCAL;
			glm::mat4 WORLD_FROM_LOCAL;
			glm::mat4 WORLD_FROM_LOCAL_NORMAL;
		};
		static_assert(sizeof(Transform) == 16*4 + 16*4 + 16*4, "Transform is the expected size.");

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosNorTanTexVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} objects_pipeline;

	//pools from which per-workspace things are allocated:
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	//workspaces hold per-render resources:
	struct Workspace {
		VkCommandBuffer command_buffer = VK_NULL_HANDLE; //from the command pool above; reset at the start of every render.

		// location for lines data (streamed to GPU per frame)
		Helpers::AllocatedBuffer lines_vertices_src; // host coherent; mapped
		Helpers::AllocatedBuffer lines_vertices; // device-local

		// location for LinesPipeline::Camera data (streamed to GPU per frame)
		Helpers::AllocatedBuffer Camera_src; // host coherent; mapped
		Helpers::AllocatedBuffer Camera; // device-local
		VkDescriptorSet Camera_descriptors; // references Camera
		
		// location for ObjectsPipeline::World data (streamed to GPU per frame)
		Helpers::AllocatedBuffer World_src; // host coherent; mapped
		Helpers::AllocatedBuffer World; // device-local
		VkDescriptorSet World_descriptors; // references World

		// location for ObjectsPipeline::Transforms data (streamed to GPU per frame)
		Helpers::AllocatedBuffer Transforms_src; // host coherent; mapped
		Helpers::AllocatedBuffer Transforms; // device-local
		VkDescriptorSet Transforms_descriptors; // references Transforms
	};
	std::vector< Workspace > workspaces; 

	//-------------------------------------------------------------------
	//static scene resources:

	// Helpers::AllocatedBuffer object_vertices;

	struct ObjectVertices {
		uint32_t first = 0;
		uint32_t count = 0;
	};
	ObjectVertices plane_vertices;
	ObjectVertices torus_vertices;
	ObjectVertices sphere_vertices;

	std::vector< Helpers::AllocatedImage > textures;
	std::vector< VkImageView > texture_views;
	VkSampler texture_sampler = VK_NULL_HANDLE;
	VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
	std::vector< VkDescriptorSet > texture_descriptors; // allocated from texture_descriptor_pool

	std::vector< Helpers::AllocatedImage > cubemaps;
	std::vector< VkImageView > cubemap_views;
	VkSampler cubemap_sampler = VK_NULL_HANDLE;
	VkDescriptorPool cubemap_descriptor_pool = VK_NULL_HANDLE;
	std::vector< VkDescriptorSet > cubemap_descriptors; // allocated from cubemap_descriptor_pool

	//--------------------------------------------------------------------
	//Resources that change when the swapchain is resized:

	virtual void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;

	Helpers::AllocatedImage swapchain_depth_image;
	VkImageView swapchain_depth_image_view = VK_NULL_HANDLE;
	std::vector< VkFramebuffer > swapchain_framebuffers;
	//used from on_swapchain and the destructor: (framebuffers are created in on_swapchain)
	void destroy_framebuffers();

	//--------------------------------------------------------------------
	//Resources that change when time passes or the user interacts:

	virtual void update(float dt) override;
	virtual void on_input(InputEvent const &) override;

	// modal action, intercepts inputs
	std::function< void(InputEvent const &) > action;
	
	float time = 0.f;

	// selecting between cams
	enum class CameraMode {
		Scene = 0,
		Free = 1,
		Debug = 2,
	} last_mode = CameraMode::Debug, camera_mode = CameraMode(rtg.configuration.camera.compare("") == 0);

	struct OrbitCamera { // used when camera_mode == CameraMode::Free
		float target_x = 0.f, target_y = 0.f, target_z = 0.f; // cam looking + orbiting
		float radius = 2.f; // dist from cam to target
		float azimuth = 0.f; // ccw angle around z between x and cam dir (rads)
		float elevation = .25f * float(M_PI); // angle from xy plane to cam dir (rads)

		float fov = 60.f / 180.f * float(M_PI); // vfov (rad)
		float near = .1f; // near clip
		float far = 1000.f; // far clip
	} free_camera, debug_camera;

	struct Camera {
		ObjectsPipeline::Transform transform;
		float aspect;
		float vfov;
		float near;
		float far;
	};
	std::map< std::string, Camera > scene_cameras;
	std::map< std::string, Camera >::iterator selected_scene_camera;
	
	glm::mat4 debug_CLIP_FROM_WORLD;
	glm::mat4 CLIP_FROM_WORLD;

	std::vector< LinesPipeline::Vertex > lines_vertices;

	ObjectsPipeline::World world;

	S72 s72;

	struct AABB {
		glm::vec3 min;
		glm::vec3 max;
	};
	std::unordered_map< Helpers::AllocatedBuffer *, AABB > aabbs;

	struct ObjectInstance {
		ObjectVertices vertices;
		Helpers::AllocatedBuffer *buffer = nullptr;
		ObjectsPipeline::Transform transform;
		uint32_t texture = 0;
		uint32_t material = 0; // 0 - lambert, 1 - mirror
	};
	std::vector< ObjectInstance > object_instances;

	// DataFile path -> buffer holding data obtained from DataFile path
	std::unordered_map< std::string, Helpers::AllocatedBuffer > scene_vertices;
	
	std::unordered_map < Helpers::AllocatedBuffer *, std::vector < ObjectInstance * > > drawables;

	// texture path -> *_view id
	std::unordered_map< std::string, uint32_t > tex_ids;
	std::unordered_map< std::string, uint32_t > cubemap_ids;
	
	struct Material {
		int albedo_id; // always exists
		int normal_id; // may not exist
		int displacement_id; // may not exist
	};
	std::unordered_map< std::string, Material > materials;

	int load_scene(const std::string &file);
	void parse_scene_graph();

	bool cull_frustrum(glm::vec3 frustrum[8], glm::vec3 bbox[8]);

	float animation_time = 0.f;
	float max_animation_time = 0.f;
	int animation_step = 1;
	bool pause_animation = false;

	//--------------------------------------------------------------------
	//Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};
