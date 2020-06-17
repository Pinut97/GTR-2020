#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"

#include "application.h"
#include "scene.h"
#include "sphericalharmonics.h"

using namespace GTR;

class Application;

SphericalHarmonics sh;

Renderer::Renderer()
{
	deferred = true;
	shadow = false;

	use_ao = true;
	use_light = true;
	use_realtime_shadows = false;

	show_GBuffers = false;
	show_ao = false;
	show_deferred = true;

	fbo = nullptr;
	ssao_fbo = nullptr;
	blur_texture = new Texture();

	points.resize(64);
	points = GTR::generateSpherePoints(64, 1.0f, true);

	start_pos = Vector3(-55, 10, -170);
	end_pos = Vector3(180, 150, 80);
	//end_pos = Vector3(-50, 15, -160);
	dim = Vector3(2, 2, 2);

	delta = (end_pos - start_pos);
	delta.x /= dim.x - 1;
	delta.y /= dim.y - 1;
	delta.z /= dim.z - 1;

	irr_fbo = new FBO();
	irr_fbo->create(1024, 1024, 1, GL_RGB, GL_FLOAT);

	computeIrradiance();
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			if (shadow)
				renderPrefabShadowMap(node_model, node->mesh, node->material, camera);
			else if (deferred)
				renderMeshInDeferred(node_model, node->mesh, node->material, camera);
			else
				renderMeshWithMaterial(node_model, node->mesh, node->material, camera);
			//node->mesh->renderBounding(node_model, true);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;
	Texture* emissive_texture = NULL;

	texture = material->color_texture;
	texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == GTR::AlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	texture = material->color_texture;
	emissive_texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == GTR::AlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	shader = Shader::Get("light");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	glDepthFunc(GL_LEQUAL);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	if (Scene::getInstance()->lightEntities.empty())
	{
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH);

		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		shader->setUniform("u_camera_pos", camera->eye);
		shader->setUniform("u_model", model);
		shader->setUniform("u_ambient_light", Scene::getInstance()->ambientLight);

		shader->setUniform("u_color", material->color);
		if (texture)
			shader->setUniform("u_texture", texture, 0);
		if (emissive_texture)
			shader->setUniform("u_emissive_texture", emissive_texture, 1);
		shader->setUniform("u_emissive_factor", material->emissive_factor);

		//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
		shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::AlphaMode::MASK ? material->alpha_cutoff : 0);

		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);
	}
	else {

		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		shader->setUniform("u_camera_pos", camera->eye);
		shader->setUniform("u_model", model);
		shader->setUniform("u_factor", material->tilling_factor);

		for (size_t i = 0; i < Scene::getInstance()->lightEntities.size(); i++)
		{
			Light* light = Scene::getInstance()->lightEntities.at(i);

			if (!light->visible)
				continue;

			if (i == 0 && material->alpha_mode != GTR::AlphaMode::BLEND)
				glDisable(GL_BLEND);
			else if (i == 0 && material->alpha_mode == GTR::AlphaMode::BLEND)
			{
				glEnable(GL_BLEND);
			}
			else {
				glEnable(GL_BLEND);
				Scene::getInstance()->ambientLight = Vector3(0, 0, 0);
			}

			//upload uniforms
			shader->setUniform("u_ambient_light", Scene::getInstance()->ambientLight);

			shader->setUniform("u_is_cascade", light->is_cascade);
			if (light->light_type == lightType::SPOT || !light->is_cascade)
				shader->setUniform("u_shadow_viewprojection", light->camera->viewprojection_matrix);
			else if (light->light_type == lightType::DIRECTIONAL && light->is_cascade)
				shader->setMatrix44Array("u_shadow_viewprojection_array", light->shadow_viewprojection, 4);
			shader->setUniform("u_light_position", light->model.getTranslation());
			shader->setUniform("u_light_color", light->color);
			shader->setUniform("u_light_maxdist", light->maxDist);
			shader->setUniform("u_light_intensity", light->intensity);
			shader->setUniform1("u_light_type", (int)light->light_type);
			shader->setUniform("u_light_direction", light->model.frontVector());
			shader->setUniform("u_light_spot_cosine", (float)cos(DEG2RAD * light->angleCutoff));
			shader->setUniform("u_light_spot_exponent", light->spotExponent);
			shader->setUniform("u_shadow_map", (light->shadowMap) ? light->shadowMap : Texture::getWhiteTexture(), 3);

			shader->setUniform("u_color", material->color);
			if (texture)
				shader->setUniform("u_texture", texture, 0);
			if (emissive_texture)
				shader->setUniform("u_emissive_texture", emissive_texture, 1);
			shader->setUniform("u_emissive_factor", material->emissive_factor);

			//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
			shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::AlphaMode::MASK ? material->alpha_cutoff : 0);

			//do the draw call that renders the mesh into the screen
			mesh->render(GL_TRIANGLES);
		}
	}
	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
}


