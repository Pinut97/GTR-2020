#pragma once
#include "prefab.h"
#include "fbo.h"
#include "sphericalharmonics.h"

//forward declarations
class Camera;

struct sIrradianceProbe {
	Vector3 pos;
	Vector3 local;
	int index;
	SphericalHarmonics sh;
};

struct sReflectionProbe {
	Vector3 pos;
	Texture* cubemap = NULL;
};

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

		bool use_ao;
		bool use_light;
		bool use_realtime_shadows;
		bool use_irradiance;
		bool use_reflection;

		bool show_GBuffers;
		bool show_ao;
		bool show_deferred;
		bool show_probes;
		bool show_irradiance;
		bool show_probe_coefficients_texture;

		FBO* fbo;
		FBO* ssao_fbo;
		FBO* irr_fbo;
		FBO* reflections_fbo;
		Texture* blur_texture;
		Texture* probes_texture;
		Texture* environment;
		FBO* aux_fbo;

		std::vector<Vector3> points;
		std::vector<sIrradianceProbe> irradiance_probes;
		std::vector<sReflectionProbe*> reflection_probes;

		Vector3 start_pos;
		Vector3 end_pos;
		Vector3 dim;
		Vector3 delta;

		const int probes_size = 10;

		Renderer();

		//add here your functions
		void renderDeferred(Camera* camera);
		void renderPrefabShadowMap(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
		void renderMeshInDeferred(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
		void computeIrradiance();
		void computeProbeCoeffs(sIrradianceProbe& p);
		void computeReflection();
		void computeProbeReflection(sReflectionProbe* p);
		int numLightsVisible();

		//debug functions
		void renderShadowMap();
		void renderGBuffers(Camera* camera);
		void renderProbes(Vector3 pos, float size, float* coeffs);
		void renderReflectionProbe(sReflectionProbe* p, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
		
		//to render skybox
		void renderSkybox(Camera* camera);
	};

	std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);
	Texture* CubemapFromHDRE(const char* filename);
};