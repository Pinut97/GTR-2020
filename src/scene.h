#pragma once

#ifndef SCENE_H
#define SCENE_H

#include <vector>
#include "entity.h"
#include "renderer.h"

class Scene {
private:
	static Scene* instance;
	Scene();
public:

	std::vector<PrefabEntity*> prefabEntities;
	std::vector<Light*> lightEntities;
	Vector3 ambientLight;
	Entity* gizmoEntity;

	unsigned int numPrefabEntities;
	unsigned int numLightEntities;
	bool ambient_light;

	FBO* fbo;

	static Scene* getInstance()
	{
		if (instance == NULL)
			instance = new Scene();
		return instance;
	}

	void render(Camera* camera, GTR::Renderer* renderer);
	void renderDeferred(Camera* camera, GTR::Renderer* renderer);
	void generateScene(Camera* camera);
	void generateTerrain(float size);
	void generateTestScene();
	void generateDepthMap(GTR::Renderer* renderer);
	void update(Camera* camera);
};

#endif // !SCENE_H