void Renderer::renderPrefabShadowMap(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	if (!mesh || !mesh->getNumVertices())
		return;

	Shader* shadow_shader = Shader::Get("flat");
	if (!shadow_shader)
		return;

	shadow_shader->enable();

	if (material->alpha_mode == GTR::AlphaMode::BLEND)
		return;

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	shadow_shader->setUniform("u_model", model);
	shadow_shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shadow_shader->setUniform("u_camera_pos", camera->eye);

	mesh->render(GL_TRIANGLES);

	shadow_shader->disable();

};

void Renderer::renderDeferred(Camera* camera)
{
	int width = Application::instance->window_width;
	int height = Application::instance->window_height;
	this->deferred = true;

	Shader* second_pass = NULL;
	Shader* ao_shader = NULL;

	//create fbo in case it hasn't been created before
	if (!this->fbo)
	{
		this->fbo = new FBO();
		this->fbo->create(width, height, 3, GL_RGB);
	}

	//first pass - Geometry
	this->fbo->bind();

	//glClearColor(0.1, 0.1, 0.1, 1.0);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	for (PrefabEntity* e : Scene::getInstance()->prefabEntities)
	{
		renderPrefab(e->model, e->pPrefab, camera);
	}

	//calculate the inverse of the viewprojection for future passes (ao and light pass)
	Matrix44 inverse_matrix = camera->viewprojection_matrix;
	inverse_matrix.inverse();
	Mesh* quad = Mesh::getQuad();

	this->fbo->unbind();

	//AMBIENT OCCLUSION pass

	if (use_ao) {

		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);

		if (!this->ssao_fbo)
		{
			this->ssao_fbo = new FBO();
			this->ssao_fbo->create(width, height);
			this->blur_texture->create(width, height);
		}

		this->ssao_fbo->bind();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		ao_shader = Shader::Get("ssao");
		ao_shader->enable();
		
		ao_shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		ao_shader->setUniform("u_inverse_viewprojection", inverse_matrix);
		ao_shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
		ao_shader->setUniform3Array("u_points", (float*)&points[0], points.size());
		
		ao_shader->setUniform("u_depth_texture", this->fbo->depth_texture, 0);	//pass the depth buffer calculated in the gbuffers
		ao_shader->setUniform("u_normal_texture", this->fbo->color_textures[1], 1);

		quad->render(GL_TRIANGLES);

		this->ssao_fbo->unbind();
		ao_shader->disable();

		Shader* blurShader = Shader::Get("blur");
		blurShader->enable();
		blurShader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
		this->ssao_fbo->color_textures[0]->copyTo(this->blur_texture, blurShader);
		blurShader->enable();
		blurShader->setUniform("u_iRes", Vector2(1.0 / (float)width * 2.0, 1.0 / (float)height) * 2.0);
		blur_texture->copyTo(this->ssao_fbo->color_textures[0], blurShader);
		blurShader->enable();
		blurShader->setUniform("u_iRes", Vector2(1.0 / (float)width * 4.0, 1.0 / (float)height) * 4.0);
		this->ssao_fbo->color_textures[0]->copyTo(this->blur_texture, blurShader);

		blurShader->disable();

	}
	
	//second pass - light

	if (use_light)
	{
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);

		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

		second_pass = Shader::Get("deferred_pospo");
		second_pass->enable();

		//camera pass
		second_pass->setUniform("u_camera_pos", camera->eye);
		second_pass->setUniform("u_inverse_viewprojection", inverse_matrix);
		second_pass->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));

		//texture pass
		second_pass->setUniform("u_color_texture", this->fbo->color_textures[0], 0);
		second_pass->setUniform("u_normal_texture", this->fbo->color_textures[1], 1);
		second_pass->setUniform("u_metal_roughness_texture", this->fbo->color_textures[2], 2);
		second_pass->setUniform("u_depth_texture", this->fbo->depth_texture, 3);
		if (use_ao && Scene::getInstance()->ambient_occlusion) {
			second_pass->setUniform("u_ao_texture", blur_texture ?
				blur_texture : Texture::getWhiteTexture(), 4);
		}
		else {
			second_pass->setUniform("u_ao_texture", Texture::getWhiteTexture(), 4);
		}

		//lights pass
		bool firstLight = true;

		//multipass
		for (size_t i = 0; i < Scene::getInstance()->lightEntities.size(); i++)	//pass for all lights
		{
			glDisable(GL_DEPTH_TEST);
			Light* light = Scene::getInstance()->lightEntities.at(i);
			if (!light->visible)
				continue;

			second_pass->enable();
			second_pass->setUniform("u_first_pass", firstLight);

			if (firstLight) {
				firstLight = false;
				glDisable(GL_BLEND);
				second_pass->setUniform("u_ambient_light", Scene::getInstance()->ambient_light 
					? Scene::getInstance()->ambientLight : Vector3(0.0f, 0.0f, 0.0f));
			}
			else {
				glEnable(GL_BLEND);
				glBlendFunc(GL_ONE, GL_ONE);
				glBlendEquation(GL_FUNC_ADD);
				assert(glGetError() == GL_NO_ERROR);
				second_pass->setUniform("u_ambient_light", Vector3(0.0f, 0.0f, 0.0f));
			}

			second_pass->setUniform("u_light_type", light->light_type);
			second_pass->setUniform("u_light_position", light->model.getTranslation());
			second_pass->setUniform("u_light_intensity", light->intensity);
			second_pass->setUniform("u_light_color", light->color);
			second_pass->setUniform("u_light_maxdist", light->maxDist);
			second_pass->setUniform("u_light_direction", light->model.frontVector());
			second_pass->setUniform("u_light_spot_cosine", (float)cos(DEG2RAD * light->angleCutoff));
			second_pass->setUniform("u_light_spot_inner_cosine", (float)cos(DEG2RAD* light->innerAngle));
			second_pass->setUniform("u_light_spot_exponent", light->spotExponent);
			second_pass->setUniform("u_light_bias", light->bias);

			if (light->shadowMap)
			{
				second_pass->setUniform("u_is_cascade", light->is_cascade);
				if (light->light_type == lightType::SPOT || !light->is_cascade)
					second_pass->setUniform("u_shadow_viewprojection", light->camera->viewprojection_matrix);
				else if (light->light_type == lightType::DIRECTIONAL && light->is_cascade)
					second_pass->setMatrix44Array("u_shadow_viewprojection_array", light->shadow_viewprojection, 4);
				second_pass->setUniform("u_shadow_map", (light->shadowMap) ? 
					light->shadowMap : Texture::getWhiteTexture(), 5);
			}

			second_pass->enable();
			quad->render(GL_TRIANGLES);	//render with blending for each light


		}
		//in case there is no lights, render the quad
		if (Scene::getInstance()->lightEntities.empty())
		{
			quad->render(GL_TRIANGLES);
			std::cout << Scene::getInstance()->ambientLight.x << std::endl;
			second_pass->setUniform("u_light_type", 3);
			second_pass->setUniform("u_ambient_light", Scene::getInstance()->ambient_light
				? Scene::getInstance()->ambientLight : Vector3(0.0f, 0.0f, 0.0f));
		}

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
	}

	second_pass->disable();

	renderShadowMap();
	renderGBuffers(camera);

	if (show_ao)
		blur_texture->toViewport();

	for each (sProbe p in probes)
		renderProbes(p.pos, 5.0f, (float*)&p.sh);
	
}

