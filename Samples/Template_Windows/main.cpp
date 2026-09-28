#include "WickedEngine.h"
#include "wicked.h"

using namespace wi::ecs;
using namespace wi::scene;
using namespace wi::graphics;

Wicked_Camera g_wicked_camera;
Static_Entity* g_static_entities;
uint32_t g_static_entity_count;
Movable_Entity* g_movable_entities;
uint32_t g_movable_entity_count;
wi::unordered_map<size_t, Entity> g_prefab_map;

struct LoadPrefabRequest
{
	uint64_t prefab_hash;
	const uint8_t* data;
	size_t size;
};

constexpr uint32_t g_load_prefab_per_frame_max = 5;
std::deque<LoadPrefabRequest> g_load_prefab_requests;
std::mutex g_load_prefab_request_mutex;

class Tides_Renderer : public wi::RenderPath3D
{
	Entity sunlight = INVALID_ENTITY;

	wi::unordered_map<uint64_t, size_t> movable_object_map;
	wi::vector<Entity> movable_objects;

	wi::unordered_map<uint64_t, size_t> static_object_map;
	wi::vector<Entity> static_objects;

public:
	void Load() override
	{
		// TODO: Load from config
		setSSREnabled(false);
		setSSGIEnabled(false);
		setMotionBlurEnabled(false);
		setDepthOfFieldEnabled(false);
		setEyeAdaptionEnabled(false);
		setReflectionsEnabled(false);
		setBloomEnabled(true);
		setVolumeLightsEnabled(false);
		setLightShaftsEnabled(false);
		setFXAAEnabled(true);
		setFSREnabled(false);
		setFSR2Enabled(false);

		Scene& scene = wi::scene::GetScene();
		wi::renderer::ClearWorld(scene);

		// Create sun light
		{
			sunlight = CreateEntity();

			TransformComponent& transform = scene.transforms.Create(sunlight);
			transform.scale_local.x = 1.0f;
			transform.scale_local.y = 1.0f;
			transform.scale_local.z = 1.0f;
			transform.translation_local.x = 0.0f;
			transform.translation_local.y = 3.0f;
			transform.translation_local.z = 0.0f;
			transform.rotation_local.x = 0.632f;
			transform.rotation_local.y = 0.0f;
			transform.rotation_local.z = 0.0f;
			transform.rotation_local.w = 0.775f;

			LightComponent& light = scene.lights.Create(sunlight);
			light.SetCastShadow(true);
			light.SetType(LightComponent::LightType::DIRECTIONAL);
			light.color = XMFLOAT3(1.0f, 1.0f, 1.0f);
			light.intensity = 10.0f;
		}

		scene.weather = WeatherComponent();
		scene.weather.SetRealisticSky(true);
		scene.weather.SetVolumetricClouds(true);

		TransformComponent transform;
		transform.Translate(XMFLOAT3(0, 0, 0));
		transform.UpdateTransform();
		wi::scene::GetCamera().TransformCamera(transform);

		this->ClearSprites();
		this->ClearFonts();

		RenderPath3D::Load();

		static_object_map.clear();
		movable_object_map.clear();
	}

