
#include "entity.h"
#include "includes.h"
#include "scene.h"
#include "camera.h"
#include "application.h"

#include <iostream>

#include "mesh.h"

#include "shader.h"

class Application;

PrefabEntity::PrefabEntity(GTR::Prefab* pPrefab_)
{
	id = Scene::getInstance()->numPrefabEntities;
	Scene::getInstance()->numPrefabEntities++;
	entity_type = eType::PREFAB;
	Matrix44 model_;
	name = "Prefab";
	model = model_;
	visible = true;
	selected = false;
	pPrefab = pPrefab_;
	factor = 1;
}

void PrefabEntity::render(Camera* camera, GTR::Renderer* renderer) {
	renderer->renderPrefab(model, this->pPrefab, camera);
}

void PrefabEntity::renderInMenu()
{
	ImGui::Text("Name: %s", name.c_str());

	ImGui::Checkbox("Active", &visible);

	if (ImGui::Button("Select"))
		Scene::getInstance()->gizmoEntity = this;

	if (ImGui::TreeNode((void*)this, "Model"))
	{
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(model.m, matrixTranslation, matrixRotation, matrixScale);
		ImGui::DragFloat3("Position", matrixTranslation, 0.1f);
		ImGui::DragFloat3("Rotation", matrixRotation, 0.1f);
		ImGui::DragFloat3("Scale", matrixScale, 0.1f);
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, model.m);

		ImGui::TreePop();
	}

	//Light color
	//ImGui::ColorEdit4("Color", (float*)&this->pPrefab->root.material->color);

//	ImGui::PopStyleColor();
}

Light::Light(lightType type_)
{
	light_type = type_;
	entity_type = eType::LIGHT;
	
	intensity = 1.0f;
	maxDist = 100.0f;
	color = Vector3(1, 1, 1);
	initial_position = model.getTranslation();
	visible = true;
	bias = 0.001f;

	is_cascade = false;
	renderedHighShadow = false;

	angleCutoff = 30;
	innerAngle = 15;	//angleCutoff* (2.0f / 3.0f);
	spotExponent = 0;
	cascade_size = 256;

	//debug
	show_shadowMap = false;
	show_camera = false;
	far_directional_shadowmap_updated = false;

	fbo = NULL;
	shadowMap = new Texture();

	camera = new Camera();
	camera->projection_matrix = model;
	camera->lookAt(model.getTranslation(), Vector3(1,0,0), Vector3(0, 1, 0));

	if (type_ == lightType::AMBIENT) { name = "Ambient light"; }
	else if (type_ == lightType::SPOT) {
		name = "Spot light";
		camera->setPerspective(
			angleCutoff * 2,
			Application::instance->window_width / (float)Application::instance->window_height,
			1.0f, 1000.0f);
	}
	else if (type_ == lightType::POINT_LIGHT) { 
		name = "Point light"; 
		camera->setPerspective(
			angleCutoff * 2,
			Application::instance->window_width / (float)Application::instance->window_height,
			1.0f, 1000.0f);
	}
	else {
		name = "Directional light";
		camera->setOrthographic(-128, 128, -128, 128, -500, 5000);
	}
}

void Light::renderInMenu()
{
	ImGui::Text("Name: %s", name.c_str()); // Edit 3 floats representing a color

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));

	ImGui::Checkbox("Active", &visible);

	ImGui::DragFloat("Intensity", &intensity);
	ImGui::DragFloat("Max Distance", &maxDist);
	ImGui::DragFloat("Bias", &bias, 0.001f);

	if (ImGui::Button("Selected"))
		Scene::getInstance()->gizmoEntity = this;

	//Model edit
	if (this->light_type != lightType::AMBIENT)	//except for ambient light, which does not have a model
	{
		ImGui::Checkbox("Shadow map", &show_shadowMap);
		ImGui::Checkbox("Show camera", &show_camera);
		if (this->light_type == lightType::DIRECTIONAL)
			ImGui::Checkbox("Activate cascade", &this->is_cascade);
		if (this->light_type == lightType::SPOT) {
			ImGui::DragFloat("Outter Angle", &this->angleCutoff);
			ImGui::DragFloat("Inner Angle", &this->innerAngle);
		}

		if (ImGui::TreeNode(camera, "Camera light")) {
			camera->renderInMenu();
			ImGui::TreePop();
		}

		if (ImGui::TreeNode((void*)this, "Model"))
		{
			float matrixTranslation[3], matrixRotation[3], matrixScale[3];
			ImGuizmo::DecomposeMatrixToComponents(model.m, matrixTranslation, matrixRotation, matrixScale);
			ImGui::DragFloat3("Position", matrixTranslation, 0.1f);
			ImGui::DragFloat3("Rotation", matrixRotation, 0.1f);
			ImGui::DragFloat3("Scale", matrixScale, 0.1f);
			ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, model.m);
			
			ImGui::TreePop();
		}
	}
	else	//to change ambient light intensity
	{
		ImGui::Checkbox("Ambient Light", &visible);
	}

	//Light color
	ImGui::ColorEdit4("Color", (float*)&color);

	ImGui::PopStyleColor();
}

void Light::setPosition(Vector3 pos) { this->model.translate(pos.x, pos.y, pos.z); }
void Light::setPosition(float x, float y, float z) { this->model.translate(x, y, z); }
void Light::setColor(float r, float g, float b) { this->color = Vector3(r, g, b); }

