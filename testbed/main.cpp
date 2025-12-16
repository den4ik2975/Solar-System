#include <cstdint>
#include <algorithm>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>
#include <random>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <array>

#include <veekay/veekay.hpp>

#include <vulkan/vulkan_core.h>
#include <imgui.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace veekay {
namespace graphics {

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(veekay::app.vk_physical_device, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) &&
		    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	std::cerr << "Failed to find suitable memory type!" << std::endl;
	veekay::app.running = false;
	return 0;
}

} // namespace graphics
} // namespace veekay

namespace {

constexpr uint32_t max_models = 1024;
constexpr const char* kShaderDir = "/mnt/d/Denis/Documents/Lab2/Lab2/shaders/";

struct Vertex {
	veekay::vec3 position;
	veekay::vec3 normal;
	veekay::vec2 uv;
	// NOTE: You can add more attributes
};

struct DirectionalLight {
	veekay::vec4 direction;
	veekay::vec4 color;
};

struct PointLight {
	veekay::vec4 position;
	veekay::vec4 color;
};

struct Spotlight {
	veekay::vec4 position;
	veekay::vec4 direction;
	veekay::vec4 color;
	veekay::vec4 cone_angles;     // внутренний/внешний угол
};

struct LightingData {
	veekay::vec4 ambient_color;
	DirectionalLight directional_light;
};

struct SceneUniforms {
	veekay::mat4 view_projection;
	veekay::vec4 camera_position; 
	LightingData lighting;
	veekay::vec4 time;
	veekay::mat4 shadow_view_projection_dir;
	veekay::mat4 shadow_view_projection_spot;
	veekay::vec4 shadow_meta; // x: spot shadow enabled, y: spot index, z: point shadow far, w: point shadow near
};

struct ModelUniforms {
	veekay::mat4 model;
	veekay::vec4 albedo_color;
	veekay::vec4 specular_color;
	veekay::vec4 material;
};

struct ShadowMap {
	VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
	uint32_t size = 2048;
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VkShaderModule shader = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	veekay::graphics::Buffer* matrix_buffer = nullptr;
	veekay::mat4 matrix{};
	VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct PointShadow {
	VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
	uint32_t size = 1024;
	float near_plane = 0.1f;
	float far_plane = 80.0f;
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView cube_view = VK_NULL_HANDLE;
	std::array<VkImageView, 6> face_views{};
	VkSampler sampler = VK_NULL_HANDLE;
	VkShaderModule shader = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	veekay::graphics::Buffer* matrix_buffer = nullptr;
	VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Mesh{
	veekay::graphics::Buffer* vertex_buffer;
	veekay::graphics::Buffer* index_buffer;
	uint32_t indices;
};

struct Transform {
	veekay::vec3 position = {};
	veekay::vec3 scale = {1.0f, 1.0f, 1.0f};
	veekay::vec3 rotation = {};

	// NOTE: Model matrix (translation, rotation and scaling)
	veekay::mat4 matrix() const;
};

	struct GameObject {
	Transform transform;

	void animate_rotate(veekay::vec3 direction, double time)
	{
		transform.rotation.x += direction.x * sin(time);
		transform.rotation.y += direction.y *sin(time);
		transform.rotation.z += direction.z *sin(time);
	}
};

struct Material
{
	veekay::vec3 albedo_color;
	veekay::vec3 specular_color = {0.5f, 0.5f, 0.5f};  // Цвет блика
	float shininess = 32.0f;  // Степень блеска (экспонента)
	float opacity = 1.0f;     // Альфа для смешивания (1 — непрозрачный)
	float warp_strength = 0.0f; // Для искажения UV (для базы)
	float emissive_pulse = 0.0f; // Доп. пульсация эмиссии/текстуры
};

Material Standart = {
	.albedo_color = veekay::vec3{0.6f, 0.6f, 0.6f},
	.specular_color = {0.5f, 0.5f, 0.5f}, // Цвет блика
	.shininess = 50.0f  // Степень блеска
};

Material MettalicBlue = {
	.albedo_color = veekay::vec3{0.2f, 0.2f, 0.8f},
	.specular_color = veekay::vec3{0.2f, 0.2f, 0.8f},  // Высокий блик как у металла
	.shininess = 150.0f
};

Material MateGreen = {
	.albedo_color = veekay::vec3{0.2f, 0.8f, 0.2f},
	.specular_color = veekay::vec3{0.1f, 0.1f, 0.1f},  // Низкий блик как у матовой поверхности
	.shininess = 1.0f     // Небольшая степень блеска
};

Material MediumRed = {
	.albedo_color = veekay::vec3{0.8f, 0.2f, 0.2f},
	.specular_color = veekay::vec3{0.3f, 0.3f, 0.3f},  // Средний блик
	.shininess = 32.0f
};

Material YellowSun = {
	.albedo_color = veekay::vec3{1.0f, 0.9f, 0.4f},
	.specular_color = veekay::vec3{1.0f, 0.9f, 0.6f},
	.shininess = -1.0f // отрицательное значение включает эмиссию
};

Material MercuryMat = {
	.albedo_color = veekay::vec3{0.7f, 0.7f, 0.7f},
	.specular_color = veekay::vec3{0.4f, 0.4f, 0.4f},
	.shininess = 64.0f
};

Material VenusMat = {
	.albedo_color = veekay::vec3{0.9f, 0.7f, 0.4f},
	.specular_color = veekay::vec3{0.4f, 0.3f, 0.2f},
	.shininess = 64.0f
};

Material EarthMat = {
	.albedo_color = veekay::vec3{0.2f, 0.4f, 0.9f},
	.specular_color = veekay::vec3{0.3f, 0.3f, 0.3f},
	.shininess = 96.0f
};

Material MarsMat = {
	.albedo_color = veekay::vec3{0.8f, 0.4f, 0.2f},
	.specular_color = veekay::vec3{0.3f, 0.2f, 0.1f},
	.shininess = 64.0f
};

Material JupiterMat = {
	.albedo_color = veekay::vec3{0.9f, 0.7f, 0.5f},
	.specular_color = veekay::vec3{0.5f, 0.4f, 0.3f},
	.shininess = 96.0f
};

Material SaturnMat = {
	.albedo_color = veekay::vec3{0.9f, 0.8f, 0.6f},
	.specular_color = veekay::vec3{0.5f, 0.4f, 0.2f},
	.shininess = 96.0f
};

Material UranusMat = {
	.albedo_color = veekay::vec3{1.0f, 1.0f, 1.0f},
	.specular_color = veekay::vec3{0.3f, 0.4f, 0.4f},
	.shininess = 96.0f
};

Material NeptuneMat = {
	.albedo_color = veekay::vec3{1.0f, 1.0f, 1.0f},
	.specular_color = veekay::vec3{0.2f, 0.3f, 0.6f},
	.shininess = 96.0f
};

Material MoonMat = {
	.albedo_color = veekay::vec3{0.8f, 0.8f, 0.8f},
	.specular_color = veekay::vec3{0.3f, 0.3f, 0.3f},
	.shininess = 32.0f
};

Material RingMat = {
	.albedo_color = veekay::vec3{0.8f, 0.7f, 0.6f},
	.specular_color = veekay::vec3{0.4f, 0.3f, 0.2f},
	.shininess = 32.0f
};

Material StarMat = {
	.albedo_color = veekay::vec3{1.0f, 1.0f, 1.0f},
	.specular_color = veekay::vec3{0.0f, 0.0f, 0.0f},
	.shininess = -1.0f // эмиссивные звёзды
};

Material CometHeadMat = {
	.albedo_color = veekay::vec3{0.8f, 0.9f, 1.0f},
	.specular_color = veekay::vec3{0.0f, 0.0f, 0.0f},
	.shininess = -1.0f // эмиссивная голова
};

Material CometTailMat = {
	.albedo_color = veekay::vec3{0.4f, 0.7f, 1.0f},
	.specular_color = veekay::vec3{0.0f, 0.0f, 0.0f},
	.shininess = -1.0f // эмиссивный хвост
};

Material GlassMat = {
	.albedo_color = veekay::vec3{0.51f, 0.81f, 0.92f},
	.specular_color = veekay::vec3{0.9f, 0.9f, 1.0f},
	.shininess = 256.0f, // «стеклянный» блеск (пока без прозрачности)
	.opacity = 0.25f
};

Material BaseMat = {
	.albedo_color = veekay::vec3{1.0f, 1.0f, 1.0f},
	.specular_color = veekay::vec3{0.25f, 0.22f, 0.2f},
	.shininess = 32.0f
};

Material SpotlightMat = {
	.albedo_color = veekay::vec3{0.25f, 0.25f, 0.25f},
	.specular_color = veekay::vec3{0.15f, 0.15f, 0.15f},
	.shininess = 32.0f
};

struct TextureSet {
	veekay::graphics::Texture* albedo = nullptr;
	veekay::graphics::Texture* specular = nullptr;
	veekay::graphics::Texture* emissive = nullptr;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};

struct Model : GameObject {
	Mesh mesh;
	std::string title;
	Material material;
	size_t texture_set = SIZE_MAX;
};

struct Camera {
	constexpr static float default_fov = 60.0f;
	constexpr static float default_near_plane = 0.01f;
	constexpr static float default_far_plane = 100.0f;

	veekay::vec3 position = {};
	veekay::vec3 rotation = {};

	float fov = default_fov;
	float near_plane = default_near_plane;
	float far_plane = default_far_plane;

	// NOTE: View matrix of camera (inverse of a transform)
	veekay::mat4 view() const;

	bool LookAt = true;
	veekay::vec3 target_position = {0, 0, 0};
	veekay::mat4 lookAt_view() const;

	// NOTE: View and projection composition
	veekay::mat4 view_projection(float aspect_ratio) const;
};

struct CameraState {
	veekay::vec3 position{};
	veekay::vec3 rotation{};
	veekay::vec3 target{};
};

struct Orbit {
	// 0.3 Эллиптическая орбита: хранит полуоси, поворот, угловую скорость и родителя для привязки (например, Луна к Земле)
	size_t model_index;
	size_t parent_index; // SIZE_MAX = нет родителя
	float a;
	float b;
	float rotation_rad;
	float omega;
	float phase;
};

struct StarInstance {
	size_t model_index;
	float time = 0.0f;
	float lifetime = 1.0f;
	veekay::vec3 base_color;
};

struct CometInstance {
	size_t model_index = SIZE_MAX; // вытянутая форма кометы целиком
	float radius = 0.1f;
	float tail_base_stretch = 1.0f;
	float fade_in = 1.0f;
	float fade_out = 1.0f;
	veekay::vec3 start{};
	veekay::vec3 end{};
	veekay::vec3 dir{};
	float duration = 1.0f;
	float t = 0.0f;
	float tail_length = 1.0f;
	uint32_t light_slot = 0; // индекс в point_lights (для головы)
};

	// NOTE: Scene objects
	inline namespace {
		Camera camera{
			.position = {0.0f, -2.0f, -40.0f},
			.rotation = {0.0f, 0.0f, 0.0f}  // x pitch = 0°, y  yaw = 0°, roll = 0°
		};

	CameraState lookat_state{
		.position = camera.position,
		.rotation = camera.rotation,
		.target = camera.target_position,
	};

	CameraState transform_state{
		.position = camera.position,
		.rotation = camera.rotation,
		.target = camera.target_position,
	};

	bool lookat_state_valid = true;
	bool transform_state_valid = true;

std::vector<Model> models;
std::vector<Orbit> orbits;
constexpr float globe_radius = 15.0f;
constexpr float base_height = 2.0f;
constexpr float base_radius = 12.0f;
constexpr float projector_radius = 0.2f;
constexpr float projector_length = 1.0f;
const std::string asset_root = "D:/Denis/Documents/Lab2/Lab2/";
size_t glass_model_index = SIZE_MAX;
size_t base_model_index = SIZE_MAX;
size_t sun_model_index = SIZE_MAX;
size_t saturn_ring_model_index = SIZE_MAX;
size_t spotlight_model_indices[8] = {
	SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX,
	SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX
};
std::vector<TextureSet> texture_sets;
size_t default_texture_set = SIZE_MAX;
size_t white_texture_set = SIZE_MAX;
std::unordered_map<std::string, veekay::graphics::Texture*> texture_cache;
std::vector<std::pair<size_t, float>> spin_targets; // model index, deg/sec
size_t shaft_model_index = SIZE_MAX;
size_t support_model_indices[4] = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
}

struct Plane : Model {
	Plane(veekay::vec3 position, Material material = Standart , std::string title = "None")
	{
		std::vector<Vertex> vertices = {
			{{-5.0f, 0.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
			{{5.0f, 0.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
			{{5.0f, 0.0f, -5.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
			{{-5.0f, 0.0f, -5.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
		};

		std::vector<uint32_t> indices = {
			0, 1, 2, 2, 3, 0
		};

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		Model m;
		m.transform.position = position;
		m.mesh = mesh;
		m.material = material,
		m.title = title;
		m.texture_set = default_texture_set;

		models.push_back(m);
	}
};

struct Cube : Model {
	Cube(veekay::vec3 position, Material material = Standart, std::string title = "None", veekay::vec3 scale = {1.f,1.f,1.f})
	{
		std::vector<Vertex> vertices = {
			{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
			{{+0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
			{{+0.5f, +0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
			{{-0.5f, +0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},

			{{+0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
			{{+0.5f, -0.5f, +0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
			{{+0.5f, +0.5f, +0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
			{{+0.5f, +0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

			{{+0.5f, -0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
			{{-0.5f, -0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
			{{-0.5f, +0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
			{{+0.5f, +0.5f, +0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},

			{{-0.5f, -0.5f, +0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
			{{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
			{{-0.5f, +0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
			{{-0.5f, +0.5f, +0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

			{{-0.5f, -0.5f, +0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
			{{+0.5f, -0.5f, +0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
			{{+0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
			{{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},

			{{-0.5f, +0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
			{{+0.5f, +0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
			{{+0.5f, +0.5f, +0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
			{{-0.5f, +0.5f, +0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
		};

		std::vector<uint32_t> indices = {
			0, 1, 2, 2, 3, 0,
			4, 5, 6, 6, 7, 4,
			8, 9, 10, 10, 11, 8,
			12, 13, 14, 14, 15, 12,
			16, 17, 18, 18, 19, 16,
			20, 21, 22, 22, 23, 20,
		};

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		Model m;
		m.transform.position = position;
		m.transform.scale = scale;
		m.mesh = mesh;
		m.material = material,
		m.title = title;
		m.texture_set = default_texture_set;

		models.push_back(m);
	}
};

struct Sphere : Model {
	// 0.1 Процедурная генерация сферы: вершины строятся по широте/долготе, нормали совпадают с направлением радиуса
	Sphere(veekay::vec3 position, float radius, uint32_t stacks, uint32_t slices, Material material, std::string title = "Sphere", veekay::vec3 scale = {1.f,1.f,1.f})
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t i = 0; i <= stacks; ++i) {
			float v = float(i) / float(stacks);
			float phi = v * float(M_PI);

			for (uint32_t j = 0; j <= slices; ++j) {
				float u = float(j) / float(slices);
				float theta = u * float(M_PI) * 2.0f;

				float x = std::sin(phi) * std::cos(theta);
				float y = std::cos(phi);
				float z = std::sin(phi) * std::sin(theta);

				veekay::vec3 normal = {x, y, z};
				veekay::vec3 pos = {x * radius, y * radius, z * radius};
				vertices.push_back({pos, normal, {u, v}});
			}
		}

		for (uint32_t i = 0; i < stacks; ++i) {
			for (uint32_t j = 0; j < slices; ++j) {
				uint32_t first = i * (slices + 1) + j;
				uint32_t second = first + slices + 1;

				indices.push_back(first);
				indices.push_back(second);
				indices.push_back(first + 1);

				indices.push_back(second);
				indices.push_back(second + 1);
				indices.push_back(first + 1);
			}
		}

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		Model m;
		m.transform.position = position;
		m.transform.scale = scale;
		m.mesh = mesh;
		m.material = material,
		m.title = title;
		m.texture_set = default_texture_set;

		models.push_back(m);
	}
};

// Процедурная "вытянутая сфера": первая половина — обычная сфера, вторая плавно вытягивается в хвост
Mesh make_stretched_sphere_mesh(float radius, float stretch, uint32_t stacks, uint32_t slices) {
	Mesh mesh{};
	struct ProfilePoint {
		float r;
		float z;
	};

	std::vector<ProfilePoint> profile(stacks + 1);
	auto smoothstep = [](float x) {
		return x * x * (3.0f - 2.0f * x);
	};

	for (uint32_t i = 0; i <= stacks; ++i) {
		float t = float(i) / float(stacks);
		if (t <= 0.5f) {
			float phi = t * float(M_PI); // 0..pi/2
			float r = std::sin(phi) * radius;
			float z = std::cos(phi) * radius - radius; // нос в z=0
			profile[i] = {r, z};
		} else {
			float k = (t - 0.5f) / 0.5f; // 0..1
			float ease = smoothstep(k);
			float r = radius * (1.0f - ease);
			float z = -radius - ease * stretch;
			profile[i] = {r, z};
		}
	}

	std::vector<float> r_deriv(stacks + 1, 0.0f);
	for (uint32_t i = 0; i <= stacks; ++i) {
		if (i == 0) {
			float dz = profile[i + 1].z - profile[i].z;
			if (std::abs(dz) < 1e-4f) dz = (dz >= 0.0f) ? 1e-4f : -1e-4f;
			r_deriv[i] = (profile[i + 1].r - profile[i].r) / dz;
		} else if (i == stacks) {
			float dz = profile[i].z - profile[i - 1].z;
			if (std::abs(dz) < 1e-4f) dz = (dz >= 0.0f) ? 1e-4f : -1e-4f;
			r_deriv[i] = (profile[i].r - profile[i - 1].r) / dz;
		} else {
			float dz = profile[i + 1].z - profile[i - 1].z;
			if (std::abs(dz) < 1e-4f) dz = (dz >= 0.0f) ? 1e-4f : -1e-4f;
			r_deriv[i] = (profile[i + 1].r - profile[i - 1].r) / dz;
		}
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	vertices.reserve((stacks + 1) * (slices + 1));
	indices.reserve(stacks * slices * 6);

	for (uint32_t i = 0; i <= stacks; ++i) {
		float v = float(i) / float(stacks);
		float r = profile[i].r;
		float z = profile[i].z;
		float r_prime = r_deriv[i];

		for (uint32_t j = 0; j <= slices; ++j) {
			float u = float(j) / float(slices);
			float theta = u * float(M_PI) * 2.0f;
			float c = std::cos(theta);
			float s = std::sin(theta);

			veekay::vec3 pos = {r * c, r * s, z};
			veekay::vec3 normal = veekay::vec3::normalized({c, s, -r_prime});
			vertices.push_back({pos, normal, {u, v}});
		}
	}

	for (uint32_t i = 0; i < stacks; ++i) {
		for (uint32_t j = 0; j < slices; ++j) {
			uint32_t first = i * (slices + 1) + j;
			uint32_t second = first + slices + 1;

			indices.push_back(first);
			indices.push_back(second);
			indices.push_back(first + 1);

			indices.push_back(second);
			indices.push_back(second + 1);
			indices.push_back(first + 1);
		}
	}

	mesh.vertex_buffer = new veekay::graphics::Buffer(
		vertices.size() * sizeof(Vertex), vertices.data(),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

	mesh.index_buffer = new veekay::graphics::Buffer(
		indices.size() * sizeof(uint32_t), indices.data(),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

	mesh.indices = uint32_t(indices.size());
	return mesh;
}

struct StretchedSphere : Model {
	StretchedSphere(veekay::vec3 position, float radius, float stretch, uint32_t stacks, uint32_t slices, Material material, std::string title = "StretchedSphere")
	{
		mesh = make_stretched_sphere_mesh(radius, stretch, stacks, slices);
		transform.position = position;
		this->material = material;
		this->title = title;
		this->texture_set = default_texture_set;
		models.push_back(*this);
	}
};

struct Cylinder : Model {
	// Цилиндр вдоль оси Z (z = [-height/2, height/2])
	Cylinder(veekay::vec3 position, float radius, float height, uint32_t segments, Material material, std::string title = "Cylinder")
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		float half_h = height * 0.5f;

		// Боковая поверхность
		for (uint32_t i = 0; i <= segments; ++i) {
			float u = float(i) / float(segments);
			float angle = u * float(M_PI) * 2.0f;
			float c = std::cos(angle);
			float s = std::sin(angle);
			veekay::vec3 normal = {c, s, 0.0f};

			vertices.push_back({{radius * c, radius * s, half_h}, normal, {u, 1.0f}});
			vertices.push_back({{radius * c, radius * s, -half_h}, normal, {u, 0.0f}});
		}

		for (uint32_t i = 0; i < segments; ++i) {
			uint32_t top1 = i * 2;
			uint32_t bot1 = top1 + 1;
			uint32_t top2 = top1 + 2;
			uint32_t bot2 = top1 + 3;

			indices.push_back(top1);
			indices.push_back(bot1);
			indices.push_back(top2);

			indices.push_back(bot1);
			indices.push_back(bot2);
			indices.push_back(top2);
		}

		// Верхняя крышка
		uint32_t top_center_idx = static_cast<uint32_t>(vertices.size());
		vertices.push_back({{0.0f, 0.0f, half_h}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f}});
		uint32_t top_start_idx = static_cast<uint32_t>(vertices.size());
		for (uint32_t i = 0; i <= segments; ++i) {
			float u = float(i) / float(segments);
			float angle = u * float(M_PI) * 2.0f;
			float c = std::cos(angle);
			float s = std::sin(angle);
			vertices.push_back({{radius * c, radius * s, half_h}, {0.0f, 0.0f, 1.0f}, {u, 1.0f}});
		}
		for (uint32_t i = 0; i < segments; ++i) {
			uint32_t a = top_start_idx + i;
			uint32_t b = top_start_idx + i + 1;
			indices.push_back(top_center_idx);
			indices.push_back(a);
			indices.push_back(b);
		}

		// Нижняя крышка
		uint32_t bottom_center_idx = static_cast<uint32_t>(vertices.size());
		vertices.push_back({{0.0f, 0.0f, -half_h}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f}});
		uint32_t bottom_start_idx = static_cast<uint32_t>(vertices.size());
		for (uint32_t i = 0; i <= segments; ++i) {
			float u = float(i) / float(segments);
			float angle = u * float(M_PI) * 2.0f;
			float c = std::cos(angle);
			float s = std::sin(angle);
			vertices.push_back({{radius * c, radius * s, -half_h}, {0.0f, 0.0f, -1.0f}, {u, 0.0f}});
		}
		for (uint32_t i = 0; i < segments; ++i) {
			uint32_t a = bottom_start_idx + i;
			uint32_t b = bottom_start_idx + i + 1;
			indices.push_back(bottom_center_idx);
			indices.push_back(b);
			indices.push_back(a);
		}

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		transform.position = position;
		this->material = material;
		this->title = title;
		this->texture_set = default_texture_set;
		models.push_back(*this);
	}
};

struct Ring : Model {
	// 0.2 Плоское кольцо (Сатурн): внутренний/внешний радиусы + корректный порядок индексов для обзора сверху
	Ring(veekay::vec3 position, float inner_radius, float outer_radius, uint32_t segments, Material material, std::string title = "Ring")
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t i = 0; i <= segments; ++i) {
			float u = float(i) / float(segments);
			float angle = u * float(M_PI) * 2.0f;
			float c = std::cos(angle);
			float s = std::sin(angle);

			veekay::vec3 normal = {0.0f, 1.0f, 0.0f};

			vertices.push_back({{inner_radius * c, 0.0f, inner_radius * s}, normal, {0.0f, u}});
			vertices.push_back({{outer_radius * c, 0.0f, outer_radius * s}, normal, {1.0f, u}});
		}

		for (uint32_t i = 0; i < segments; ++i) {
			uint32_t start = i * 2;
			indices.push_back(start);
			indices.push_back(start + 2);
			indices.push_back(start + 1);

			indices.push_back(start + 1);
			indices.push_back(start + 2);
			indices.push_back(start + 3);
		}

		mesh.vertex_buffer = new veekay::graphics::Buffer(
			vertices.size() * sizeof(Vertex), vertices.data(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

		mesh.index_buffer = new veekay::graphics::Buffer(
			indices.size() * sizeof(uint32_t), indices.data(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

		mesh.indices = uint32_t(indices.size());

		Model m;
		m.transform.position = position;
		m.mesh = mesh;
		m.material = material,
		m.title = title;
		m.texture_set = default_texture_set;

		models.push_back(m);
	}
};

// Объявления генераторов случайных величин (реализация ниже)
float randf(std::mt19937& rng, float a, float b);
veekay::vec3 random_unit_vec(std::mt19937& rng);
veekay::vec3 random_on_shell(std::mt19937& rng, float radius);

struct StarSpawner {
	float radius;
	void respawn(StarInstance& star, std::mt19937& rng) const {
		veekay::vec3 dir = random_unit_vec(rng);
		star.model_index = star.model_index;
		star.time = 0.0f;
		star.lifetime = randf(rng, 1.0f, 3.0f);
		star.base_color = veekay::vec3{randf(rng, 0.6f, 1.0f), randf(rng, 0.6f, 1.0f), 1.0f};
		models[star.model_index].transform.position = dir * radius;
		models[star.model_index].material.albedo_color = star.base_color;
	}
};

struct CometSpawner {
	float shell_radius;
	float sun_safe_radius;
	float min_duration;
	float max_duration;
	float tail_scale;

	bool sample_path(veekay::vec3& start, veekay::vec3& end, std::mt19937& rng) const {
		for (int i = 0; i < 16; ++i) {
			start = random_on_shell(rng, shell_radius);
			end = random_on_shell(rng, shell_radius);
			veekay::vec3 seg = end - start;
			float seg_len2 = seg.x * seg.x + seg.y * seg.y + seg.z * seg.z;
			if (seg_len2 < 1e-6f) continue;
			float t = -(start.x * seg.x + start.y * seg.y + start.z * seg.z) / seg_len2;
			t = std::max(0.0f, std::min(1.0f, t));
			veekay::vec3 closest = start + seg * t;
			float dist2 = closest.x * closest.x + closest.y * closest.y + closest.z * closest.z;
			if (dist2 >= sun_safe_radius * sun_safe_radius) {
				return true;
			}
		}
		return false;
	}

	void respawn(CometInstance& comet, std::mt19937& rng) const {
		veekay::vec3 start, end;
		if (!sample_path(start, end, rng)) {
			start = {shell_radius, 0.0f, 0.0f};
			end = {-shell_radius, 0.0f, 0.0f};
		}
		comet.start = start;
		comet.end = end;
		comet.dir = veekay::vec3::normalized(end - start);
		comet.t = 0.0f;
		comet.duration = randf(rng, min_duration, max_duration);
		comet.fade_in = randf(rng, 0.8f, 1.5f);
		comet.fade_out = randf(rng, 0.8f, 1.5f);
		float distance = std::sqrt((end - start).x * (end - start).x + (end - start).y * (end - start).y + (end - start).z * (end - start).z);
		comet.tail_length = distance * tail_scale;
	}
};

// NOTE: Vulkan objects
inline namespace {
	VkShaderModule vertex_shader_module;
	VkShaderModule fragment_shader_module;

	VkDescriptorPool descriptor_pool;
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSet descriptor_set;
	VkDescriptorSetLayout texture_descriptor_set_layout;

	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkPipeline pipeline_glass;

	veekay::graphics::Buffer* scene_uniforms_buffer;
	veekay::graphics::Buffer* model_uniforms_buffer;
	veekay::graphics::Buffer* point_lights_buffer;
	veekay::graphics::Buffer* spotlights_buffer;

	veekay::graphics::Texture* missing_texture;
	VkSampler missing_texture_sampler;

	veekay::graphics::Texture* white_texture;
	veekay::graphics::Texture* black_texture;
	VkSampler texture_sampler;
}

float toRadians(float degrees) {
	return degrees * float(M_PI) / 180.0f;
}

float randf(std::mt19937& rng, float a, float b) {
	std::uniform_real_distribution<float> dist(a, b);
	return dist(rng);
}

veekay::graphics::Texture* load_texture_rgba(VkCommandBuffer cmd, const std::string& path, veekay::graphics::Texture* fallback) {
	int w = 0, h = 0, comp = 0;
	stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, STBI_rgb_alpha);
	if (!pixels || w <= 0 || h <= 0) {
		std::cerr << "Failed to load texture: " << path << "\n";
		if (pixels) stbi_image_free(pixels);
		return fallback;
	}

	std::vector<uint8_t> data(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
	std::memcpy(data.data(), pixels, data.size());
	stbi_image_free(pixels);

	try {
		return new veekay::graphics::Texture(cmd, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
		                                     VK_FORMAT_R8G8B8A8_UNORM, data.data());
	} catch (const std::exception& e) {
		std::cerr << "Failed to create texture for " << path << ": " << e.what() << "\n";
		return fallback;
	}
}

veekay::graphics::Texture* make_solid_texture(VkCommandBuffer cmd, uint32_t rgba) {
	return new veekay::graphics::Texture(cmd, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, &rgba);
}

veekay::graphics::Texture* cached_texture(VkCommandBuffer cmd, const std::string& path, veekay::graphics::Texture* fallback) {
	auto resolve = [&](const std::string& p) -> std::string {
		// If path already absolute (Windows drive or starts with /), keep it, else prepend asset_root
		if (p.size() > 2 && ((p[1] == ':' && (p[2] == '\\' || p[2] == '/')) || p[0] == '/' || p[0] == '\\')) {
			return p;
		}
		return asset_root + p;
	};
	std::string full = resolve(path);
	auto it = texture_cache.find(full);
	if (it != texture_cache.end()) {
		return it->second ? it->second : fallback;
	}
	auto tex = load_texture_rgba(cmd, full, fallback);
	texture_cache[full] = tex;
	return tex ? tex : fallback;
}

size_t create_texture_set(VkDevice device, veekay::graphics::Texture* albedo, veekay::graphics::Texture* specular, veekay::graphics::Texture* emissive) {
	TextureSet set;
	set.albedo = albedo ? albedo : missing_texture;
	set.specular = specular ? specular : white_texture;
	set.emissive = emissive ? emissive : black_texture;

	VkDescriptorSetAllocateInfo alloc_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &texture_descriptor_set_layout,
	};

	if (vkAllocateDescriptorSets(device, &alloc_info, &set.descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate texture descriptor set\n";
		return default_texture_set;
	} else {
		VkDescriptorImageInfo infos[3];
		infos[0] = {
			.sampler = texture_sampler,
			.imageView = set.albedo->view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};
		infos[1] = {
			.sampler = texture_sampler,
			.imageView = set.specular->view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};
		infos[2] = {
			.sampler = texture_sampler,
			.imageView = set.emissive->view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		VkWriteDescriptorSet writes[3];
		for (int i = 0; i < 3; ++i) {
			writes[i] = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = set.descriptor_set,
				.dstBinding = static_cast<uint32_t>(i),
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &infos[i]
			};
		}
		vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
	}

	// 3.3 Формируем отдельный набор дескрипторов с тремя текстурами материала
	texture_sets.push_back(set);
	return texture_sets.size() - 1;
}

size_t create_texture_set_from_paths(VkCommandBuffer cmd, const std::string& albedo_path, const std::string& spec_path = std::string(), const std::string& emissive_path = std::string()) {
	veekay::graphics::Texture* albedo = albedo_path.empty() ? missing_texture : cached_texture(cmd, albedo_path, missing_texture);
	veekay::graphics::Texture* spec = spec_path.empty() ? white_texture : cached_texture(cmd, spec_path, white_texture);
	veekay::graphics::Texture* emissive = emissive_path.empty() ? black_texture : cached_texture(cmd, emissive_path, black_texture);
	return create_texture_set(veekay::app.vk_device, albedo, spec, emissive);
}

veekay::vec3 random_unit_vec(std::mt19937& rng) {
	float x = randf(rng, -1.0f, 1.0f);
	float y = randf(rng, -1.0f, 1.0f);
	float z = randf(rng, -1.0f, 1.0f);
	veekay::vec3 v{x, y, z};
	float len = std::sqrt(x * x + y * y + z * z);
	if (len < 1e-5f) return {0.0f, 1.0f, 0.0f};
	return v / len;
}

veekay::vec3 random_on_shell(std::mt19937& rng, float radius) {
	return random_unit_vec(rng) * radius;
}

veekay::mat4 make_look_at(veekay::vec3 eye, veekay::vec3 target, veekay::vec3 up_hint = {0, 1, 0}) {
	// Guard against degenerate forward vector
	veekay::vec3 forward_vec = eye - target;
	float forward_len = veekay::vec3::length(forward_vec);
	if (forward_len < 1e-5f) {
		return veekay::mat4::identity();
	}
	const veekay::vec3 forward = forward_vec / forward_len;

	// Pick an up that is not collinear with forward to avoid NaNs
	veekay::vec3 up = up_hint;
	if (std::abs(veekay::vec3::dot(forward, up)) > 0.999f) {
		up = {0, 0, 1};
		if (std::abs(veekay::vec3::dot(forward, up)) > 0.999f) {
			up = {0, 1, 0};
		}
	}

	veekay::vec3 right = veekay::vec3::normalized(veekay::vec3::cross(up, forward));
	up = veekay::vec3::normalized(veekay::vec3::cross(forward, right));

	const veekay::mat4 basis = {
		right.x, up.x, -forward.x, 0,
		right.y, up.y, -forward.y, 0,
		right.z, up.z, -forward.z, 0,
		0, 0, 0, 1
	};

	return veekay::mat4::translation(-eye) * basis;
}

veekay::mat4 make_ortho(float left, float right, float bottom, float top, float near_plane, float far_plane) {
	veekay::mat4 result = veekay::mat4::identity();

	result[0][0] = 2.0f / (right - left);
	result[1][1] = 2.0f / (top - bottom);
	result[2][2] = 1.0f / (far_plane - near_plane);
	result[3][2] = -near_plane / (far_plane - near_plane);
	result[3][0] = -(right + left) / (right - left);
	result[3][1] = -(top + bottom) / (top - bottom);

	return result;
}

veekay::mat4 Transform::matrix() const {
    auto scale_mat = veekay::mat4::scaling(scale);
    
    auto rot_x = veekay::mat4::rotation(veekay::vec3{1.0f, 0.0f, 0.0f}, toRadians(rotation.x));
    auto rot_y = veekay::mat4::rotation(veekay::vec3{0.0f, 1.0f, 0.0f}, toRadians(rotation.y));
    auto rot_z = veekay::mat4::rotation(veekay::vec3{0.0f, 0.0f, 1.0f}, toRadians(rotation.z));
    
	auto rotation_mat = rot_x * rot_y * rot_z;
    
    auto translation_mat = veekay::mat4::translation(position);
    
	return scale_mat * rotation_mat * translation_mat;
}

veekay::mat4 Camera::view() const {
    // 2.4 Матрица вида: инверсия трансформа камеры (T^-1 * R^-1), чтобы объекты двигались относительно наблюдателя
    veekay::mat4 rot_x = veekay::mat4::rotation(veekay::vec3{1.0f, 0.0f, 0.0f}, -toRadians(rotation.x));
    veekay::mat4 rot_y = veekay::mat4::rotation(veekay::vec3{0.0f, 1.0f, 0.0f}, -toRadians(rotation.y));
    veekay::mat4 rot_z = veekay::mat4::rotation(veekay::vec3{0.0f, 0.0f, 1.0f}, -toRadians(rotation.z));

    veekay::mat4 rotation_matrix = rot_y * rot_x * rot_z;
    veekay::mat4 translation_matrix = veekay::mat4::translation(-position);

    // Соответствуем порядку умножения veekay: сначала T^-1, затем R^-1
    return translation_matrix * rotation_matrix;
}

veekay::mat4 Camera::lookAt_view() const{
    veekay::vec3 forward = target_position - position;

	float forward_len_sq = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
	if (forward_len_sq < 1e-6f) {
		forward = {0.0f, 0.0f, 1.0f};
	}
    forward = veekay::vec3::normalized(forward);

    veekay::vec3 up = {0.0f, 1.0f, 0.0f};
    veekay::vec3 right = veekay::vec3::cross(up, forward);
	float right_len_sq = right.x * right.x + right.y * right.y + right.z * right.z;
	if (right_len_sq < 1e-6f) {
		right = {1.0f, 0.0f, 0.0f};
	}
    right = veekay::vec3::normalized(right);
    up = veekay::vec3::normalized(veekay::vec3::cross(forward, right));

	veekay::mat4 r_matrix = veekay::mat4::identity();
    r_matrix.elements[0][0] = right.x;
    r_matrix.elements[1][0] = right.y;
    r_matrix.elements[2][0] = right.z;
    
    r_matrix.elements[0][1] = up.x;
    r_matrix.elements[1][1] = up.y;
    r_matrix.elements[2][1] = up.z;
    
    r_matrix.elements[0][2] = forward.x;
    r_matrix.elements[1][2] = forward.y;
    r_matrix.elements[2][2] = forward.z;

    veekay::mat4 translation_matrix = veekay::mat4::translation(-position);

	return translation_matrix * r_matrix;
}

veekay::mat4 Camera::view_projection(float aspect_ratio) const {
	// 2.4 Композиция вида/проекции: соответствуем API veekay (вид слева, проекция справа)
	auto projection = veekay::mat4::projection(fov, aspect_ratio, near_plane, far_plane);

	if(LookAt == true)
		return lookAt_view() * projection;
	else
		return view() * projection;
}

// Глобальные параметры сцены и источников света
namespace {
		LightingData lighting_params{};
		uint32_t point_light_count = 1;
		PointLight point_lights[8];
		uint32_t spotlight_count = 1;
		Spotlight spotlights[8];
		float orbit_speedup = 2.0f;
		veekay::vec4 sun_base_color = {2.5f, 2.3f, 1.6f, 0.0f};
		float sun_intensity = 13.0f;
		float dir_sun_intensity = 0.2f; // separate control for far directional sun brightness
	veekay::vec4 spotlight_base_color[8] = {
		{0.8f, 0.8f, 0.9f, 0.0f},
		{0.7f, 0.7f, 0.8f, 0.0f},
		{0.7f, 0.8f, 0.9f, 0.0f},
		{0.8f, 0.7f, 0.8f, 0.0f},
		{0.8f, 0.8f, 0.9f, 0.0f},
		{0.7f, 0.7f, 0.8f, 0.0f},
		{0.7f, 0.8f, 0.9f, 0.0f},
		{0.8f, 0.7f, 0.8f, 0.0f},
	};
	float spotlight_intensity[8] = {10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f};
	std::vector<StarInstance> stars;
	std::vector<CometInstance> comets;
	std::mt19937 rng{static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())};
	StarSpawner star_spawner{20.0f};
	CometSpawner comet_spawner{
		.shell_radius = globe_radius - 3.0f,
		.sun_safe_radius = 2.0f,
		.min_duration = 6.0f,
		.max_duration = 12.0f,
		.tail_scale = 0.25f
	};
	ShadowMap directional_shadow{};
	ShadowMap spot_shadow{};
	PointShadow sun_point_shadow{};
	PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR = nullptr;
	PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR = nullptr;
}

// NOTE: Loads shader byte code from file
// NOTE: Your shaders are compiled via CMake with this code too, look it up
VkShaderModule loadShaderModule(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	size_t size = file.tellg();
	std::vector<uint32_t> buffer(size / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), size);
	file.close();

	VkShaderModuleCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = buffer.data(),
	};

	VkShaderModule result;
	if (vkCreateShaderModule(veekay::app.vk_device, &
	                         info, nullptr, &result) != VK_SUCCESS) {
		return nullptr;
	}

	return result;
}

bool create_shadow_resources(ShadowMap& shadow, VkCommandBuffer cmd, VkDescriptorPool descriptor_pool, veekay::graphics::Buffer* model_uniforms_buffer, const char* shader_path) {
	VkDevice device = veekay::app.vk_device;

	shadow.depth_format = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo image_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = shadow.depth_format,
		.extent = {shadow.size, shadow.size, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	if (vkCreateImage(device, &image_info, nullptr, &shadow.image) != VK_SUCCESS) {
		std::cerr << "Failed to create shadow image\n";
		return false;
	}

	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(device, shadow.image, &mem_reqs);

	VkMemoryAllocateInfo alloc_info{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = veekay::graphics::findMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	};

	if (vkAllocateMemory(device, &alloc_info, nullptr, &shadow.memory) != VK_SUCCESS) {
		std::cerr << "Failed to allocate shadow image memory\n";
		return false;
	}

	vkBindImageMemory(device, shadow.image, shadow.memory, 0);

	VkImageViewCreateInfo view_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = shadow.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = shadow.depth_format,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	if (vkCreateImageView(device, &view_info, nullptr, &shadow.view) != VK_SUCCESS) {
		std::cerr << "Failed to create shadow image view\n";
		return false;
	}

	VkSamplerCreateInfo sampler_info{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.compareEnable = VK_TRUE,
		.compareOp = VK_COMPARE_OP_LESS,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};

	if (vkCreateSampler(device, &sampler_info, nullptr, &shadow.sampler) != VK_SUCCESS) {
		std::cerr << "Failed to create shadow sampler\n";
		return false;
	}

	shadow.shader = loadShaderModule(shader_path);
	if (!shadow.shader) {
		std::cerr << "Failed to load shadow shader\n";
		return false;
	}

	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
	};

	VkDescriptorSetLayoutCreateInfo layout_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(bindings) / sizeof(bindings[0]),
		.pBindings = bindings,
	};

	if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &shadow.descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create shadow descriptor set layout\n";
		return false;
	}

	VkDescriptorSetAllocateInfo alloc_info_set{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &shadow.descriptor_set_layout,
	};

	if (vkAllocateDescriptorSets(device, &alloc_info_set, &shadow.descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate shadow descriptor set\n";
		return false;
	}

	shadow.matrix_buffer = new veekay::graphics::Buffer(
		sizeof(veekay::mat4),
		nullptr,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

	VkDescriptorBufferInfo buffer_infos[] = {
		{
			.buffer = shadow.matrix_buffer->buffer,
			.range = sizeof(veekay::mat4),
		},
		{
			.buffer = model_uniforms_buffer->buffer,
			.range = sizeof(ModelUniforms),
		},
	};

	VkWriteDescriptorSet write_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = shadow.descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_infos[0],
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = shadow.descriptor_set,
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &buffer_infos[1],
		},
	};

	vkUpdateDescriptorSets(device, sizeof(write_infos) / sizeof(write_infos[0]), write_infos, 0, nullptr);

	VkPipelineLayoutCreateInfo pipeline_layout_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &shadow.descriptor_set_layout,
	};

	if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &shadow.pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create shadow pipeline layout\n";
		return false;
	}

	VkPipelineShaderStageCreateInfo stage_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = shadow.shader,
		.pName = "main",
	};

	VkVertexInputBindingDescription buffer_binding{
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	VkVertexInputAttributeDescription attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, position),
		}
	};

	VkPipelineVertexInputStateCreateInfo input_state_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &buffer_binding,
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = attributes,
	};

	VkPipelineInputAssemblyStateCreateInfo assembly_state_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	VkPipelineRasterizationStateCreateInfo raster_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_FRONT_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_TRUE,
		.depthBiasConstantFactor = 1.25f,
		.depthBiasSlopeFactor = 1.75f,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisample_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
	};

	VkPipelineDepthStencilStateCreateInfo depth_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
	};

	VkPipelineColorBlendStateCreateInfo colorBlendState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 0,
	};

	VkViewport viewport{
		.x = 0.0f,
		.y = 0.0f,
		.width = static_cast<float>(shadow.size),
		.height = static_cast<float>(shadow.size),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor{
		.offset = {0, 0},
		.extent = {shadow.size, shadow.size},
	};

	VkPipelineViewportStateCreateInfo viewport_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor,
	};

	VkDynamicState dyn_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};

	VkPipelineDynamicStateCreateInfo dyn_state_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dyn_states) / sizeof(dyn_states[0]),
		.pDynamicStates = dyn_states,
	};

	VkPipelineRenderingCreateInfoKHR rendering_create_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
		.colorAttachmentCount = 0,
		.pColorAttachmentFormats = nullptr,
		.depthAttachmentFormat = shadow.depth_format,
	};

	VkGraphicsPipelineCreateInfo pipeline_info{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &rendering_create_info,
		.stageCount = 1,
		.pStages = &stage_info,
		.pVertexInputState = &input_state_info,
		.pInputAssemblyState = &assembly_state_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &raster_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_info,
		.pColorBlendState = &colorBlendState,
		.pDynamicState = &dyn_state_info,
		.layout = shadow.pipeline_layout,
		.renderPass = VK_NULL_HANDLE,
	};

	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &shadow.pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create shadow pipeline\n";
		return false;
	}

	shadow.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	// Initial layout transition to depth attachment optimal
	VkImageMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = shadow.image,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);
	shadow.current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	return true;
}

void destroy_shadow_resources(ShadowMap& shadow) {
	VkDevice device = veekay::app.vk_device;
	if (shadow.pipeline) vkDestroyPipeline(device, shadow.pipeline, nullptr);
	if (shadow.pipeline_layout) vkDestroyPipelineLayout(device, shadow.pipeline_layout, nullptr);
	if (shadow.descriptor_set_layout) vkDestroyDescriptorSetLayout(device, shadow.descriptor_set_layout, nullptr);
	if (shadow.shader) vkDestroyShaderModule(device, shadow.shader, nullptr);
	if (shadow.sampler) vkDestroySampler(device, shadow.sampler, nullptr);
	if (shadow.view) vkDestroyImageView(device, shadow.view, nullptr);
	if (shadow.image) vkDestroyImage(device, shadow.image, nullptr);
	if (shadow.memory) vkFreeMemory(device, shadow.memory, nullptr);
	delete shadow.matrix_buffer;
	shadow = ShadowMap{};
}

void transition_shadow_layout(ShadowMap& shadow, VkCommandBuffer cmd, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access) {
	if (shadow.current_layout == new_layout) {
		return;
	}
	VkImageMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = shadow.current_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = shadow.image,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	shadow.current_layout = new_layout;
}

void record_shadow_pass(ShadowMap& shadow, VkCommandBuffer cmd, uint32_t model_count, uint32_t model_stride) {
	transition_shadow_layout(shadow, cmd,
	                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
	                         VK_ACCESS_SHADER_READ_BIT,
	                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

	VkClearValue clear_depth{.depthStencil = {1.0f, 0}};
	VkRenderingAttachmentInfoKHR depth_attachment{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
		.imageView = shadow.view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = clear_depth,
	};

	VkRenderingInfoKHR rendering_info{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
		.renderArea = {{0, 0}, {shadow.size, shadow.size}},
		.layerCount = 1,
		.colorAttachmentCount = 0,
		.pColorAttachments = nullptr,
		.pDepthAttachment = &depth_attachment,
	};

	vkCmdBeginRenderingKHR(cmd, &rendering_info);

	VkViewport viewport{
		.x = 0.0f, .y = 0.0f,
		.width = static_cast<float>(shadow.size),
		.height = static_cast<float>(shadow.size),
		.minDepth = 0.0f, .maxDepth = 1.0f,
	};
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, {shadow.size, shadow.size}};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow.pipeline);

	VkDeviceSize zero_offset = 0;
	VkBuffer current_vertex_buffer = VK_NULL_HANDLE;
	VkBuffer current_index_buffer = VK_NULL_HANDLE;

	for (size_t i = 0; i < model_count; ++i) {
		if (i == glass_model_index) continue;
		const Model& model = models[i];
		const Mesh& mesh = model.mesh;

		if (current_vertex_buffer != mesh.vertex_buffer->buffer) {
			current_vertex_buffer = mesh.vertex_buffer->buffer;
			vkCmdBindVertexBuffers(cmd, 0, 1, &current_vertex_buffer, &zero_offset);
		}

		if (current_index_buffer != mesh.index_buffer->buffer) {
			current_index_buffer = mesh.index_buffer->buffer;
			vkCmdBindIndexBuffer(cmd, current_index_buffer, zero_offset, VK_INDEX_TYPE_UINT32);
		}

		uint32_t offset = static_cast<uint32_t>(i * model_stride);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow.pipeline_layout,
		                        0, 1, &shadow.descriptor_set, 1, &offset);

		vkCmdDrawIndexed(cmd, mesh.indices, 1, 0, 0, 0);
	}

	vkCmdEndRenderingKHR(cmd);

	transition_shadow_layout(shadow, cmd,
	                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	                         VK_ACCESS_SHADER_READ_BIT);
}

bool create_point_shadow_resources(PointShadow& shadow, VkCommandBuffer cmd, VkDescriptorPool descriptor_pool, veekay::graphics::Buffer* model_uniforms_buffer, const char* shader_path) {
	VkDevice device = veekay::app.vk_device;
	shadow.depth_format = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo image_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = shadow.depth_format,
		.extent = {shadow.size, shadow.size, 1},
		.mipLevels = 1,
		.arrayLayers = 6,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	if (vkCreateImage(device, &image_info, nullptr, &shadow.image) != VK_SUCCESS) {
		std::cerr << "Failed to create point shadow image\n";
		return false;
	}

	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(device, shadow.image, &mem_reqs);

	VkMemoryAllocateInfo alloc_info{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = veekay::graphics::findMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	};

	if (vkAllocateMemory(device, &alloc_info, nullptr, &shadow.memory) != VK_SUCCESS) {
		std::cerr << "Failed to allocate point shadow memory\n";
		return false;
	}

	vkBindImageMemory(device, shadow.image, shadow.memory, 0);

	// Cube view for sampling
	VkImageViewCreateInfo cube_view_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = shadow.image,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = shadow.depth_format,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6,
		},
	};
	if (vkCreateImageView(device, &cube_view_info, nullptr, &shadow.cube_view) != VK_SUCCESS) {
		std::cerr << "Failed to create cube shadow view\n";
		return false;
	}

	// Face views for rendering
	for (uint32_t face = 0; face < 6; ++face) {
		VkImageViewCreateInfo face_view_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = shadow.image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = shadow.depth_format,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = face,
				.layerCount = 1,
			},
		};
		if (vkCreateImageView(device, &face_view_info, nullptr, &shadow.face_views[face]) != VK_SUCCESS) {
			std::cerr << "Failed to create face view for point shadow\n";
			return false;
		}
	}

	VkSamplerCreateInfo sampler_info{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.compareEnable = VK_TRUE,
		.compareOp = VK_COMPARE_OP_LESS,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};
	if (vkCreateSampler(device, &sampler_info, nullptr, &shadow.sampler) != VK_SUCCESS) {
		std::cerr << "Failed to create point shadow sampler\n";
		return false;
	}

	shadow.shader = loadShaderModule(shader_path);
	if (!shadow.shader) {
		std::cerr << "Failed to load point shadow shader\n";
		return false;
	}

	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
	};

	VkDescriptorSetLayoutCreateInfo layout_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(bindings) / sizeof(bindings[0]),
		.pBindings = bindings,
	};
	if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &shadow.descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create point shadow descriptor set layout\n";
		return false;
	}

	VkDescriptorSetAllocateInfo alloc_info_set{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &shadow.descriptor_set_layout,
	};
	if (vkAllocateDescriptorSets(device, &alloc_info_set, &shadow.descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate point shadow descriptor set\n";
		return false;
	}

	const size_t mat_stride = veekay::graphics::Buffer::structureAlignment(sizeof(veekay::mat4));
	shadow.matrix_buffer = new veekay::graphics::Buffer(
		mat_stride * 6,                 // 6 faces
		nullptr,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);


	VkDescriptorBufferInfo buffer_infos[] = {
		{
			.buffer = shadow.matrix_buffer->buffer,
			.range = sizeof(veekay::mat4),
		},
		{
			.buffer = model_uniforms_buffer->buffer,
			.range = sizeof(ModelUniforms),
		},
	};

	VkWriteDescriptorSet write_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = shadow.descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &buffer_infos[0],
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = shadow.descriptor_set,
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &buffer_infos[1],
		},
	};
	vkUpdateDescriptorSets(device, sizeof(write_infos) / sizeof(write_infos[0]), write_infos, 0, nullptr);

	VkPipelineLayoutCreateInfo pipeline_layout_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &shadow.descriptor_set_layout,
	};
	if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &shadow.pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create point shadow pipeline layout\n";
		return false;
	}

	VkPipelineShaderStageCreateInfo stage_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = shadow.shader,
		.pName = "main",
	};

	VkVertexInputBindingDescription buffer_binding{
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	VkVertexInputAttributeDescription attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, position),
		}
	};
	VkPipelineVertexInputStateCreateInfo input_state_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &buffer_binding,
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = attributes,
	};
	VkPipelineInputAssemblyStateCreateInfo assembly_state_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	VkPipelineRasterizationStateCreateInfo raster_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_FRONT_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_TRUE,
		.depthBiasConstantFactor = 1.25f,
		.depthBiasSlopeFactor = 1.75f,
		.lineWidth = 1.0f,
	};
	VkPipelineMultisampleStateCreateInfo multisample_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
	};
	VkPipelineDepthStencilStateCreateInfo depth_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
	};
	VkPipelineColorBlendStateCreateInfo colorBlendState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 0,
	};
	VkViewport viewport{
		.x = 0.0f,
		.y = 0.0f,
		.width = static_cast<float>(shadow.size),
		.height = static_cast<float>(shadow.size),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	VkRect2D scissor{
		.offset = {0, 0},
		.extent = {shadow.size, shadow.size},
	};
	VkPipelineViewportStateCreateInfo viewport_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor,
	};
	VkDynamicState dyn_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};
	VkPipelineDynamicStateCreateInfo dyn_state_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dyn_states) / sizeof(dyn_states[0]),
		.pDynamicStates = dyn_states,
	};
	VkPipelineRenderingCreateInfoKHR rendering_create_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
		.colorAttachmentCount = 0,
		.pColorAttachmentFormats = nullptr,
		.depthAttachmentFormat = shadow.depth_format,
	};
	VkGraphicsPipelineCreateInfo pipeline_info{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &rendering_create_info,
		.stageCount = 1,
		.pStages = &stage_info,
		.pVertexInputState = &input_state_info,
		.pInputAssemblyState = &assembly_state_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &raster_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_info,
		.pColorBlendState = &colorBlendState,
		.pDynamicState = &dyn_state_info,
		.layout = shadow.pipeline_layout,
		.renderPass = VK_NULL_HANDLE,
	};
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &shadow.pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create point shadow pipeline\n";
		return false;
	}

	shadow.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = shadow.image,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6,
		},
	};
	vkCmdPipelineBarrier(cmd,
	                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);
	shadow.current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	return true;
}

void destroy_point_shadow_resources(PointShadow& shadow) {
	VkDevice device = veekay::app.vk_device;
	if (shadow.pipeline) vkDestroyPipeline(device, shadow.pipeline, nullptr);
	if (shadow.pipeline_layout) vkDestroyPipelineLayout(device, shadow.pipeline_layout, nullptr);
	if (shadow.descriptor_set_layout) vkDestroyDescriptorSetLayout(device, shadow.descriptor_set_layout, nullptr);
	if (shadow.shader) vkDestroyShaderModule(device, shadow.shader, nullptr);
	if (shadow.sampler) vkDestroySampler(device, shadow.sampler, nullptr);
	for (auto view : shadow.face_views) {
		if (view) vkDestroyImageView(device, view, nullptr);
	}
	if (shadow.cube_view) vkDestroyImageView(device, shadow.cube_view, nullptr);
	if (shadow.image) vkDestroyImage(device, shadow.image, nullptr);
	if (shadow.memory) vkFreeMemory(device, shadow.memory, nullptr);
	delete shadow.matrix_buffer;
	shadow = PointShadow{};
}

void transition_point_shadow(PointShadow& shadow, VkCommandBuffer cmd, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access) {
	if (shadow.current_layout == new_layout) return;
	VkImageMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = shadow.current_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = shadow.image,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6,
		},
	};
	vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	shadow.current_layout = new_layout;
}

void record_point_shadow_pass(PointShadow& shadow, VkCommandBuffer cmd, uint32_t model_count, uint32_t model_stride, veekay::vec3 light_pos, int skip_model) {
	transition_point_shadow(shadow, cmd,
	                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
	                        VK_ACCESS_SHADER_READ_BIT,
	                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

	const veekay::vec3 directions[6] = {
		{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
	};
	const veekay::vec3 ups[6] = {
		{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}
	};

	VkViewport viewport{
		.x = 0.0f, .y = 0.0f,
		.width = static_cast<float>(shadow.size),
		.height = static_cast<float>(shadow.size),
		.minDepth = 0.0f, .maxDepth = 1.0f,
	};
	VkRect2D scissor{{0, 0}, {shadow.size, shadow.size}};

	VkDeviceSize zero_offset = 0;
	VkBuffer current_vertex_buffer = VK_NULL_HANDLE;
	VkBuffer current_index_buffer = VK_NULL_HANDLE;

	const uint32_t mat_stride = (uint32_t)veekay::graphics::Buffer::structureAlignment(sizeof(veekay::mat4));
	char* mat_base = static_cast<char*>(shadow.matrix_buffer->mapped_region);

	for (uint32_t face = 0; face < 6; ++face) {
		auto view = make_look_at(light_pos, light_pos + directions[face], ups[face]);
		auto proj = veekay::mat4::projection(90.0f, 1.0f, shadow.near_plane, shadow.far_plane);
		veekay::mat4 vp = view * proj;
		*reinterpret_cast<veekay::mat4*>(mat_base + face * mat_stride) = vp;

		VkRenderingAttachmentInfoKHR depth_attachment{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
			.imageView = shadow.face_views[face],
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = {.depthStencil = {1.0f, 0}},
		};
		VkRenderingInfoKHR rendering_info{
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
			.renderArea = {{0, 0}, {shadow.size, shadow.size}},
			.layerCount = 1,
			.colorAttachmentCount = 0,
			.pColorAttachments = nullptr,
			.pDepthAttachment = &depth_attachment,
		};

		vkCmdBeginRenderingKHR(cmd, &rendering_info);

		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow.pipeline);

		for (size_t i = 0; i < model_count; ++i) {
			if (static_cast<int>(i) == skip_model) continue;
			if (i == glass_model_index) continue;
			const Model& model = models[i];
			const Mesh& mesh = model.mesh;

			if (current_vertex_buffer != mesh.vertex_buffer->buffer) {
				current_vertex_buffer = mesh.vertex_buffer->buffer;
				vkCmdBindVertexBuffers(cmd, 0, 1, &current_vertex_buffer, &zero_offset);
			}
			if (current_index_buffer != mesh.index_buffer->buffer) {
				current_index_buffer = mesh.index_buffer->buffer;
				vkCmdBindIndexBuffer(cmd, current_index_buffer, zero_offset, VK_INDEX_TYPE_UINT32);
			}
			uint32_t dyn_offsets[2] = {
				face * mat_stride,                          // binding 0 (matrix)
				static_cast<uint32_t>(i * model_stride)     // binding 1 (model)
			};
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow.pipeline_layout,
									0, 1, &shadow.descriptor_set, 2, dyn_offsets);

			vkCmdDrawIndexed(cmd, mesh.indices, 1, 0, 0, 0);
		}

		vkCmdEndRenderingKHR(cmd);
	}

	transition_point_shadow(shadow, cmd,
	                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	                        VK_ACCESS_SHADER_READ_BIT);
}

void initialize(VkCommandBuffer cmd) {
	VkDevice& device = veekay::app.vk_device;
	VkPhysicalDevice& physical_device = veekay::app.vk_physical_device;
	vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
	vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));

	{ // NOTE: Build graphics pipeline
		vertex_shader_module = loadShaderModule("D:/Denis/Documents/Lab2/Lab2/shaders/shader.vert.spv");
		if (!vertex_shader_module) {
			std::cerr << "Failed to load Vulkan vertex shader from file\n";
			veekay::app.running = false;
			return;
		}

		fragment_shader_module = loadShaderModule("D:/Denis/Documents/Lab2/Lab2/shaders/shader.frag.spv");
		if (!fragment_shader_module) {
			std::cerr << "Failed to load Vulkan fragment shader from file\n";
			veekay::app.running = false;
			return;
		}

		VkPipelineShaderStageCreateInfo stage_infos[2];

		// NOTE: Vertex shader stage
		stage_infos[0] = VkPipelineShaderStageCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader_module,
			.pName = "main",
		};

		// NOTE: Fragment shader stage
		stage_infos[1] = VkPipelineShaderStageCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader_module,
			.pName = "main",
		};

		// NOTE: How many bytes does a vertex take?
		VkVertexInputBindingDescription buffer_binding{
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		// NOTE: Declare vertex attributes
		VkVertexInputAttributeDescription attributes[] = {
			{
				.location = 0, // NOTE: First attribute
				.binding = 0, // NOTE: First vertex buffer
				.format = VK_FORMAT_R32G32B32_SFLOAT, // NOTE: 3-component vector of floats
				.offset = offsetof(Vertex, position), // NOTE: Offset of "position" field in a Vertex struct
			},
			{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, normal),
			},
			{
				.location = 2,
				.binding = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(Vertex, uv),
			},
		};

		// NOTE: Describe inputs
		VkPipelineVertexInputStateCreateInfo input_state_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &buffer_binding,
			.vertexAttributeDescriptionCount = sizeof(attributes) / sizeof(attributes[0]),
			.pVertexAttributeDescriptions = attributes,
		};

		// NOTE: Every three vertices make up a triangle,
		//       so our vertex buffer contains a "list of triangles"
		VkPipelineInputAssemblyStateCreateInfo assembly_state_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		};