	void Update(float dt) override
	{
		CameraComponent& camera = wi::scene::GetCamera();

		TransformComponent transform;
		transform.Translate(XMFLOAT3(g_wicked_camera.position[0], g_wicked_camera.position[1], g_wicked_camera.position[2]));
		transform.Rotate(XMFLOAT4(g_wicked_camera.orientation[0], g_wicked_camera.orientation[1], g_wicked_camera.orientation[2], g_wicked_camera.orientation[3]));
		transform.UpdateTransform();
		camera.TransformCamera(transform);
		camera.zNearP = g_wicked_camera.near_plane;
		camera.zFarP = g_wicked_camera.far_plane;
		camera.fov = g_wicked_camera.fov_vertical;

		Scene& scene = wi::scene::GetScene();

		if (g_static_entities != nullptr)
		{
			for (uint32_t i = 0; i < g_static_entity_count; i++)
			{
				const Static_Entity& static_entity = g_static_entities[i];
				auto it = static_object_map.find(static_entity.game_entity);
				if (it == static_object_map.end())
				{
					auto prefab_entry = g_prefab_map.find(static_entity.prefab_hash);
					if (prefab_entry != g_prefab_map.end())
					{
						Entity entity = InstantiateEntity(prefab_entry->second, static_entity);
						static_object_map.emplace(static_entity.game_entity, static_objects.size());
						static_objects.emplace_back(entity);
					}
				}
			}
		}

		if (g_movable_entities != nullptr)
		{
			for (uint32_t i = 0; i < g_movable_entity_count; i++)
			{
				const Movable_Entity& movable_entity = g_movable_entities[i];
				auto it = movable_object_map.find(movable_entity.game_entity);
				if (it != movable_object_map.end())
				{
					Entity entity = movable_objects[it->second];
					TransformComponent* transform = scene.transforms.GetComponent(entity);
					transform->scale_local.x = movable_entity.scale[0];
					transform->scale_local.y = movable_entity.scale[1];
					transform->scale_local.z = movable_entity.scale[2];
					transform->translation_local.x = movable_entity.position[0];
					transform->translation_local.y = movable_entity.position[1];
					transform->translation_local.z = movable_entity.position[2];
					transform->rotation_local.x = movable_entity.orientation[0];
					transform->rotation_local.y = movable_entity.orientation[1];
					transform->rotation_local.z = movable_entity.orientation[2];
					transform->rotation_local.w = movable_entity.orientation[3];
					transform->SetDirty();
				}
				else
				{
					auto prefab_entry = g_prefab_map.find(movable_entity.prefab_hash);
					if (prefab_entry != g_prefab_map.end())
					{
						Entity entity = InstantiateEntity(prefab_entry->second, movable_entity);
						movable_object_map.emplace(movable_entity.game_entity, movable_objects.size());
						movable_objects.emplace_back(entity);
					}
				}
			}
		}

		process_prefab_load_requests();

		RenderPath3D::Update(dt);
	}

	void Render() const override
	{
		RenderPath3D::Render();
	}

private:
	void process_prefab_load_requests()
	{
		std::scoped_lock(g_load_prefab_request_mutex);
		Scene& scene = wi::scene::GetScene();
		uint32_t loaded_prefab_count = 0;

		while (!g_load_prefab_requests.empty() && loaded_prefab_count < g_load_prefab_per_frame_max)
		{
			LoadPrefabRequest request = g_load_prefab_requests[0];
			g_load_prefab_requests.pop_front();
			loaded_prefab_count++;

			// TODO: Make it so this function can return the actual entity that was created
			wi::scene::LoadModel(request.data, request.size);

			// HACK: Retrieve the loaded entity ID from the last object component
			Entity prefab = scene.objects.GetEntity(scene.objects.GetCount() - 1);

			ObjectComponent* object = scene.objects.GetComponent(prefab);
			if (object)
			{
				object->SetRenderable(false);
			}
			else
			{
				wi::backlog::post("\tPrefab DOES NOT have an object component",  wi::backlog::LogLevel::Warning);
			}

			NameComponent* name = scene.names.GetComponent(prefab);
			if (name)
			{
				char buf[256];
				memset(buf, 0, 256);
				sprintf_s(buf, 256, "%llu", request.prefab_hash);
				wi::backlog::post("\tPrefab '" + name->name + ":" + std::string(buf) + "' loaded succesfully", wi::backlog::LogLevel::Warning);
			}

			g_prefab_map.emplace(request.prefab_hash, prefab);
		}
	}

	Entity InstantiateEntity(Entity const& source_entity, Static_Entity const& static_entity)
	{
		Scene& scene = wi::scene::GetScene();

		Entity entity = CreateEntity();
		scene.layers.Create(entity).layerMask = ~0;

		TransformComponent& transform = scene.transforms.Create(entity);
		transform.scale_local.x = static_entity.scale[0];
		transform.scale_local.y = static_entity.scale[1];
		transform.scale_local.z = static_entity.scale[2];
		transform.translation_local.x = static_entity.position[0];
		transform.translation_local.y = static_entity.position[1];
		transform.translation_local.z = static_entity.position[2];
		transform.rotation_local.x = static_entity.orientation[0];
		transform.rotation_local.y = static_entity.orientation[1];
		transform.rotation_local.z = static_entity.orientation[2];
		transform.rotation_local.w = static_entity.orientation[3];
		transform.SetDirty();

		ObjectComponent& object = scene.objects.Create(entity);
		object.SetRenderable(true);
		object.SetCastShadow(true);
		object.SetDynamic(false);
		object.meshID = source_entity;

		return entity;
	}

