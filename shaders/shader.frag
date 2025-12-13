#version 450

layout (location = 0) in vec3 f_position;
layout (location = 1) in vec3 f_normal;
layout (location = 2) in vec2 f_uv;

layout (location = 0) out vec4 final_color;

layout (set = 1, binding = 0) uniform sampler2D tex_albedo;
layout (set = 1, binding = 1) uniform sampler2D tex_specular;
layout (set = 1, binding = 2) uniform sampler2D tex_emissive;

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
	mat4 view_projection;   // 2.5 Матрица вида*проекции из CPU (transpose внутри движка)
	vec4 camera_position;   // 2.5 Позиция камеры в мировых координатах
	LightingData lighting;  // 2.5 Фоновый свет и направленный источник
	vec4 time;              // x = time
} scene;

layout (binding = 1, std140) uniform ModelUniforms {
	mat4 model;          // 2.5 Матрица модели (мировое преобразование)
	vec4 albedo_color;   // 2.5 Диффузный/альбедо цвет
	vec4 specular_color; // 2.5 Цвет блика
	vec4 material;       // 2.5 x = shininess (экспонента блеска), <0 => эмиссия, y = warp, w = opacity
} model_data;

layout (binding = 2, std430) readonly buffer PointLightsBuffer {
	uint count;   // 2.6 Кол-во точечных источников
	uint _pad0;
	uint _pad1;
	uint _pad2;
	PointLight lights[]; // 2.6 Массив точечных источников (std430)
} point_lights;

layout (binding = 3, std430) readonly buffer SpotlightsBuffer {
	uint count;   // 2.7 Кол-во прожекторов
	uint _pad0;
	uint _pad1;
	uint _pad2;
	Spotlight lights[]; // 2.7 Массив прожекторов (std430)
} spotlights;

void main() {
	vec3 N = normalize(f_normal);
	vec3 V = normalize(scene.camera_position.xyz - f_position);
	
	// 2.8 Свойства материала для Блинн-Фонга
	float warp = max(model_data.material.y, 0.0);
	vec2 uv = f_uv;
	if (warp > 0.0) {
		float t = scene.time.x;
		uv += vec2(t * 0.02, t * 0.01); // gentle drift
		vec2 centered = uv - vec2(0.5);
		float r = length(centered);
		float core = 1.0 - smoothstep(0.2, 0.9, r);
		// core swirl + shallow radial wave
		float twist = core * warp * 0.07 * sin(t * 1.4 + r * 12.0);
		float c = cos(twist);
		float s = sin(twist);
		centered = vec2(c * centered.x - s * centered.y, s * centered.x + c * centered.y);
		centered *= (1.0 + core * warp * 0.02 * sin(r * 15.0 - t * 1.5));
		uv = vec2(0.5) + centered;
	}
	vec4 albedo_sample = texture(tex_albedo, uv);
	vec4 spec_sample = texture(tex_specular, uv);
	vec4 emissive_sample = texture(tex_emissive, uv);

	float r_center = length(f_uv - vec2(0.5));
	float emissive_pulse = max(model_data.material.z, 0.0);
	float pulse = 1.0 + emissive_pulse * 0.15 * sin(scene.time.x * 2.0 - r_center * 15.0);
	albedo_sample.rgb *= pulse;
	emissive_sample.rgb *= pulse;

	vec3 k_a = model_data.albedo_color.xyz * albedo_sample.rgb;   // Фоновая/диффузная часть
	vec3 k_d = model_data.albedo_color.xyz * albedo_sample.rgb;   // Диффузная часть
	vec3 k_s = model_data.specular_color.xyz * spec_sample.rgb; // Бликовая часть
	float shininess = model_data.material.x;  // Степень блеска (экспонента)
	float alpha = clamp(model_data.material.w * albedo_sample.a, 0.0, 1.0);

	// 0.6 Эмиссия: при отрицательном shininess не считаем освещение, выводим базовый цвет (Солнце)
	if (shininess < 0.0) {
		final_color = vec4((k_d + emissive_sample.rgb), alpha);
		return;
	}
	
	// 2.8 Фоновая составляющая
	vec3 ambient = scene.lighting.ambient_color.xyz * k_a;
	vec3 result = ambient;
	
	// 2.8 Направленный источник (остался в API): стандартный Blinn-Phong
	// Направление уже указывает в сторону источника
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
	
	// 2.8 Точечные источники из SSBO: закон обратных квадратов + Blinn-Phong
	// Считаем затухание 1/dist^2 и добавляем диффуз/блик для каждого источника
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
	
	// 2.9 Прожекторы из SSBO: гладкий край через smoothstep(inner/outer)
	// Учитываем угол между лучом и осью прожектора, делаем мягкий спад между inner/outer
	uint spotlight_count = spotlights.count;
	for (uint i = 0; i < spotlight_count && i < 8; i++) {
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
	
	result += emissive_sample.rgb;
	final_color = vec4(result, alpha);
}
