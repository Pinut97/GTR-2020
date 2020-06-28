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
	Light* sun;

	unsigned int numPrefabEntities;
	unsigned int numLightEntities;
	bool ambient_light;
	bool ambient_occlusion;

	FBO* fbo;

	static Scene* getInstance()
	{
		if (instance == NULL)
			instance = new Scene();
		return instance;
	}

	void render(Camera* camera, GTR::Renderer* renderer);
	void renderDeferred(Camera* camera, GTR::Renderer* renderer);
	void renderForward(Camera* camera, GTR::Renderer* renderer);
	void generateScene(Camera* camera);
	void generateTerrain(float size);
	void generateTestScene();
	void generateSecondScene(Camera* camera);
	void generateDepthMap(GTR::Renderer* renderer, Camera* camera);
};

#endif // !SCENE_H