void Renderer::computeIrradiance()
{
	for(int z = 0; z < dim.z; z++)
		for(int y = 0; y < dim.y; y++)
			for (int x = 0; x < dim.x; x++)
			{
				sProbe p;
				p.local.set(x, y, z);

				p.index = x + y * dim.x + z * dim.x * dim.y;

				p.pos = start_pos + delta * Vector3(x, y, z);
				probes.push_back(p);
			}

	//probes.at(0).sh.coeffs[0].set(1.0f, 0.0f, 1.0f);

	for (auto& p: probes)
	{
		computeProbeCoeffs(p);
	}

}

void Renderer::computeProbeCoeffs(sProbe& p)
{
	Camera* cam = new Camera();
	FloatImage images[6];

	cam->setPerspective(90, 1, 0.1f, 1000.0f);

	//p.sh.coeffs[0].set(1.0f, 0.0f, 1.0f);
	
	for (int i = 0; i < 6; i++)
	{
		Vector3 eye = p.pos;
		Vector3 front = cubemapFaceNormals[i][2];
		Vector3 center = p.pos + front;
		Vector3 up = cubemapFaceNormals[i][1];
		cam->lookAt(eye, center, up);
		cam->enable();

		irr_fbo->bind();
		Scene::getInstance()->render(cam, this);
		irr_fbo->unbind();

		images[i].fromTexture(irr_fbo->color_textures[0]);
	}
	
	p.sh = computeSH(images);
}