	Entity InstantiateEntity(Entity const& source_entity, Movable_Entity const& movable_entity)
	{
		Scene& scene = wi::scene::GetScene();

		Entity entity = CreateEntity();
		scene.layers.Create(entity).layerMask = ~0;

		TransformComponent& transform = scene.transforms.Create(entity);
		transform.scale_local.x = movable_entity.scale[0];
		transform.scale_local.y = movable_entity.scale[1];
		transform.scale_local.z = movable_entity.scale[2];
		transform.translation_local.x = movable_entity.position[0];
		transform.translation_local.y = movable_entity.position[1];
		transform.translation_local.z = movable_entity.position[2];
		transform.rotation_local.x = movable_entity.orientation[0];
		transform.rotation_local.y = movable_entity.orientation[1];
		transform.rotation_local.z = movable_entity.orientation[2];
		transform.rotation_local.w = movable_entity.orientation[3];
		transform.SetDirty();

		ObjectComponent& object = scene.objects.Create(entity);
		object.SetRenderable(true);
		object.SetCastShadow(true);
		object.SetDynamic(true);
		object.meshID = source_entity;

		return entity;
	}
};

class Tides_Application : public wi::Application
{
	Tides_Renderer renderer;

public:
	~Tides_Application() override
	{
	}

	void Initialize() override
	{
		Application::Initialize();

		infoDisplay = {};
		infoDisplay.watermark = false;
		infoDisplay.fpsinfo = true;
		infoDisplay.device_name = true;
		infoDisplay.resolution = true;
		infoDisplay.vram_usage = true;

		renderer.init(canvas);
		renderer.Load();

		ActivatePath(&renderer);

		wi::eventhandler::SetVSync(vsyncEnabled);
		wi::profiler::SetEnabled(profilerEnabled);
	}

	void Compose(wi::graphics::CommandList cmd) override
	{
		Application::Compose(cmd);
	}

	bool vsyncEnabled = true;
	bool profilerEnabled = false;
};

Tides_Application application;

void wicked_set_shader_path(const char* path)
{
	wi::renderer::SetShaderPath(path);
}

void wicked_set_shader_source_path(const char* path)
{
	wi::renderer::SetShaderSourcePath(path);
}

void wicked_set_window(void* handle)
{
	application.SetWindow((HWND)handle);
}

void wicked_run(Wicked_Camera camera, Movable_Entity* movable_entities, uint32_t movable_entity_count, Static_Entity* static_entities, uint32_t static_entity_count)
{
	g_wicked_camera = camera;
	g_static_entities = static_entities;
	g_static_entity_count = static_entity_count;
	g_movable_entities = movable_entities;
	g_movable_entity_count = movable_entity_count;

	application.Run();
}

void wicked_shutdown()
{
	application.Exit();
	wi::jobsystem::ShutDown(); // waits for jobs to finish before shutdown
}

void wicked_toggle_profiler()
{
	application.profilerEnabled = !application.profilerEnabled;
	wi::profiler::SetEnabled(application.profilerEnabled);
}

void wicked_toggle_vsync()
{
	application.vsyncEnabled = !application.vsyncEnabled;
	wi::eventhandler::SetVSync(application.vsyncEnabled);
}

void wicked_toggle_info_displayer()
{
	application.infoDisplay.active = !application.infoDisplay.active;
}

void wicked_load_prefab_from_memory(uint64_t prefab_hash, const uint8_t* data, size_t size)
{
	std::scoped_lock(g_load_prefab_request_mutex);
	LoadPrefabRequest request = {};
	request.prefab_hash = prefab_hash;
	request.size = size;
	// NOTE: The file streaming is going to hold on to this data for a while
	// so it should be safe to just pass a pointer around
	request.data = data;
	g_load_prefab_requests.emplace_back(request);
}