		// NOTE: Declare clockwise triangle order as front-facing
		//       Discard triangles that are facing away
		//       Fill triangles, don't draw lines instaed
		VkPipelineRasterizationStateCreateInfo raster_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_CLOCKWISE,
			.lineWidth = 1.0f,
		};

		// NOTE: Use 1 sample per pixel
		VkPipelineMultisampleStateCreateInfo sample_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable = false,
			.minSampleShading = 1.0f,
		};

		VkViewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(veekay::app.window_width),
			.height = static_cast<float>(veekay::app.window_height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};

		VkRect2D scissor{
			.offset = {0, 0},
			.extent = {veekay::app.window_width, veekay::app.window_height},
		};

		// NOTE: Let rasterizer draw on the entire window
		VkPipelineViewportStateCreateInfo viewport_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,

			.viewportCount = 1,
			.pViewports = &viewport,

			.scissorCount = 1,
			.pScissors = &scissor,
		};

		// NOTE: Let rasterizer perform depth-testing and overwrite depth values on condition pass
		VkPipelineDepthStencilStateCreateInfo depth_info{};
		depth_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_info.depthTestEnable = VK_TRUE;
		depth_info.depthWriteEnable = VK_TRUE;
		depth_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		// NOTE: Let fragment shader write all the color channels (opaque)
		VkPipelineColorBlendAttachmentState attachment_info{};
		attachment_info.blendEnable = VK_FALSE;
		attachment_info.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		attachment_info.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		attachment_info.colorBlendOp = VK_BLEND_OP_ADD;
		attachment_info.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		attachment_info.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		attachment_info.alphaBlendOp = VK_BLEND_OP_ADD;
		attachment_info.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		                                 VK_COLOR_COMPONENT_G_BIT |
		                                 VK_COLOR_COMPONENT_B_BIT |
		                                 VK_COLOR_COMPONENT_A_BIT;

		// NOTE: Blending for glass (alpha)
		VkPipelineColorBlendAttachmentState attachment_info_glass = attachment_info;
		attachment_info_glass.blendEnable = VK_TRUE;
		attachment_info_glass.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		attachment_info_glass.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		attachment_info_glass.colorBlendOp = VK_BLEND_OP_ADD;
		attachment_info_glass.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		attachment_info_glass.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		attachment_info_glass.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo blend_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

			.logicOpEnable = false,
			.logicOp = VK_LOGIC_OP_COPY,

			.attachmentCount = 1,
			.pAttachments = &attachment_info
		};

		VkPipelineColorBlendStateCreateInfo blend_info_glass = blend_info;
		blend_info_glass.pAttachments = &attachment_info_glass;

		{
			const uint32_t max_texture_sets = max_models;
			VkDescriptorPoolSize pools[] = {
				{
					.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 16,
				},
				{
					.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					.descriptorCount = 16,
				},
				{
					.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 16,
				},
				{
					.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 3u * (max_texture_sets + 16) + 12
				}
			};
			
			VkDescriptorPoolCreateInfo info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.maxSets = max_texture_sets + 32,
				.poolSizeCount = sizeof(pools) / sizeof(pools[0]),
				.pPoolSizes = pools,
			};

			if (vkCreateDescriptorPool(device, &info, nullptr,
			                           &descriptor_pool) != VK_SUCCESS) {
				std::cerr << "Failed to create Vulkan descriptor pool\n";
				veekay::app.running = false;
				return;
			}
		}

		// NOTE: Descriptor set layout specification
		{
			VkDescriptorSetLayoutBinding bindings[] = {
				{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 2,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 3,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 4,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 5,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 6,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
			};

			VkDescriptorSetLayoutCreateInfo info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = sizeof(bindings) / sizeof(bindings[0]),
				.pBindings = bindings,
			};

			if (vkCreateDescriptorSetLayout(device, &info, nullptr,
			                                &descriptor_set_layout) != VK_SUCCESS) {
				std::cerr << "Failed to create Vulkan descriptor set layout\n";
				veekay::app.running = false;
				return;
			}
		}

		// Layout for textures (albedo, specular, emissive)
		{
			// 3.1 Макет для привязки трех текстур материала (albedo/spec/emissive) в отдельный набор дескрипторов
			VkDescriptorSetLayoutBinding bindings[] = {
				{
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				{
					.binding = 2,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				}
			};

			VkDescriptorSetLayoutCreateInfo info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = sizeof(bindings) / sizeof(bindings[0]),
				.pBindings = bindings,
			};

			if (vkCreateDescriptorSetLayout(device, &info, nullptr,
			                                &texture_descriptor_set_layout) != VK_SUCCESS) {
				std::cerr << "Failed to create Vulkan texture descriptor set layout\n";
				veekay::app.running = false;
				return;
			}
		}

		{
			VkDescriptorSetAllocateInfo info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &descriptor_set_layout,
			};

			if (vkAllocateDescriptorSets(device, &info, &descriptor_set) != VK_SUCCESS) {
				std::cerr << "Failed to create Vulkan descriptor set\n";
				veekay::app.running = false;
				return;
			}
		}

		// NOTE: Declare external data sources, only push constants this time
		VkDescriptorSetLayout set_layouts[] = {descriptor_set_layout, texture_descriptor_set_layout};
		VkPipelineLayoutCreateInfo layout_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 2,
			.pSetLayouts = set_layouts,
		};

		// NOTE: Create pipeline layout
		if (vkCreatePipelineLayout(device, &layout_info,
		                           nullptr, &pipeline_layout) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan pipeline layout\n";
			veekay::app.running = false;
			return;
		}
		
		VkGraphicsPipelineCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = 2,
			.pStages = stage_infos,
			.pVertexInputState = &input_state_info,
			.pInputAssemblyState = &assembly_state_info,
			.pViewportState = &viewport_info,
			.pRasterizationState = &raster_info,
			.pMultisampleState = &sample_info,
			.pDepthStencilState = &depth_info,
			.pColorBlendState = &blend_info,
			.layout = pipeline_layout,
			.renderPass = veekay::app.vk_render_pass,
		};

		// NOTE: Create graphics pipeline (opaque)
		if (vkCreateGraphicsPipelines(device, nullptr,
		                              1, &info, nullptr, &pipeline) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan pipeline\n";
			veekay::app.running = false;
			return;
		}

		// Glass pipeline: no culling, depth write off, blending on
		VkPipelineRasterizationStateCreateInfo raster_info_glass = raster_info;
		raster_info_glass.cullMode = VK_CULL_MODE_NONE;

		VkPipelineDepthStencilStateCreateInfo depth_info_glass = depth_info;
		depth_info_glass.depthWriteEnable = false;

		VkGraphicsPipelineCreateInfo info_glass = info;
		info_glass.pRasterizationState = &raster_info_glass;
		info_glass.pDepthStencilState = &depth_info_glass;
		info_glass.pColorBlendState = &blend_info_glass;

		if (vkCreateGraphicsPipelines(device, nullptr,
		                              1, &info_glass, nullptr, &pipeline_glass) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan glass pipeline\n";
			veekay::app.running = false;
			return;
		}
	}

	scene_uniforms_buffer = new veekay::graphics::Buffer(
		sizeof(SceneUniforms),
		nullptr,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

	model_uniforms_buffer = new veekay::graphics::Buffer(
		max_models * veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms)),
		nullptr,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	
	constexpr size_t ssbo_padding = 16 - sizeof(uint32_t);

	point_lights_buffer = new veekay::graphics::Buffer(
		16 + sizeof(PointLight) * 8,  // padded count + lights
		nullptr,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	spotlights_buffer = new veekay::graphics::Buffer(
		16 + sizeof(Spotlight) * 8,  // padded count + lights
		nullptr,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	directional_shadow.size = 2048;
	if (!create_shadow_resources(directional_shadow, cmd, descriptor_pool, model_uniforms_buffer, "D:/Denis/Documents/Lab2/Lab2/shaders/shadow.vert.spv")) {
		veekay::app.running = false;
		return;
	}
	spot_shadow.size = 1024;
	if (!create_shadow_resources(spot_shadow, cmd, descriptor_pool, model_uniforms_buffer, "D:/Denis/Documents/Lab2/Lab2/shaders/shadow.vert.spv")) {
		veekay::app.running = false;
		return;
	}
	sun_point_shadow.size = 1024;
	sun_point_shadow.near_plane = 0.1f;
	sun_point_shadow.far_plane = 120.0f;
	if (!create_point_shadow_resources(sun_point_shadow, cmd, descriptor_pool, model_uniforms_buffer, "D:/Denis/Documents/Lab2/Lab2/shaders/shadow.vert.spv")) {
		veekay::app.running = false;
		return;
	}

	// NOTE: Textures and samplers
	{
		// 3.2 Создаем сэмплер (линейная фильтрация, мипы, repeat) для всех материалов
		VkSamplerCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.mipLodBias = 0.0f;
		info.minLod = 0.0f;
		info.maxLod = VK_LOD_CLAMP_NONE;

		if (vkCreateSampler(device, &info, nullptr, &texture_sampler) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan texture sampler\n";
			veekay::app.running = false;
			return;
		}

		missing_texture_sampler = texture_sampler;

		uint32_t checker[] = {
			0xff000000, 0xffff00ff,
			0xffff00ff, 0xff000000,
		};

		missing_texture = new veekay::graphics::Texture(cmd, 2, 2,
		                                                VK_FORMAT_B8G8R8A8_UNORM,
		                                                checker);
		white_texture = make_solid_texture(cmd, 0xffffffff);
		black_texture = make_solid_texture(cmd, 0xff000000);
		default_texture_set = create_texture_set(device, missing_texture, white_texture, black_texture);
		white_texture_set = create_texture_set(device, white_texture, white_texture, black_texture);
	}

	{
		VkDescriptorBufferInfo buffer_infos[] = {
			{
				.buffer = scene_uniforms_buffer->buffer,
				.offset = 0,
				.range = sizeof(SceneUniforms),
			},
			{
				.buffer = model_uniforms_buffer->buffer,
				.offset = 0,
				.range = sizeof(ModelUniforms),
			},
			{
				.buffer = point_lights_buffer->buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE,
			},
			{
				.buffer = spotlights_buffer->buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE,
			},
		};

		VkDescriptorImageInfo shadow_infos[] = {
			{
				.sampler = directional_shadow.sampler,
				.imageView = directional_shadow.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
			{
				.sampler = spot_shadow.sampler,
				.imageView = spot_shadow.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
			{
				.sampler = sun_point_shadow.sampler,
				.imageView = sun_point_shadow.cube_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
		};

		VkWriteDescriptorSet write_infos[] = {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &buffer_infos[0],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &buffer_infos[1],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pBufferInfo = &buffer_infos[2],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 3,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pBufferInfo = &buffer_infos[3],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 4,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &shadow_infos[0],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 5,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &shadow_infos[1],
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 6,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &shadow_infos[2],
			},
		};

		vkUpdateDescriptorSets(device, sizeof(write_infos) / sizeof(write_infos[0]),
		                       write_infos, 0, nullptr);
	}

		// 0.4 Сцена солнечной системы: планеты/кольцо, с масштабированием расстояний под окно
		//    Используем эксцентриситет/период/угол из planets.md, полуось b = a*sqrt(1-e^2)
		// Plane({0.0f, 0.0f, 0.0f}, Standart, "Ecliptic");
		star_spawner.radius = globe_radius - 1.0f;

	const auto add_orbit = [&](size_t model_index, size_t parent_index, float a, float e, float rotation_deg, float period_years, float phase = 0.0f) {
		float b = a * std::sqrt(1.0f - e * e);
		float omega = float(2.0 * M_PI / period_years);
		orbits.push_back(Orbit{
			.model_index = model_index,
			.parent_index = parent_index,
			.a = a,
			.b = b,
			.rotation_rad = toRadians(rotation_deg),
			.omega = omega,
			.phase = phase
		});
	};

	auto add_planet = [&](const veekay::vec3& pos, float radius, Material mat, const std::string& name) -> size_t {
		size_t before = models.size();
		Sphere(pos, radius, 24, 36, mat, name);
		return models.size() - 1;
	};

		// Масштабируем расстояния под сцену
		const float base_distance = 1.0f;

		// Солнце
	size_t sun_idx = add_planet({0.0f, 0.0f, 0.0f}, 1.5f, YellowSun, "Sun");
	sun_model_index = sun_idx;

		// Планеты
	size_t mercury_idx = add_planet({base_distance * 2.0f, 0.0f, 0.0f}, 0.25f, MercuryMat, "Mercury");
	add_orbit(mercury_idx, SIZE_MAX, base_distance * 2.0f, 0.206f, 29.0f, 0.24f);
	spin_targets.push_back({mercury_idx, 25.0f});

	size_t venus_idx = add_planet({base_distance * 3.0f, 0.0f, 0.0f}, 0.35f, VenusMat, "Venus");
	add_orbit(venus_idx, SIZE_MAX, base_distance * 3.0f, 0.007f, 77.0f, 0.62f);
	spin_targets.push_back({venus_idx, 15.0f});

	size_t earth_idx = add_planet({base_distance * 4.0f, 0.0f, 0.0f}, 0.4f, EarthMat, "Earth");
	add_orbit(earth_idx, SIZE_MAX, base_distance * 4.0f, 0.017f, 0.0f, 1.0f);
	spin_targets.push_back({earth_idx, 20.0f});

	size_t moon_idx = add_planet({base_distance * 4.6f, 0.0f, 0.0f}, 0.12f, MoonMat, "Moon");
	add_orbit(moon_idx, earth_idx, 0.7f, 0.055f, 0.0f, 0.075f);
	spin_targets.push_back({moon_idx, 40.0f});

	size_t mars_idx = add_planet({base_distance * 5.0f, 0.0f, 0.0f}, 0.3f, MarsMat, "Mars");
	add_orbit(mars_idx, SIZE_MAX, base_distance * 5.0f, 0.093f, 49.0f, 1.88f);
	spin_targets.push_back({mars_idx, 18.0f});

	size_t jupiter_idx = add_planet({base_distance * 7.0f, 0.0f, 0.0f}, 0.9f, JupiterMat, "Jupiter");
	add_orbit(jupiter_idx, SIZE_MAX, base_distance * 7.0f, 0.048f, 100.0f, 11.86f);
	spin_targets.push_back({jupiter_idx, 30.0f});

	size_t saturn_idx = add_planet({base_distance * 9.0f, 0.0f, 0.0f}, 0.8f, SaturnMat, "Saturn");
	add_orbit(saturn_idx, SIZE_MAX, base_distance * 9.0f, 0.054f, 113.0f, 29.46f);
	spin_targets.push_back({saturn_idx, 28.0f});

		// Кольцо Сатурна
		Ring({0.0f, 0.0f, 0.0f}, 1.0f, 1.6f, 64, RingMat, "Saturn Ring");
		size_t ring_idx = models.size() - 1;
		saturn_ring_model_index = ring_idx;
		add_orbit(ring_idx, saturn_idx, 0.0f, 0.0f, 0.0f, 1.0f); // прицеплено к Сатурну

	size_t uranus_idx = add_planet({base_distance * 11.0f, 0.0f, 0.0f}, 0.7f, UranusMat, "Uranus");
	add_orbit(uranus_idx, SIZE_MAX, base_distance * 11.0f, 0.047f, 74.0f, 84.01f);
	spin_targets.push_back({uranus_idx, 24.0f});

	size_t neptune_idx = add_planet({base_distance * 13.0f, 0.0f, 0.0f}, 0.7f, NeptuneMat, "Neptune");
	add_orbit(neptune_idx, SIZE_MAX, base_distance * 13.0f, 0.009f, 131.0f, 164.79f);
	spin_targets.push_back({neptune_idx, 20.0f});

		// 0.1 Звёзды: 30 эмиссивных сфер на небесной сфере, перераспавн при окончании жизни
		const int star_count = 50;
		for (int i = 0; i < star_count; ++i) {
			size_t idx_before = models.size();
			Sphere({0.0f, 0.0f, 0.0f}, 0.05f, 8, 12, StarMat, "Star");
			models[idx_before].texture_set = white_texture_set;
			StarInstance s;
			s.model_index = idx_before;
			s.base_color = StarMat.albedo_color;
			star_spawner.respawn(s, rng);
			stars.push_back(s);
		}

		// 0.1 Кометы: до 3 штук, вытянутая модель + точечный свет в голове
		const int comet_max = 3;
		for (int i = 0; i < comet_max; ++i) {
			const float comet_radius = 0.1f;
			const float comet_base_stretch = 1.0f;
			size_t model_idx_before = models.size();
			StretchedSphere({0.0f, 0.0f, 0.0f}, comet_radius, comet_base_stretch, 20, 28, CometTailMat, "Comet");
			CometInstance c;
			c.model_index = model_idx_before;
			c.radius = comet_radius;
			c.tail_base_stretch = comet_base_stretch;
			c.light_slot = static_cast<uint32_t>(1 + i); // слоты 1..2 под кометы (0 — Солнце)
			models[c.model_index].material.warp_strength = 1.0f;
			models[c.model_index].material.emissive_pulse = 0.5f;
			models[c.model_index].texture_set = white_texture_set;
			comet_spawner.respawn(c, rng);
			comets.push_back(c);
		}

		// Основание глобуса
		{
			size_t idx_before = models.size();
			Cylinder({0.0f, 0.0f, 0.0f}, base_radius, base_height, 48, BaseMat, "GlobeBase");
			base_model_index = idx_before;
			if (base_model_index < models.size()) {
				models[base_model_index].transform.position.y = (globe_radius + base_height * 0.4f);
				models[base_model_index].transform.rotation = {-90.0f, 0.0f, 0.0f};
				models[base_model_index].transform.scale.z = -1.0f; // инверсия, чтобы виднелась наружная сторона при таком повороте
				models[base_model_index].material.emissive_pulse = 0.8f; // радиальная пульсация текстуры
			}
		}


		// Наклонные опоры к стеклу
		for (int i = 0; i < 4; ++i) {
			float angle = toRadians(45.0f + 90.0f * i);
			veekay::vec3 dir = {std::cos(angle), 0.0f, std::sin(angle)};
			float base_y_pos = models[base_model_index].transform.position.y;
			float y_bottom = base_y_pos - base_height * 0.5f - 0.1f; // опора стартует чуть выше базы (учитываем инверсию Y)
			veekay::vec3 bottom = dir * (base_radius * 0.55f); // ближе к центру
			bottom.y = y_bottom;

			// короткая опора вверх и чуть внутрь
			veekay::vec3 top = bottom - veekay::vec3{dir.x * 0.15f, 1.2f, dir.z * 0.15f};

			veekay::vec3 span = top - bottom;
			float length = std::sqrt(span.x * span.x + span.y * span.y + span.z * span.z);
			float yaw = std::atan2(span.x, span.z) * 180.0f / float(M_PI);
			float pitch = std::asin(-span.y / std::max(length, 1e-4f)) * 180.0f / float(M_PI);

			size_t idx_before = models.size();
			Cylinder({0.0f, 0.0f, 0.0f}, 0.12f, 1.0f, 24, BaseMat, "Support");
			support_model_indices[i] = idx_before;
			if (support_model_indices[i] < models.size()) {
				Model& sup = models[support_model_indices[i]];
				sup.transform.position = bottom + span * 0.5f;
				sup.transform.scale = {1.0f, 1.0f, -length}; // инвертируем Z, чтобы фронт-фейсы смотрели наружу, как у базы
				sup.transform.rotation = {pitch, yaw, 0.0f};
				sup.texture_set = default_texture_set; // переопределим корректным деревом после загрузки текстур
				sup.material.warp_strength = 0.0f;
				sup.material.emissive_pulse = 0.0f;
				sup.material.albedo_color = veekay::vec3{1.0f, 1.0f, 1.0f};
				sup.material.specular_color = BaseMat.specular_color;
				sup.material.shininess = BaseMat.shininess;
			}
		}

		// Стеклянный купол (пока непрозрачный материал с сильным бликом)
		{
			glass_model_index = models.size();
			Sphere({0.0f, 0.0f, 0.0f}, globe_radius, 24, 36, GlassMat, "GlassGlobe");
		}

		// Корпуса прожекторов для 8 источников
		for (uint32_t i = 0; i < 8; ++i) {
			size_t idx_before = models.size();
			Cylinder({0.0f, 0.0f, 0.0f}, projector_radius, projector_length, 24, SpotlightMat, "SpotlightModel");
			spotlight_model_indices[i] = idx_before;
			if (spotlight_model_indices[i] < models.size()) {
				models[spotlight_model_indices[i]].transform.scale = {1.0f, 1.0f, 1.0f};
				models[spotlight_model_indices[i]].texture_set = white_texture_set;
				models[spotlight_model_indices[i]].material.warp_strength = 0.0f;
				models[spotlight_model_indices[i]].material.emissive_pulse = 0.0f;
			}
		}

		// 3.4 Привязываем уникальные наборы текстур к моделям (albedo/spec/emissive)
		auto set_tex = [&](size_t idx, const std::string& albedo, const std::string& spec = std::string(), const std::string& emissive = std::string(), float warp = 0.0f) {
			if (idx >= models.size()) return;
			size_t tex_idx = create_texture_set_from_paths(cmd, albedo, spec, emissive);
			models[idx].texture_set = tex_idx;
			models[idx].material.warp_strength = warp;
		};

		set_tex(sun_idx, "textures/2k_sun.jpg", std::string(), std::string(), 1.2f);
		models[sun_idx].material.emissive_pulse = 1.0f;
		set_tex(mercury_idx, "textures/2k_mercury.jpg");
		set_tex(venus_idx, "textures/2k_venus_surface.jpg");
		set_tex(earth_idx, "textures/2k_earth_daymap.jpg", "textures/2k_earth_specular_map.png", "textures/2k_earth_clouds.jpg");
		set_tex(moon_idx, "textures/2k_moon.jpg");
		set_tex(mars_idx, "textures/2k_mars.jpg");
		set_tex(jupiter_idx, "textures/2k_jupiter.jpg");
		set_tex(saturn_idx, "textures/2k_saturn.jpg");
		if (saturn_ring_model_index < models.size()) {
			set_tex(saturn_ring_model_index, "textures/2k_saturn_ring_alpha.png");
			models[saturn_ring_model_index].material.opacity = 0.6f;
		}
		set_tex(uranus_idx, "textures/2k_uranus.jpg");
		set_tex(neptune_idx, "textures/2k_neptune.jpg");
		if (base_model_index < models.size()) {
			set_tex(base_model_index, "textures/rosewood/textures/rosewood_veneer1_diff_2k.jpg", std::string(), std::string(), 0.25f);
			size_t base_tex = models[base_model_index].texture_set;
			for (int i = 0; i < 4; ++i) {
				if (support_model_indices[i] < models.size()) {
					Model& sup = models[support_model_indices[i]];
					sup.texture_set = base_tex;
					sup.material.albedo_color = veekay::vec3{1.0f, 1.0f, 1.0f};
					sup.material.specular_color = BaseMat.specular_color;
					sup.material.shininess = BaseMat.shininess;
					sup.material.warp_strength = 0.0f;
					sup.material.emissive_pulse = 0.0f;
					sup.material.opacity = 1.0f;
					// 3.* Опоры отрисовываются внешней стороной (см. инверсию Z-скейла при создании)
				}
			}
		}
	}

// NOTE: Destroy resources here, do not cause leaks in your program!
void shutdown() {
	VkDevice& device = veekay::app.vk_device;

	destroy_shadow_resources(directional_shadow);
	destroy_shadow_resources(spot_shadow);
	destroy_point_shadow_resources(sun_point_shadow);

	for(auto model : models)
	{
		delete model.mesh.index_buffer;
		delete model.mesh.vertex_buffer;
	}

	delete spotlights_buffer;
	delete point_lights_buffer;
	delete model_uniforms_buffer;
	delete scene_uniforms_buffer;

	{
		std::unordered_set<veekay::graphics::Texture*> cleaned;
		for (const auto& kv : texture_cache) {
			if (kv.second && kv.second != missing_texture && kv.second != white_texture && kv.second != black_texture) {
				if (!cleaned.count(kv.second)) {
					delete kv.second;
					cleaned.insert(kv.second);
				}
			}
		}
		texture_cache.clear();
	}

	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(device, texture_descriptor_set_layout, nullptr);
	vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

	vkDestroyPipeline(device, pipeline_glass, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	vkDestroyShaderModule(device, fragment_shader_module, nullptr);
	vkDestroyShaderModule(device, vertex_shader_module, nullptr);

	if (texture_sampler) {
		vkDestroySampler(device, texture_sampler, nullptr);
	}
	if (missing_texture) delete missing_texture;
	if (white_texture) delete white_texture;
	if (black_texture) delete black_texture;
}
void update(double time) {
    static int selectedModelIndex = 0;
	static bool first = false;
	// 0.7 Дельта по времени между кадрами для анимации звёзд/комет и плавных респавнов
	static double prev_time = time;
	double dt = first ? (time - prev_time) : 0.0;
	prev_time = time;
	if (dt < 0.0) dt = 0.0;
	float dt_f = static_cast<float>(dt);

	// Готовим цвет Солнца без множителя, чтобы в UI редактировать базовое значение
	if (point_light_count > 0) {
		point_lights[0].color = sun_base_color;
		point_lights[0].color.w = 0.0f;
	}
	for (uint32_t i = 0; i < spotlight_count && i < 8; ++i) {
		spotlights[i].color = spotlight_base_color[i];
		spotlights[i].color.w = 0.0f;
	}

	ImGui::Begin("Controls:");
	ImGui::DragFloat3("Camera pos", reinterpret_cast<float *>(&camera.position));
	ImGui::DragFloat3("Camera rot", reinterpret_cast<float *>(&camera.rotation));
	ImGui::InputFloat3("Look-at target", reinterpret_cast<float *>(&camera.target_position));

	bool previousLookAt = camera.LookAt;
	// 2.1 Переключение режимов камеры: сохраняем/восстанавливаем положение и цель при переходе LookAt <-> свободная камера
	ImGui::Checkbox("Look-at mode", &camera.LookAt);
	if (previousLookAt != camera.LookAt) {
		if (previousLookAt) {
			lookat_state = {
				.position = camera.position,
				.rotation = camera.rotation,
				.target = camera.target_position,
			};
			lookat_state_valid = true;

			if (transform_state_valid) {
				camera.position = transform_state.position;
				camera.rotation = transform_state.rotation;
				camera.target_position = transform_state.target;
			}
		} else {
			transform_state = {
				.position = camera.position,
				.rotation = camera.rotation,
				.target = camera.target_position,
			};
			transform_state_valid = true;

			if (lookat_state_valid) {
				camera.position = lookat_state.position;
				camera.rotation = lookat_state.rotation;
				camera.target_position = lookat_state.target;
			} else if (!models.empty()) {
				size_t clamped_index = std::min<size_t>(selectedModelIndex, models.size() - 1);
				camera.target_position = models[clamped_index].transform.position;
			}
		}
	}

	if (camera.LookAt && !models.empty()) {
        ImGui::Text("Target:");
        if (ImGui::BeginCombo("##CameraTarget", ("Model " + models[selectedModelIndex].title).c_str())) {
            for (size_t i = 0; i < models.size(); ++i) {
                bool isSelected = (selectedModelIndex == (int)i);
                if (ImGui::Selectable(("Model " + models[i].title).c_str(), isSelected)) {
                    selectedModelIndex = (int)i;
					camera.target_position = models[selectedModelIndex].transform.position;
                }
                if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
            }
            ImGui::EndCombo();
        }
	}

	ImGui::SliderFloat("Orbit speedup", &orbit_speedup, 0.1f, 10.0f, "%.1f");
	// 0.5 Регулировка яркости Солнца без изменения подобранного цвета
	ImGui::SliderFloat("Sun intensity", &sun_intensity, 0.1f, 20.0f, "%.1f");
	ImGui::SliderFloat("Far sun intensity", &dir_sun_intensity, 0.0f, 2.0f, "%.2f");

	ImGui::Separator();
	
	// Фоновый (ambient) свет
	ImGui::Text("Ambient Light");
	ImGui::ColorEdit3("Ambient Color", &lighting_params.ambient_color.x);
	
	ImGui::Separator();
	
	// Точечные источники
	ImGui::Text("Point Lights");
	int point_count_int = int(point_light_count);
	if (ImGui::DragInt("Point Light Count", &point_count_int, 1.0f, 0, 8)) {
		point_light_count = uint32_t(std::max(0, std::min(8, point_count_int)));
	}
	for (uint32_t i = 0; i < point_light_count && i < 8; i++) {
		ImGui::PushID(int(i));
		ImGui::Text("Point Light %u", i);
		ImGui::DragFloat3("Position", &point_lights[i].position.x, 0.1f);
		ImGui::ColorEdit3("Color", &point_lights[i].color.x);
		ImGui::PopID();
	}
	
	ImGui::Separator();

	if (point_light_count > 0) {
		sun_base_color = point_lights[0].color;
		sun_base_color.w = 0.0f;
	}

	// Прожекторы
	ImGui::Text("Spotlights");
	int spot_count_int = int(spotlight_count);
	if (ImGui::DragInt("Spotlight Count", &spot_count_int, 1.0f, 0, 8)) {
		uint32_t old_count = spotlight_count;
		spotlight_count = uint32_t(std::max(0, std::min(8, spot_count_int)));

		// Инициализируем новые источники, если количество увеличилось
		for (uint32_t i = old_count; i < spotlight_count; ++i) {
			// Устанавливаем стандартные значения
			spotlights[i].position = veekay::vec4{0.0f, 0.0f, 0.0f, 0.0f};
			spotlights[i].direction = veekay::vec4{0.0f, 0.0f, -1.0f, 0.0f}; // важное: не нулевой вектор
			spotlights[i].color = veekay::vec4{1.0f, 1.0f, 1.0f, 0.0f};
			spotlight_base_color[i] = spotlights[i].color;
			spotlight_intensity[i] = 1.0f;
			spotlights[i].cone_angles = veekay::vec4{
				std::cos(toRadians(20.0f)),  // inner
				std::cos(toRadians(25.0f)),  // outer
				0.0f, 0.0f
			};
		}
	}
	for (uint32_t i = 0; i < spotlight_count && i < 8; i++) {
		ImGui::PushID(int(i + 8));
		ImGui::Text("Spotlight %u", i);
		ImGui::DragFloat3("Position", &spotlights[i].position.x, 0.1f);
		ImGui::DragFloat3("Direction", &spotlights[i].direction.x, 0.01f, -1.0f, 1.0f);
		ImGui::ColorEdit3("Color", &spotlights[i].color.x);
		// 0.5 Регулировка яркости прожектора отдельным множителем
		ImGui::SliderFloat("Intensity", &spotlight_intensity[i], 0.0f, 100.0f);
		float inner_deg = std::acos(spotlights[i].cone_angles.x) * 180.0f / float(M_PI);
		float outer_deg = std::acos(spotlights[i].cone_angles.y) * 180.0f / float(M_PI);
		if (ImGui::DragFloat("Inner Angle (deg)", &inner_deg, 1.0f, 0.0f, 90.0f)) {
			spotlights[i].cone_angles.x = std::cos(inner_deg * float(M_PI) / 180.0f);
		}
		if (ImGui::DragFloat("Outer Angle (deg)", &outer_deg, 1.0f, 0.0f, 90.0f)) {
			spotlights[i].cone_angles.y = std::cos(outer_deg * float(M_PI) / 180.0f);
		}
		ImGui::PopID();
	}
	ImGui::End();
	
	// 2.2 Управление камерой с клавиатуры/мыши вне UI окна
	for (uint32_t i = 0; i < spotlight_count && i < 8; ++i) {
		spotlight_base_color[i] = spotlights[i].color;
		spotlight_base_color[i].w = 0.0f;
	}
	
	if (!ImGui::IsWindowHovered()) {
		using namespace veekay::input;
	
		if (mouse::isButtonDown(mouse::Button::left)) {
			auto view = (camera.LookAt) ? camera.lookAt_view() : camera.view();
			veekay::vec3 right = {view[0][0], view[1][0], view[2][0]};
			veekay::vec3 up    = {view[0][1], view[1][1], view[2][1]};
			veekay::vec3 front = {-view[0][2], -view[1][2], -view[2][2]};

			if (!camera.LookAt) {
				auto delta = mouse::cursorDelta();
				camera.rotation.y += delta.x * 0.2f;
    			camera.rotation.x -= delta.y * 0.2f;
			}

			if (keyboard::isKeyDown(keyboard::Key::w))
				camera.position -= front * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::s))
				camera.position += front * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::d))
				camera.position += right * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::a))
				camera.position -= right * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::q))
				camera.position -= up * 0.1f;

			if (keyboard::isKeyDown(keyboard::Key::z))
				camera.position += up * 0.1f;
		}
	}

	float aspect_ratio = float(veekay::app.window_width) / float(veekay::app.window_height);

	// создаем источники первый раз
	if (!first) {
		lighting_params.ambient_color = veekay::vec4{0.2f, 0.2f, 0.2f, 0.0f};
		
		// Точечный источник (Солнце)
			point_light_count = 1;
			point_lights[0].position = veekay::vec4{0.0f, 0.0f, 0.0f, 0.0f};
			point_lights[0].color = sun_base_color; // Цвет задаётся из UI
			
			// 2.3 Добавленный тип источника — прожекторы, подсвечивающие сцену сверху/снизу (8 штук)
			spotlight_count = 8;
			float inner_angle_rad = toRadians(25.0f);
			float outer_angle_rad = toRadians(40.0f);

			auto set_spot = [&](int idx, veekay::vec3 pos, veekay::vec3 dir) {
				veekay::vec3 nd = veekay::vec3::normalized(dir);
				spotlights[idx].position = veekay::vec4{pos.x, pos.y, pos.z, 0.0f};
				spotlights[idx].direction = veekay::vec4{nd.x, nd.y, nd.z, 0.0f};
				spotlights[idx].color = spotlight_base_color[idx];
				spotlights[idx].cone_angles = veekay::vec4{
					std::cos(inner_angle_rad),
					std::cos(outer_angle_rad),
					0.0f,
					0.0f
				};
			};

			float ring_radius = globe_radius - 2.0f; // внутри стекла, чуть отступаем
			float y_top = globe_radius * 0.35f;
			float y_bottom = -globe_radius * 0.35f;

			for (int i = 0; i < 4; ++i) {
				float angle = (float(i) / 4.0f) * float(M_PI) * 2.0f;
				veekay::vec3 pos_top = {ring_radius * std::cos(angle), y_top, ring_radius * std::sin(angle)};
				veekay::vec3 dir_top = veekay::vec3::normalized(-pos_top);
				set_spot(i, pos_top, dir_top);

				veekay::vec3 pos_bottom = {ring_radius * std::cos(angle + float(M_PI) / 4.0f), y_bottom, ring_radius * std::sin(angle + float(M_PI) / 4.0f)};
				veekay::vec3 dir_bottom = veekay::vec3::normalized(-pos_bottom);
				set_spot(i + 4, pos_bottom, dir_bottom);
			}
			
			first = true;
		}
	// Нормализуем направления прожекторов и умножаем базовый цвет на интенсивность
	for (uint32_t i = 0; i < spotlight_count && i < 8; i++) {
		veekay::vec3 spot_dir = veekay::vec3::normalized(veekay::vec3{
			spotlights[i].direction.x,
			spotlights[i].direction.y,
			spotlights[i].direction.z
		});
		spotlights[i].direction = veekay::vec4{spot_dir.x, spot_dir.y, spot_dir.z, 0.0f};
		spotlights[i].color = veekay::vec4{
			spotlight_base_color[i].x * spotlight_intensity[i],
			spotlight_base_color[i].y * spotlight_intensity[i],
			spotlight_base_color[i].z * spotlight_intensity[i],
			0.0f};

		if (spotlight_model_indices[i] < models.size()) {
			Model& marker = models[spotlight_model_indices[i]];
			float yaw = std::atan2(spot_dir.x, spot_dir.z) * 180.0f / float(M_PI);
			float pitch = std::asin(-spot_dir.y) * 180.0f / float(M_PI);
			marker.transform.rotation = {pitch, yaw, 0.0f};
			marker.transform.position = {spotlights[i].position.x, spotlights[i].position.y, spotlights[i].position.z};
			marker.material = SpotlightMat;
		}
	}

	// Apply sun intensity to the first point light color but keep user-editable base
	if (point_light_count > 0) {
		// 0.5 Яркость Солнца: умножаем пользовательский цвет на слайдер интенсивности
		point_lights[0].color = veekay::vec4{
			sun_base_color.x * sun_intensity,
			sun_base_color.y * sun_intensity,
			sun_base_color.z * sun_intensity,
			0.0f};
	}

	veekay::vec3 sun_dir = veekay::vec3::normalized(veekay::vec3{-0.6f, -1.0f, -0.4f});
	lighting_params.directional_light.direction = veekay::vec4{sun_dir.x, sun_dir.y, sun_dir.z, 0.0f};
	float dir_intensity = std::clamp(dir_sun_intensity, 0.0f, 5.0f);
	lighting_params.directional_light.color = veekay::vec4{
		sun_base_color.x * dir_intensity,
		sun_base_color.y * dir_intensity,
		sun_base_color.z * dir_intensity,
		0.0f};

	// 0.7 Анимация звёзд: плавно мигают и респавнятся в новом месте после окончания жизни
	for (auto& star : stars) {
		star.time += dt_f;
		if (star.time >= star.lifetime) {
			star_spawner.respawn(star, rng);
		}
		float t_norm = (star.lifetime > 1e-5f) ? std::clamp(star.time / star.lifetime, 0.0f, 1.0f) : 0.0f;
		float pulse = std::sin(t_norm * float(M_PI)); // 0..1..0 вспышка на середине жизни
		float intensity = 0.2f + 0.8f * pulse;
		models[star.model_index].material.albedo_color = star.base_color * intensity;
	}

	// 0.8 Кометы: движение из случайной точки в другую, хвост ориентируем по направлению полёта, голова даёт точечный свет
	uint32_t min_point_lights = 1 + static_cast<uint32_t>(comets.size()); // слот 0 — Солнце, далее кометы
	if (min_point_lights > 8) min_point_lights = 8;
	point_light_count = std::max(point_light_count, min_point_lights);
	point_light_count = std::min<uint32_t>(8, point_light_count);
	for (size_t i = 0; i < comets.size(); ++i) {
		CometInstance& comet = comets[i];
		comet.t += dt_f;
		if (comet.t >= comet.duration) {
			comet_spawner.respawn(comet, rng);
		}

		veekay::vec3 path = comet.end - comet.start;
		float path_len = std::sqrt(path.x * path.x + path.y * path.y + path.z * path.z);
		float progress = (comet.duration > 1e-5f) ? std::clamp(comet.t / comet.duration, 0.0f, 1.0f) : 0.0f;
		veekay::vec3 head_pos = comet.start + comet.dir * (progress * path_len);

		float fade_in = (comet.fade_in > 1e-5f) ? std::clamp(comet.t / comet.fade_in, 0.0f, 1.0f) : 1.0f;
		float fade_out = (comet.fade_out > 1e-5f) ? std::clamp((comet.duration - comet.t) / comet.fade_out, 0.0f, 1.0f) : 1.0f;
		float vis = std::clamp(fade_in * fade_out, 0.0f, 1.0f);

		if (comet.model_index < models.size()) {
			float yaw = std::atan2(comet.dir.x, comet.dir.z) * 180.0f / float(M_PI);
			float pitch = std::asin(-comet.dir.y) * 180.0f / float(M_PI);
			Model& comet_model = models[comet.model_index];
			float base_length = comet.radius + comet.tail_base_stretch;
			float z_scale = (base_length > 1e-4f) ? (comet.tail_length / base_length) * vis : vis;
			comet_model.transform.scale = {1.0f, 1.0f, z_scale};
			comet_model.transform.position = head_pos;
			comet_model.transform.rotation = {pitch, yaw, 0.0f};
			comet_model.material.albedo_color = CometTailMat.albedo_color * vis;
			comet_model.material.specular_color = CometTailMat.specular_color;
			comet_model.material.shininess = CometTailMat.shininess;
			comet_model.material.opacity = CometTailMat.opacity;
		}

		if (comet.light_slot < 8) {
			point_lights[comet.light_slot].position = veekay::vec4{head_pos.x, head_pos.y, head_pos.z, 0.0f};
			point_lights[comet.light_slot].color = veekay::vec4{2.0f * vis, 2.0f * vis, 1.5f * vis, 0.0f}; // мягкое свечение головы
		}
	}

	// Update orbital positions
	// 0.3 Расчет эллиптических орбит (поворот, эксцентриситет) и привязка к родителю
	//     omega=2*pi/period, sim_years ускоряем коэффициентом orbit_speedup из UI
	double sim_years = time * (orbit_speedup * 0.1);
	for (const auto& orbit : orbits) {
		if (orbit.model_index >= models.size()) continue;

		veekay::vec3 center = {0.0f, 0.0f, 0.0f};
		if (orbit.parent_index != SIZE_MAX && orbit.parent_index < models.size()) {
			center = models[orbit.parent_index].transform.position;
		}

		auto angle = float(sim_years * orbit.omega + orbit.phase);

		float x_temp = orbit.a * std::cos(angle);
		float z_temp = orbit.b * std::sin(angle);

		float cos_r = std::cos(orbit.rotation_rad);
		float sin_r = std::sin(orbit.rotation_rad);

		float x = x_temp * cos_r - z_temp * sin_r;
		float z = x_temp * sin_r + z_temp * cos_r;

		models[orbit.model_index].transform.position = center + veekay::vec3{x, 0.0f, z};
	}

	// Вращение вокруг собственной оси
	for (const auto& spin : spin_targets) {
		if (spin.first < models.size()) {
			models[spin.first].transform.rotation.y -= dt_f * spin.second;
		}
	}
	// Вращаем вал в такт ускорению орбит
	if (shaft_model_index < models.size()) {
		models[shaft_model_index].transform.rotation.y += dt_f * orbit_speedup * 10.0f;
	}

	veekay::mat4 dir_shadow_matrix = veekay::mat4::identity();
	veekay::mat4 spot_shadow_matrix = veekay::mat4::identity();
	veekay::vec4 shadow_meta = {0.0f, -1.0f, 0.0f, 0.0f};
	{
		float scene_span = globe_radius + 15.0f;
		veekay::vec3 shadow_eye = camera.position - veekay::vec3{
			lighting_params.directional_light.direction.x,
			lighting_params.directional_light.direction.y,
			lighting_params.directional_light.direction.z
		} * 30.0f;
		dir_shadow_matrix = make_look_at(shadow_eye, camera.position) *
		                    make_ortho(-scene_span, scene_span, -scene_span, scene_span, 0.1f, 120.0f);
		directional_shadow.matrix = dir_shadow_matrix;
		*(veekay::mat4*)directional_shadow.matrix_buffer->mapped_region = dir_shadow_matrix;
	}

	int selected_spot = -1;
	float best_score = -1e9f;
	for (uint32_t i = 0; i < spotlight_count && i < 8; ++i) {
		veekay::vec3 pos = {spotlights[i].position.x, spotlights[i].position.y, spotlights[i].position.z};
		veekay::vec3 dir = veekay::vec3::normalized({spotlights[i].direction.x, spotlights[i].direction.y, spotlights[i].direction.z});
		veekay::vec3 beam_dir = {-dir.x, -dir.y, -dir.z};
		veekay::vec3 to_camera = veekay::vec3::normalized(camera.position - pos);
		float facing = std::max(0.0f, beam_dir.x * to_camera.x + beam_dir.y * to_camera.y + beam_dir.z * to_camera.z);
		veekay::vec3 diff = pos - camera.position;
		float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
		float score = facing * 3.0f - distance * 0.05f;
		if (score > best_score && facing > 0.1f) {
			best_score = score;
			selected_spot = static_cast<int>(i);
		}
	}
	if (selected_spot >= 0) {
		veekay::vec3 pos = {spotlights[selected_spot].position.x, spotlights[selected_spot].position.y, spotlights[selected_spot].position.z};
		veekay::vec3 dir = veekay::vec3::normalized({spotlights[selected_spot].direction.x, spotlights[selected_spot].direction.y, spotlights[selected_spot].direction.z});
		veekay::vec3 beam_dir = {-dir.x, -dir.y, -dir.z};
		float outer_cos = std::clamp(spotlights[selected_spot].cone_angles.y, -1.0f, 1.0f);
		float outer_angle = std::acos(outer_cos);
		float fov_deg = std::max(15.0f, outer_angle * 2.0f * 180.0f / float(M_PI));
		float near_plane = 0.1f;
		float far_plane = 60.0f;
		auto view = make_look_at(pos, pos + beam_dir);
		auto proj = veekay::mat4::projection(fov_deg, 1.0f, near_plane, far_plane);
		spot_shadow_matrix = view * proj;
		spot_shadow.matrix = spot_shadow_matrix;
		*(veekay::mat4*)spot_shadow.matrix_buffer->mapped_region = spot_shadow_matrix;
		shadow_meta = {1.0f, static_cast<float>(selected_spot), 0.0f, 0.0f};
	} else {
		spot_shadow.matrix = spot_shadow_matrix;
		*(veekay::mat4*)spot_shadow.matrix_buffer->mapped_region = spot_shadow_matrix;
	}

	SceneUniforms scene_uniforms{
		.view_projection = camera.view_projection(aspect_ratio),
		.camera_position = veekay::vec4{camera.position.x, camera.position.y, camera.position.z, 0.0f},
		.lighting = lighting_params,
		.time = veekay::vec4{static_cast<float>(time), 0.0f, 0.0f, 0.0f},
		.shadow_view_projection_dir = dir_shadow_matrix,
		.shadow_view_projection_spot = spot_shadow_matrix,
		.shadow_meta = shadow_meta,
	};
	scene_uniforms.shadow_meta.z = sun_point_shadow.far_plane;
	scene_uniforms.shadow_meta.w = sun_point_shadow.near_plane;

	// 2.5 Заполнение UBO с матрицей вида/проекции и позицией камеры для шейдеров
	std::vector<ModelUniforms> model_uniforms(models.size());
	for (size_t i = 0, n = models.size(); i < n; ++i) {
		const Model& model = models[i];
		ModelUniforms& uniforms = model_uniforms[i];

		uniforms.model = model.transform.matrix();
		uniforms.albedo_color = veekay::vec4{model.material.albedo_color.x, model.material.albedo_color.y, model.material.albedo_color.z, 0.0f};
		uniforms.specular_color = veekay::vec4{model.material.specular_color.x, model.material.specular_color.y, model.material.specular_color.z, 0.0f};
		uniforms.material = veekay::vec4{model.material.shininess, model.material.warp_strength, model.material.emissive_pulse, model.material.opacity};
	}

	*(SceneUniforms*)scene_uniforms_buffer->mapped_region = scene_uniforms;

	const size_t alignment =
		veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms));

	for (size_t i = 0, n = model_uniforms.size(); i < n; ++i) {
		const ModelUniforms& uniforms = model_uniforms[i];

		char* const pointer = static_cast<char*>(model_uniforms_buffer->mapped_region) + i * alignment;
		*reinterpret_cast<ModelUniforms*>(pointer) = uniforms;
	}

	// 2.6 Точечные источники в SSBO (std430): счётчик + массив для фрагментного шейдера
	{
		// Записываем количество по смещению 0, затем 16-байтное выравнивание
		uint32_t* count_ptr = static_cast<uint32_t*>(point_lights_buffer->mapped_region);
		*count_ptr = point_light_count;
		
		// Массив начинается с 16 байт (выравнивание std430)
		PointLight* lights_ptr = reinterpret_cast<PointLight*>(
			static_cast<char*>(point_lights_buffer->mapped_region) + 16);
		for (uint32_t i = 0; i < point_light_count && i < 8; i++) {
			lights_ptr[i] = point_lights[i];
		}
	}
	
	// Write spotlights to SSBO
	// 2.7 Передача прожекторов в SSBO (std430) для расчёта Блинн-Фонга в шейдере
	{
		// Записываем количество по смещению 0, затем 16-байтное выравнивание
		uint32_t* count_ptr = static_cast<uint32_t*>(spotlights_buffer->mapped_region);
		*count_ptr = spotlight_count;
		
		// Массив начинается с 16 байт (выравнивание std430)
		Spotlight* lights_ptr = reinterpret_cast<Spotlight*>(
			static_cast<char*>(spotlights_buffer->mapped_region) + 16);
		for (uint32_t i = 0; i < spotlight_count && i < 8; i++) {
			lights_ptr[i] = spotlights[i];
		}
	}

}

