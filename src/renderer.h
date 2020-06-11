#pragma once
#include "prefab.h"
#include "fbo.h"

//forward declarations
class Camera;

namespace GTR {

	class Prefab;
	class Material;
	
	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:
		bool shadow;
		bool deferred;
		bool show_GBuffers;
		bool show_ao;
		bool show_deferred;
		bool use_ao;
		bool use_light;

		FBO* fbo;
		FBO* ssao_fbo;
		Texture* blur_texture;

		std::vector<Vector3> points;

		Renderer();

		//add here your functions
		void renderDeferred(Camera* camera);

		void renderPrefabShadowMap(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void renderMeshInDeferred(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void renderLights(Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
	};

	std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);

};