void Light::renderShadowMap(GTR::Renderer* renderer, Camera* user_camera)
{
	if (this->light_type != lightType::SPOT && this->light_type != lightType::DIRECTIONAL &&
		this->light_type != lightType::POINT_LIGHT)
		return;

	int w = 1024;
	int h = 1024;

	renderer->shadow = true;

	if (!this->fbo)
	{
		if (light_type != lightType::POINT_LIGHT)
		{
			this->fbo = new FBO();
			this->fbo->setDepthOnly(w, h);
			this->shadowMap->create(fbo->depth_texture->width, fbo->depth_texture->height);
		}
		else
		{
			this->fbo = new FBO();
			this->fbo->setDepthOnly(w, h);
			this->shadowMap->create(fbo->depth_texture->width, fbo->depth_texture->height);
		}

	}

	if (!this->camera)
		return;

	this->fbo->bind();

	this->camera->enable();

	if (light_type == lightType::SPOT)
	{
		renderSpotShadowMap(renderer);
	}
	else if( light_type == lightType::DIRECTIONAL){

		renderDirectionalShadowMap(renderer, is_cascade, user_camera);
	}

	this->fbo->unbind();

	renderer->shadow = false;
	glDisable(GL_DEPTH_TEST);

	float width = fbo->depth_texture->width;
	float height = fbo->depth_texture->height;

	this->shadowMap = this->fbo->depth_texture;

}

void Light::renderSpotShadowMap(GTR::Renderer* renderer)
{
	int w = this->fbo->depth_texture->width;
	int h = this->fbo->depth_texture->height;

	this->camera->lookAt(this->model.getTranslation(), 
		this->model.getTranslation() + this->model.frontVector(),
		Vector3(0, 1, 0));

	glClear(GL_DEPTH_BUFFER_BIT);
	glViewport(0, 0, w, h);

	for (auto& entity : Scene::getInstance()->prefabEntities)
	{
		renderer->renderPrefab(entity->model, entity->pPrefab, this->camera);
	}
}

void Light::renderDirectionalShadowMap(GTR::Renderer* renderer, bool is_cascade, Camera* user_camera)
{

	float texture_width = this->fbo->depth_texture->width;
	float texture_height = this->fbo->depth_texture->height;
	float w = cascade_size;
	float h = cascade_size;
	float grid;

	glClear(GL_DEPTH_BUFFER_BIT);

	Texture* background = Texture::getWhiteTexture();
	this->camera->eye = user_camera->center + this->target_vector;
	this->camera->lookAt(this->camera->eye, user_camera->center, Vector3(0, 1, 0));

	if (!is_cascade)
	{
		glViewport(0, 0, texture_width, texture_height);
		this->camera->setOrthographic(-w , w , -h , h ,
			this->camera->near_plane, this->camera->far_plane);

		grid = w / (texture_width);
		camera->view_matrix.M[3][1] = round(camera->view_matrix.M[3][1] / grid) * grid;
		camera->view_matrix.M[3][0] = round(camera->view_matrix.M[3][0] / grid) * grid;

		this->camera->viewprojection_matrix = camera->view_matrix * camera->projection_matrix;

		for (auto& entity : Scene::getInstance()->prefabEntities)
		{
			renderer->renderPrefab(entity->model, entity->pPrefab, this->camera);
		}
	}
	else {

		for (int i = 1; i <= 4; i++)
		{
			this->camera->setOrthographic(-w * i, w * i, -h * i, h * i,
				this->camera->near_plane, this->camera->far_plane);

			this->camera->updateProjectionMatrix();

			switch (i)
			{
			case 1:
				glViewport(0, 0, texture_width / 2.0f, texture_height / 2.0f);
				grid = w / (texture_width * 0.5f); break;
			case 2:
				glViewport(texture_width / 2.0f, 0, texture_width / 2.0f, texture_height / 2.0f); 
				grid = (w * 2.0) / (this->fbo->depth_texture->width / 2); break;
			case 3:
				glViewport(0, texture_height / 2, texture_width / 2, texture_height / 2); 
				grid = (w * 3.0) / (this->fbo->depth_texture->width / 2); break;
			case 4:
				glViewport(texture_width / 2, texture_height / 2, texture_width / 2, texture_height / 2); 
				grid = (w * 4.0) / (this->fbo->depth_texture->width / 2); break;
			default:
				break;
			}

			//in order to find the size of each pixel in world coordinates we need to take the width of the frustum
			//divided by the texture size. Since the texture is an atlas texture we have to divide it by the size of 
			//each real texture and not the whole texture (in this case just the half of the whole texture since each
			//texture occupies a quarter of the whole)
			//once the calculations are done, we round the position of the camera to make it fit into the grid

			camera->view_matrix.M[3][1] = round(camera->view_matrix.M[3][1] / grid) * grid;
			camera->view_matrix.M[3][0] = round(camera->view_matrix.M[3][0] / grid) * grid;
			this->camera->viewprojection_matrix = camera->view_matrix * camera->projection_matrix;

			this->shadow_viewprojection[i - 1] = camera->viewprojection_matrix;

			for (auto& entity : Scene::getInstance()->prefabEntities)
			{
				renderer->renderPrefab(entity->model, entity->pPrefab, this->camera);
			}
		}
	}
}