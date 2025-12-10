#version 450

layout (location = 0) in vec3 f_position;
layout (location = 1) in vec3 f_normal;
layout (location = 2) in vec2 f_uv;

layout (location = 0) out vec4 final_color;

struct DirectionalLight {
	vec4 direction;
	vec4 color;
};

struct PointLight {
	vec4 position;
	vec4 color;
};

struct Spotlight {
	vec4 position;
	vec4 direction;
	vec4 color;
	vec4 cone_angles;
};

struct LightingData {
	vec4 ambient_color;
	DirectionalLight directional_light;
};

layout (binding = 0, std140) uniform SceneUniforms {
	mat4 view_projection;
	vec4 camera_position;
	LightingData lighting;
} scene;

layout (binding = 1, std140) uniform ModelUniforms {
	mat4 model;
	vec4 albedo_color;
	vec4 specular_color;
	vec4 material;
} model_data;

layout (binding = 2, std430) readonly buffer PointLightsBuffer {
	uint count;
	uint _pad0;
	uint _pad1;
	uint _pad2;
	PointLight lights[];
} point_lights;

layout (binding = 3, std430) readonly buffer SpotlightsBuffer {
	uint count;
	uint _pad0;
	uint _pad1;
	uint _pad2;
	Spotlight lights[];
} spotlights;

void main() {
	vec3 N = normalize(f_normal);
	vec3 V = normalize(scene.camera_position.xyz - f_position);
	
	// Material properties for Blinn-Phong
	vec3 k_a = model_data.albedo_color.xyz;  // Ambient
	vec3 k_d = model_data.albedo_color.xyz; // Diffuse
	vec3 k_s = model_data.specular_color.xyz; // Specular
	float shininess = model_data.material.x; // Shininess

	// Unlit/emissive if shininess is negative (used for the Sun mesh)
	if (shininess < 0.0) {
		final_color = vec4(k_d, 1.0);
		return;
	}
	
	vec3 ambient = scene.lighting.ambient_color.xyz * k_a;
	vec3 result = ambient;
	
	// Directional light
	// Direction already points toward the light source
	vec3 L_dir = normalize(scene.lighting.directional_light.direction.xyz);
	float NdotL_dir = max(dot(N, L_dir), 0.0);
	
	if (NdotL_dir > 0.0) {
		// Diffuse
		vec3 diffuse = k_d * scene.lighting.directional_light.color.xyz * NdotL_dir;
		
		// Blinn-Phong specular
		vec3 H_dir = normalize(L_dir + V);
		float NdotH_dir = max(dot(N, H_dir), 0.0);
		vec3 specular = k_s * scene.lighting.directional_light.color.xyz * pow(NdotH_dir, shininess);
		
		result += diffuse + specular;
	}
	
	// Point lights with inverse-square law attenuation
	uint point_light_count = point_lights.count;
	for (uint i = 0; i < point_light_count && i < 8; i++) {
		vec3 L_vec = point_lights.lights[i].position.xyz - f_position;
		float distance = length(L_vec);
		vec3 L = normalize(L_vec);
		
		// Inverse-square law attenuation: 1 / distance^2
		float attenuation = 1.0 / (distance * distance);
		
		float NdotL = max(dot(N, L), 0.0);
		
		if (NdotL > 0.0) {
			// Diffuse component
			vec3 diffuse = k_d * point_lights.lights[i].color.xyz * NdotL * attenuation;
			
			// Blinn-Phong specular component
			vec3 H = normalize(L + V);
			float NdotH = max(dot(N, H), 0.0);
			vec3 specular = k_s * point_lights.lights[i].color.xyz * pow(NdotH, shininess) * attenuation;
			
			result += diffuse + specular;
		}
	}
	
	// Spotlights with cone angle attenuation
	uint spotlight_count = spotlights.count;
	for (uint i = 0; i < spotlight_count && i < 4; i++) {
		vec3 L_vec = spotlights.lights[i].position.xyz - f_position;
		float distance = length(L_vec);
		vec3 L = normalize(L_vec);
		vec3 spot_dir = normalize(spotlights.lights[i].direction.xyz);
		
		// Calculate angle between light direction and spotlight direction
		float cos_angle = dot(-L, spot_dir);
		
		// Smooth falloff between inner and outer cone
		float inner_cone = spotlights.lights[i].cone_angles.x;
		float outer_cone = spotlights.lights[i].cone_angles.y;
		float spot_factor = smoothstep(outer_cone, inner_cone, cos_angle);
		
		// Inverse-square law attenuation
		float distance_attenuation = 1.0 / (distance * distance);
		float attenuation = distance_attenuation * spot_factor;
		
		float NdotL = max(dot(N, L), 0.0);
		
		if (NdotL > 0.0 && spot_factor > 0.0) {
			// Diffuse component
			vec3 diffuse = k_d * spotlights.lights[i].color.xyz * NdotL * attenuation;
			
			// Blinn-Phong specular component
			vec3 H = normalize(L + V);
			float NdotH = max(dot(N, H), 0.0);
			vec3 specular = k_s * spotlights.lights[i].color.xyz * pow(NdotH, shininess) * attenuation;
			
			result += diffuse + specular;
		}
	}
	
	final_color = vec4(result, 1.0f);
}
