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
#include "extra/hdre.h"

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
	use_irradiance = false;
	use_reflection = true;
	use_deferred = true;
	use_volumetric = true;

	show_GBuffers = false;
	show_ao = false;
	show_deferred = true;
	show_irr_probes = false;
	show_irradiance = false;
	show_reflection_probes = false;
	show_probe_coefficients_texture = false;

	fbo = nullptr;
	ssao_fbo = nullptr;
	probes_texture = nullptr;
	blur_texture = new Texture();
	environment = CubemapFromHDRE("data/panorama.hdre");

	points.resize(64);
	points = GTR::generateSpherePoints(64, 1.0f, true);

	irr_start_pos = Vector3(-350, 10, -350);
	irr_end_pos = Vector3(400, 250, 130);
	irr_dim = Vector3(8, 6, 12);
	irr_num_probes = irr_dim.x * irr_dim.y * irr_dim.z;

	irr_delta = (irr_end_pos - irr_start_pos);
	irr_delta.x /= irr_dim.x - 1;
	irr_delta.y /= irr_dim.y - 1;
	irr_delta.z /= irr_dim.z - 1;

	irr_fbo = new FBO();
	irr_fbo->create(64, 64, 1, GL_RGB, GL_FLOAT);

	reflections_fbo = new FBO();

	//create reflexion probes
	sReflectionProbe* reflection_probe_1 = new sReflectionProbe;

	reflection_probe_1->pos.set(180, 100, -225);
	reflection_probe_1->cubemap = new Texture();
	reflection_probe_1->cubemap->createCubemap(512, 512, NULL, GL_RGB, GL_UNSIGNED_INT, false);
	reflection_probes.push_back(reflection_probe_1);

	sReflectionProbe* reflection_probe_2 = new sReflectionProbe;

	reflection_probe_2->pos.set(180, 85, 5);
	reflection_probe_2->cubemap = new Texture();
	reflection_probe_2->cubemap->createCubemap(512, 512, NULL, GL_RGB, GL_UNSIGNED_INT, false);
	reflection_probes.push_back(reflection_probe_2);
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
			//if (shadow)
			//	renderPrefabShadowMap(node_model, node->mesh, node->material, camera);
			if (deferred)
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
	if(material->two_sided)
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
	glEnable(GL_DEPTH_TEST);

	shader->setUniform("u_color", material->color);
	if (texture)
		shader->setUniform("u_texture", texture, 0);
	if (emissive_texture)
		shader->setUniform("u_emissive_texture", emissive_texture, 1);
	shader->setUniform("u_emissive_factor", material->emissive_factor);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::AlphaMode::MASK ? material->alpha_cutoff : 0);

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform("u_factor", material->tilling_factor);

	if (Scene::getInstance()->lightEntities.empty())
	{
		glDisable(GL_BLEND);
		
		shader->setUniform("u_ambient_light", Scene::getInstance()->ambientLight);

		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);
	}
	else {

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
			}

			//upload uniforms
			shader->setUniform("u_ambient_light", i == 0 ? Scene::getInstance()->ambientLight : Vector3(0, 0, 0));

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

			//do the draw call that renders the mesh into the screen
			mesh->render(GL_TRIANGLES);
		}
	}
	//disable shader
	shader->disable();

	this->renderShadowMap();

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
	Shader* reflection_pass = NULL;
	Shader* volumetric_shader = NULL;

	//create fbo in case it hasn't been created before
	if (!this->fbo)
	{
		this->fbo = new FBO();
		this->fbo->create(width, height, 3, GL_RGB);
	}

	//first pass - Geometry
	renderSkybox(camera);
	this->fbo->bind();

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

		//glClearColor(0.0, 0.0, 0.0, 1.0);
		//glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

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


		second_pass->setUniform("u_user_irr", use_irradiance);
		if(use_irradiance)
		{
			//irradiance pass
			second_pass->setUniform("u_irr_texture", probes_texture, 5);
			second_pass->setUniform("u_irr_start", irr_start_pos);
			second_pass->setUniform("u_irr_end", irr_end_pos);
			second_pass->setUniform("u_irr_delta", irr_delta);
			second_pass->setUniform("u_irr_dims", irr_dim);
		}

		//lights pass
		bool firstLight = true;
		int visibleLights = numLightsVisible() - 1;
		int currentLight = -1;

		//multipass
		for (size_t i = 0; i < Scene::getInstance()->lightEntities.size(); i++)	//pass for all lights
		{
			glDisable(GL_DEPTH_TEST);
			Light* light = Scene::getInstance()->lightEntities.at(i);
			if (!light->visible)
			{
				continue;
			}
			else
			{
				currentLight++;
			}
				
			second_pass->setUniform("u_current_total", Vector2(currentLight, visibleLights));

			if (firstLight) {
				firstLight = false;
				glDisable(GL_BLEND);
				Vector3 ambient = Scene::getInstance()->ambientLight;
				second_pass->setUniform("u_ambient_light", Scene::getInstance()->ambient_light ? 
					Scene::getInstance()->ambientLight : Vector3(0,0,0));
			}
			else {
				glEnable(GL_BLEND);
				glBlendFunc(GL_ONE, GL_ONE);
				glBlendEquation(GL_FUNC_ADD);
				assert(glGetError() == GL_NO_ERROR);
				second_pass->setUniform("u_ambient_light", Vector3(0.0f, 0.0f, 0.0f));
			}

			if(currentLight == visibleLights)
			{
				second_pass->setUniform("u_environment_texture", environment);
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
					light->shadowMap : Texture::getWhiteTexture(), 6);
			}

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

		second_pass->disable();
	}

	//REFLECTION PASS
	if (use_reflection)
	{
		reflection_pass = Shader::Get("reflection");

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glEnable(GL_DEPTH_TEST);

		reflection_pass->enable();

		reflection_pass->setUniform("u_normal_texture", fbo->color_textures[1], 0);
		reflection_pass->setUniform("u_metal_roughness_texture", fbo->color_textures[2], 1);
		reflection_pass->setUniform("u_depth_texture", this->fbo->depth_texture, 2);
		reflection_pass->setUniform("u_environment_texture", environment, 3);
		reflection_pass->setUniform("u_reflection_texture_1", reflection_probes[0]->cubemap, 4);
		reflection_pass->setUniform("u_reflection_texture_2", reflection_probes[1]->cubemap, 5);
		reflection_pass->setUniform("u_probe_1_pos", reflection_probes[0]->pos);
		reflection_pass->setUniform("u_probe_2_pos", reflection_probes[1]->pos);
		reflection_pass->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
		reflection_pass->setUniform("u_camera_pos", camera->eye);
		reflection_pass->setUniform("u_inverse_viewprojection", inverse_matrix);

		quad->render(GL_TRIANGLES);

		reflection_pass->disable();
	}

	//VOLUMETRIC PASS

	if (use_volumetric && Scene::getInstance()->sun && Scene::getInstance()->sun->shadowMap)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);

		Light* sun = Scene::getInstance()->sun;
		Matrix44 inv_vp = camera->viewprojection_matrix;
		inv_vp.inverse();

		volumetric_shader = Shader::Get("volumetric");
		
		volumetric_shader->enable();

		volumetric_shader->setUniform("u_depth_texture", fbo->depth_texture, 0);
		volumetric_shader->setUniform("u_camera_position", camera->eye);
		volumetric_shader->setUniform("u_inverse_viewprojection", inv_vp);
		volumetric_shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		volumetric_shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
		volumetric_shader->setUniform("u_light_color", sun->color);
		volumetric_shader->setUniform("u_light_position", sun->model.getTranslation());
		volumetric_shader->setUniform("u_light_shadowmap", sun->shadowMap, 1);
		volumetric_shader->setUniform("u_is_cascade", sun->is_cascade);
		volumetric_shader->setUniform("u_light_bias", sun->bias);
		if (sun->light_type == lightType::SPOT || !sun->is_cascade)
			volumetric_shader->setUniform("u_shadow_viewprojection", sun->camera->viewprojection_matrix);
		else if (sun->light_type == lightType::DIRECTIONAL && sun->is_cascade)
			volumetric_shader->setMatrix44Array("u_shadow_viewprojection_array", sun->shadow_viewprojection, 4);

		quad->render(GL_TRIANGLES);

		volumetric_shader->disable();
		glDisable(GL_BLEND);
	}

	/********	DEBUG OPTIONS	********/
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	renderShadowMap();
	renderGBuffers(camera);

	if(show_probe_coefficients_texture && probes_texture != nullptr)
		probes_texture->toViewport();

	if (show_ao && blur_texture != nullptr)
		blur_texture->toViewport();

	if (show_irr_probes)
	{
		for each (sIrradianceProbe p in irradiance_probes)
			renderIrradianceProbes(p.pos, 5.0f, (float*)&p.sh);
	}
	if (show_reflection_probes)
	{
		glDisable(GL_DEPTH_TEST);
		renderReflectionProbe(reflection_probes[0], camera);
		renderReflectionProbe(reflection_probes[1], camera);
	}
}

