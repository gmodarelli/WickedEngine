#include "WickedEngine.h"
#include "wicked.h"

using namespace wi::ecs;
using namespace wi::scene;
using namespace wi::graphics;

std::hash<std::string> string_hasher;

const char* g_tripod_prefab_path = "content\\prefabs\\pb_fire_tripod.wiscene";

Wicked_Camera g_wicked_camera;
Movable_Entity* g_movable_entities;
uint32_t g_movable_entity_count;
wi::unordered_map<size_t, Entity> g_prefab_map;

class Tides_Renderer : public wi::RenderPath3D
{
	Entity sunlight = INVALID_ENTITY;

	wi::unordered_map<uint64_t, size_t> movable_object_map;
	wi::vector<Entity> movable_objects;

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

		// // Load prefabs test
		// {
		// 	wicked_load_prefab(g_tripod_prefab_path);
		// }

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
		auto prefab_entry = g_prefab_map.find(string_hasher(g_tripod_prefab_path));

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
					if (prefab_entry != g_prefab_map.end())
					{
						Entity entity = InstantiateMovableEntity(prefab_entry->second, movable_entity);
						movable_object_map.emplace(movable_entity.game_entity, movable_objects.size());
						movable_objects.emplace_back(entity);
					}
				}
			}
		}

		RenderPath3D::Update(dt);
	}

	void Render() const override
	{
		RenderPath3D::Render();
	}

private:
	Entity InstantiateMovableEntity(Entity const& source_entity, Movable_Entity const& movable_entity)
	{
		Scene& scene = wi::scene::GetScene();

		Entity entity = CreateEntity();

		TransformComponent& transform = scene.transforms.Create(entity);
		ObjectComponent& object = scene.objects.Create(entity);

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

		infoDisplay.active = true;
		infoDisplay.watermark = false;
		infoDisplay.resolution = false;
		infoDisplay.fpsinfo = true;

		renderer.init(canvas);
		renderer.Load();

		ActivatePath(&renderer);
	}

	void Compose(wi::graphics::CommandList cmd) override
	{
		Application::Compose(cmd);
	}
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

void wicked_run(Wicked_Camera camera, Movable_Entity* movable_entities, uint32_t movable_entity_count)
{
	g_wicked_camera = camera;
	g_movable_entities = movable_entities;
	g_movable_entity_count = movable_entity_count;

	application.Run();
}

void wicked_shutdown()
{
	application.Exit();
	wi::jobsystem::ShutDown(); // waits for jobs to finish before shutdown
}

void wicked_load_prefab(const char* prefab_path)
{
	Entity prefab = wi::scene::LoadModel(prefab_path);

	// TODO(pixeljuice): Figure out why we get a null reference here
	Scene& scene = wi::scene::GetScene();
	ObjectComponent* object = scene.objects.GetComponent(prefab);
	if (object) object->SetRenderable(false);

	size_t prefab_key = string_hasher(prefab_path);
	g_prefab_map.emplace(prefab_key, prefab);
}