void render(VkCommandBuffer cmd, VkFramebuffer framebuffer) {
	vkResetCommandBuffer(cmd, 0);

	{ // NOTE: Start recording rendering commands
		VkCommandBufferBeginInfo info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		vkBeginCommandBuffer(cmd, &info);
	}

	const uint32_t model_stride = veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms));
	if (vkCmdBeginRenderingKHR && vkCmdEndRenderingKHR && !models.empty()) {
		record_shadow_pass(directional_shadow, cmd, static_cast<uint32_t>(models.size()), model_stride);
		record_shadow_pass(spot_shadow, cmd, static_cast<uint32_t>(models.size()), model_stride);
		if (point_light_count > 0) {
			veekay::vec3 sun_pos = {point_lights[0].position.x, point_lights[0].position.y, point_lights[0].position.z};
			record_point_shadow_pass(sun_point_shadow, cmd, static_cast<uint32_t>(models.size()), model_stride, sun_pos, static_cast<int>(sun_model_index));
		}
	}

	{ // NOTE: Use current swapchain framebuffer and clear it
		VkClearValue clear_color{.color = {{0.1f, 0.1f, 0.1f, 1.0f}}};
		VkClearValue clear_depth{.depthStencil = {1.0f, 0}};

		VkClearValue clear_values[] = {clear_color, clear_depth};

		VkRenderPassBeginInfo info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = veekay::app.vk_render_pass,
			.framebuffer = framebuffer,
			.renderArea = {
				.extent = {
					veekay::app.window_width,
					veekay::app.window_height
				},
			},
			.clearValueCount = 2,
			.pClearValues = clear_values,
		};

		vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
	}

	VkDeviceSize zero_offset = 0;

	auto draw_models_with_pipeline = [&](VkPipeline pipe, auto predicate) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

		VkBuffer current_vertex_buffer = VK_NULL_HANDLE;
		VkBuffer current_index_buffer = VK_NULL_HANDLE;

		const size_t model_uniorms_alignment =
			veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms));

		for (size_t i = 0, n = models.size(); i < n; ++i) {
			if (!predicate(i)) continue;

			const Model& model = models[i];
			const Mesh& mesh = model.mesh;

			if (current_vertex_buffer != mesh.vertex_buffer->buffer) {
				current_vertex_buffer = mesh.vertex_buffer->buffer;
				vkCmdBindVertexBuffers(cmd, 0, 1, &current_vertex_buffer, &zero_offset);
			}

			if (current_index_buffer != mesh.index_buffer->buffer) {
				current_index_buffer = mesh.index_buffer->buffer;
				vkCmdBindIndexBuffer(cmd, current_index_buffer, zero_offset, VK_INDEX_TYPE_UINT32);
			}

			uint32_t offset = i * model_uniorms_alignment;
			size_t tex_index = model.texture_set < texture_sets.size() ? model.texture_set : default_texture_set;
			VkDescriptorSet tex_set = (tex_index < texture_sets.size()) ? texture_sets[tex_index].descriptor_set : VK_NULL_HANDLE;
			if (tex_set == VK_NULL_HANDLE && default_texture_set < texture_sets.size()) {
				tex_set = texture_sets[default_texture_set].descriptor_set;
			}
			VkDescriptorSet sets[2] = {descriptor_set, tex_set};
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
			                    0, 2, sets, 1, &offset);

			vkCmdDrawIndexed(cmd, mesh.indices, 1, 0, 0, 0);
		}
	};

	// Opaque pass: draw everything except glass
	draw_models_with_pipeline(pipeline, [&](size_t idx) {
		return idx != glass_model_index;
	});

	// Transparent glass pass
	draw_models_with_pipeline(pipeline_glass, [&](size_t idx) {
		if (idx == glass_model_index) return true;
		if (saturn_ring_model_index < models.size() && idx == saturn_ring_model_index) return true;
		return false;
	});

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

} // namespace

int main() {
	return veekay::run({
		.init = initialize,
		.shutdown = shutdown,
		.update = update,
		.render = render,
	});
}