void Renderer::renderShadowMap()
{
	for each (Light* light in Scene::getInstance()->lightEntities)
	{
		if (light->shadowMap && light->show_shadowMap)
		{
			glViewport(0, 0, 300, 300);
			glDisable(GL_BLEND);
			glEnable(GL_DEPTH_TEST);
			Shader* shader = Shader::Get("depth");
			shader->enable();
			shader->setUniform("u_camera_nearfar",
				Vector2(light->camera->near_plane, light->camera->far_plane));
			if (light->light_type == lightType::SPOT || light->light_type == lightType::POINT_LIGHT)
				light->shadowMap->toViewport(shader);
			else
				light->shadowMap->toViewport();
			shader->disable();
		}
		else if (light->show_camera)
		{
			glDisable(GL_BLEND);
			glEnable(GL_DEPTH_TEST);
			glViewport(0, 0, 300, 300);
			light->camera->enable();
			Scene::getInstance()->render(light->camera, this);
		}
	}
}

void Renderer::renderGBuffers(Camera* camera)
{
	float height = Application::instance->window_height;
	float width = Application::instance->window_width;

	if (show_GBuffers) {
		glViewport(0, height * 0.5, width * 0.5, height * 0.5);
		this->fbo->color_textures[0]->toViewport();
		glViewport(width * 0.5, height * 0.5, width * 0.5, height * 0.5);
		this->fbo->color_textures[1]->toViewport();
		glViewport(0, 0, width * 0.5, height * 0.5);
		this->fbo->color_textures[2]->toViewport();

		//depth channel
		glViewport(width * 0.5, 0, width * 0.5, height * 0.5);
		Shader* depth_shader = Shader::Get("depth");
		depth_shader->enable();
		depth_shader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));
		this->fbo->depth_texture->toViewport(depth_shader);
		depth_shader->disable();

		glViewport(0, 0, width, height);	//restore viewport size
	}
}

void Renderer::renderMeshInDeferred(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{

	if (!mesh || !mesh->getNumVertices())
		return;

	Texture* color_texture = NULL;
	Texture* emissive_texture = NULL;
	Texture* metal_roughness_texture = NULL;

	Shader* shader = Shader::Get("deferred");
	if (!shader)
		return;

	color_texture = material->color_texture;
	emissive_texture = material->emissive_texture;
	metal_roughness_texture = material->metallic_roughness_texture;

	shader->enable();

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDepthFunc(GL_LEQUAL);

	if (material->alpha_mode != GTR::AlphaMode::BLEND)
		glDisable(GL_BLEND);
	else {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	//camera uniforms
	shader->setUniform("u_model", model);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);

	//object uniforms
	shader->setUniform("u_color", material->color);
	shader->setUniform("u_color_texture", color_texture ? color_texture : Texture::getWhiteTexture(), 0);
	shader->setUniform("u_metal_roughness_texture", metal_roughness_texture ? metal_roughness_texture : Texture::getRedTexture(), 1);

	mesh->render(GL_TRIANGLES);

	shader->disable();

}

void GTR::Renderer::renderProbes(Vector3 pos, float size, float* coeffs)
{
	Camera* camera = Camera::current;
	Shader* shader = Shader::Get("probe");
	Matrix44 model;
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	model.setTranslation(pos.x, pos.y, pos.z);
	model.scale(size, size, size);
	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform3Array("u_coeffs", coeffs, 9);
	Mesh::Get("data/meshes/sphere.obj")->render(GL_TRIANGLES);
	shader->disable();
}

std::vector<Vector3> GTR::generateSpherePoints(int num,
	float radius, bool hemi)
{
	std::vector<Vector3> points;
	points.resize(num);
	for (int i = 0; i < num; i += 3)
	{
		Vector3& p = points[i];
		float u = random();
		float v = random();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random() * 0.9 + 0.1) * radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}
