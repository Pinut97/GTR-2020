#pragma once

#ifndef ENTITY_H
#define ENTITY_H

#include "framework.h"
#include "prefab.h"
#include "renderer.h"
#include "fbo.h"
#include <iostream>

enum lightType {
	DIRECTIONAL,
	POINT_LIGHT,
	SPOT,
	AMBIENT
};

enum eType {
	BASE_NODE,
	PREFAB,
	LIGHT
};

class Entity {
public:
	unsigned int id;
	Matrix44 model;
	bool visible;
	bool selected;
	std::string name;
	eType entity_type;

	virtual void render(Camera* camera, GTR::Renderer* renderer) = 0;
	virtual void renderInMenu() = 0;
};

class PrefabEntity : public Entity {
public:

	PrefabEntity(GTR::Prefab* pPrefab);

	GTR::Prefab* pPrefab;
	float factor;	//factor for uv coordinates

	void render(Camera* camera, GTR::Renderer* renderer);
	void renderDeferred(Camera* camera, GTR::Renderer* renderer);
	void renderInMenu();
	void setPosition(float x, float y, float z) { this->model.translate(x, y, z); }
};

class Light : public Entity {
public:

	Vector3 color;
	Vector3 target_vector;
	Vector3 initial_position;

	float intensity;
	float maxDist;
	float bias;
	float cascade_size;

	Camera* camera;
	Texture* shadowMap;
	FBO* fbo;
	Mesh* mesh;

	Matrix44 shadow_viewprojection[4];
	Matrix44 shadow_cubemap[6];

	lightType light_type;

	//spot parameters
	float angleCutoff;
	float innerAngle;
	float spotExponent;

	bool far_directional_shadowmap_updated;
	bool is_cascade;	//only for directional lights
	
	//debug parameters
	bool show_shadowMap;	//tell if shadow map is being shown
	bool show_camera;	//debuggin purposes
	bool renderedHighShadow;

	//METHODES
	Light(lightType type_);

	void render(Camera* camera, GTR::Renderer* renderer) {};
	void renderInMenu();
	void setPosition(Vector3 pos);
	void setPosition(float x, float y, float z);
	void setColor(float r, float g, float b);

	void renderShadowMap(GTR::Renderer* renderer, Camera* user_camera);

private:
	void renderDirectionalShadowMap(GTR::Renderer* renderer, bool is_cascade, Camera* user_camera);
	void renderSpotShadowMap(GTR::Renderer* renderer);
};

#endif // !ENTITY_H