void Renderer::computeIrradiance()
{
	irradiance_probes.clear();

	for(int z = 0; z < irr_dim.z; z++)
		for(int y = 0; y < irr_dim.y; y++)
			for (int x = 0; x < irr_dim.x; x++)
			{
				sIrradianceProbe p;
				p.local.set(x, y, z);

				p.index = floor(x + y * irr_dim.x + z * irr_dim.x * irr_dim.y);

				p.pos = irr_start_pos + irr_delta * Vector3(x, y, z);
				irradiance_probes.push_back(p);
			}

	if (!irr_fbo) {
		irr_fbo = new FBO();
		irr_fbo->create(64, 64, 1, GL_RGB, GL_FLOAT);
	}

	for (auto& p: irradiance_probes)
	{
		computeProbeCoeffs(p);
	}

	probes_texture = new Texture(9, irradiance_probes.size(), GL_RGB, GL_FLOAT);
	SphericalHarmonics* sh_data = nullptr;
	sh_data = new SphericalHarmonics[irr_dim.x * irr_dim.y * irr_dim.z];

	for (size_t i = 0; i < irradiance_probes.size(); i++)
	{
		sIrradianceProbe p = irradiance_probes.at(i);
		sh_data[p.index] = p.sh;
	}

	sIrrHeader irr_header;
	irr_header.start = irr_start_pos;
	irr_header.end = irr_end_pos;
	irr_header.dims = irr_dim;
	irr_header.delta = irr_delta;
	irr_header.num_probes = irr_dim.x * irr_dim.y * irr_dim.z;

	FILE* f = fopen("irradiance.bin", "wb");
	fwrite(&irr_header, sizeof(sIrrHeader), 1, f);
	fwrite(&sh_data[0], sizeof(SphericalHarmonics), irradiance_probes.size(), f);
	fclose(f);
}

void Renderer::computeProbeCoeffs(sIrradianceProbe& p)
{
	FloatImage images[6];

	Camera cam;
	cam.setPerspective(90, 1, 0.1f, 1000.0f);

	for (int i = 0; i < 6; i++)
	{
		Vector3 eye = p.pos;
		Vector3 front = cubemapFaceNormals[i][2];
		Vector3 center = p.pos + front;
		Vector3 up = cubemapFaceNormals[i][1];
		cam.lookAt(eye, center, up);
		cam.enable();

		irr_fbo->bind();

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		//Scene::getInstance()->render()
		Scene::getInstance()->renderForward(&cam, this);
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
			//glEnable(GL_DEPTH_TEST);
			if (light->light_type == lightType::SPOT)
			{
				Shader* shader = Shader::Get("depth");
				shader->enable();
				shader->setUniform("u_camera_nearfar",
					Vector2(light->camera->near_plane, light->camera->far_plane));
				light->shadowMap->toViewport(shader);
				shader->disable();
			}
			else
				light->shadowMap->toViewport();
			glEnable(GL_BLEND);
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

	//metal and roughness factors
	shader->setUniform("u_metallic_factor", material->metallic_factor);
	shader->setUniform("u_roughness_factor", material->roughness_factor);

	//object uniforms
	material->color = vec4(1.0, 1.0, 1.0, 1.0);
	shader->setUniform("u_color", material->color);

	shader->setUniform("u_color_texture", color_texture ? color_texture : Texture::getWhiteTexture(), 0);

	shader->setUniform("u_metal_roughness_texture", metal_roughness_texture ? metal_roughness_texture : Texture::getRedTexture(), 1);

	mesh->render(GL_TRIANGLES);

	shader->disable();

}

void GTR::Renderer::renderIrradianceProbes(Vector3 pos, float size, float* coeffs)
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

void Renderer::renderReflectionProbe(sReflectionProbe* p, Camera* camera)
{
	
	Shader* shader = Shader::Get("skybox");

	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	Matrix44 model;
	model.setTranslation(p->pos.x, p->pos.y, p->pos.z);
	//model.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	model.scale(10.0f, 10.0f, 10.0f);
	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform("u_texture", p->cubemap, 0);

	Mesh::Get("data/meshes/sphere.obj")->render(GL_TRIANGLES);

	shader->disable();

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	
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

void Renderer::renderSkybox(Camera* camera)
{
	if (!environment)
		return;

	Shader* shader = Shader::Get("skybox");

	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	Matrix44 model;
	model.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	model.scale(10.0, 10.0, 10.0);
	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform("u_texture", environment, 0);
	Mesh::Get("data/meshes/sphere.obj")->render(GL_TRIANGLES);
	shader->disable();
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
}

void Renderer::computeReflection()
{
	for(auto probe : reflection_probes)
	{
		computeProbeReflection(probe);
	}
}

void Renderer::computeProbeReflection(sReflectionProbe* p)
{
	Camera cam;
	cam.setPerspective(90, 1, 0.1f, 1000.0f);

	for(int i = 0; i < 6; ++i)
	{
		//assign cubemap face to FBO
		reflections_fbo->setTexture(p->cubemap, i);

		//bind FBO
		reflections_fbo->bind();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		
		//render view
		Vector3 eye = p->pos;
		Vector3 center = p->pos + cubemapFaceNormals[i][2];
		Vector3 up = cubemapFaceNormals[i][1];
		cam.lookAt(eye, center, up);
		cam.enable();
		Scene::getInstance()->render(&cam, this);
		reflections_fbo->unbind();
	}

	//generate the mipmaps
	p->cubemap->generateMipmaps();
}

int Renderer::numLightsVisible()
{
	int count = 0;

	for (auto light : Scene::getInstance()->lightEntities)
	{
		if (light->visible)
			count++;
	}

	return count;
}

Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = new HDRE();
	if (!hdre->load(filename))
	{
		delete hdre;
		return NULL;
	}

	Texture* texture = new Texture();
	texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFaces(0), hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
	for (int i = 1; i < 6; ++i)
		texture->uploadCubemap(texture->format, texture->type, false, (Uint8**)hdre->getFaces(i), GL_RGBA32F, i);
	return texture;
}

bool Renderer::loadIrradiance(const char* filename)
{
	FILE* f = fopen(filename, "rb");
	if (!f)
		return false;

	sIrrHeader header;
	fread(&header, sizeof(header), 1, f);
	irr_start_pos = header.start;
	irr_end_pos = header.end;
	irr_delta = header.delta;
	irr_dim = header.dims;
	irr_num_probes = header.num_probes;

	SphericalHarmonics* sh_data = new SphericalHarmonics[irr_dim.x * irr_dim.y * irr_dim.z];

	fread(&sh_data[0], sizeof(SphericalHarmonics), irr_dim.x * irr_dim.y * irr_dim.z, f);
	fclose(f);

	irradiance_probes.clear();

	for (int z = 0; z < irr_dim.z; z++)
		for (int y = 0; y < irr_dim.y; y++)
			for (int x = 0; x < irr_dim.x; x++)
			{
				sIrradianceProbe p;
				p.local.set(x, y, z);
				p.index = floor(x + y * irr_dim.x + z * irr_dim.x * irr_dim.y);
				p.pos = irr_start_pos + irr_delta * Vector3(x, y, z);
				int index = floor(x + y * irr_dim.x + z * irr_dim.x * irr_dim.y);
				p.sh = sh_data[index];
				irradiance_probes.push_back(p);
			}

	probes_texture->upload(GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

	probes_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	delete[] sh_data;
}