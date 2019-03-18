#include <KlayGE/KlayGE.hpp>
#include <KFL/CXX17/iterator.hpp>
#include <KFL/Util.hpp>
#include <KFL/Math.hpp>
#include <KlayGE/Font.hpp>
#include <KlayGE/FrameBuffer.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KlayGE/SceneManager.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/ResLoader.hpp>
#include <KlayGE/RenderSettings.hpp>
#include <KlayGE/Mesh.hpp>
#include <KlayGE/SceneNodeHelper.hpp>
#include <KlayGE/PostProcess.hpp>
#include <KlayGE/InfTerrain.hpp>
#include <KlayGE/LensFlare.hpp>
#include <KlayGE/Camera.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/InputFactory.hpp>
#include <KlayGE/DeferredRenderingLayer.hpp>
#include <KlayGE/LightShaft.hpp>
#include <KlayGE/SkyBox.hpp>
#include <KFL/Half.hpp>

#include <sstream>

#include "SampleCommon.hpp"
#include "FoliageTerrain.hpp"
#include "Foliage.hpp"

using namespace std;
using namespace KlayGE;

namespace
{
	class RenderableFoggySkyBox : public RenderableSkyBox
	{
	public:
		BOOST_TYPE_INDEX_REGISTER_RUNTIME_CLASS((RenderableSkyBox))

		RenderableFoggySkyBox()
		{
			RenderEffectPtr effect = SyncLoadRenderEffect("FoggySkyBox.fxml");

			gbuffer_mrt_tech_ = effect->TechniqueByName("GBufferSkyBoxMRTTech");
			special_shading_tech_ = effect->TechniqueByName("SpecialShadingFoggySkyBox");
			this->Technique(effect, gbuffer_mrt_tech_);
		}
		
		void FogColor(Color const & clr)
		{
			*(effect_->ParameterByName("fog_color")) = float3(clr.r(), clr.g(), clr.b());
		}
	};


	enum
	{
		Exit,
	};

	InputActionDefine actions[] =
	{
		InputActionDefine(Exit, KS_Escape),
	};
}

int SampleMain()
{
	ContextCfg cfg = Context::Instance().Config();
	cfg.deferred_rendering = true;
	Context::Instance().Config(cfg);

	FoliageApp app;
	app.Create();
	app.Run();

	return 0;
}

FoliageApp::FoliageApp()
			: App3DFramework("Foliage"),
				light_shaft_on_(true)
{
	ResLoader::Instance().AddPath("../../Samples/media/Foliage");
}

void FoliageApp::OnCreate()
{
	this->LookAt(float3(-3480.42f, -172.59f, 8235.04f), float3(-3480.58f, -172.28f, 8235.98f));
	this->Proj(0.1f, 5000);

	TexturePtr c_cube = ASyncLoadTexture("DH001cross_filtered_c.dds", EAH_GPU_Read | EAH_Immutable);
	TexturePtr y_cube = ASyncLoadTexture("DH001cross_filtered_y.dds", EAH_GPU_Read | EAH_Immutable);

	font_ = SyncLoadFont("gkai00mp.kfont");

	deferred_rendering_ = Context::Instance().DeferredRenderingLayerInstance();
	deferred_rendering_->SSVOEnabled(0, false);

	auto& root_node = Context::Instance().SceneManagerInstance().SceneRootNode();

	auto ambient_light = MakeSharedPtr<AmbientLightSource>();
	ambient_light->SkylightTex(y_cube, c_cube);
	ambient_light->Color(float3(0.1f, 0.1f, 0.1f));
	root_node.AddComponent(ambient_light);

	sun_light_node_ = MakeSharedPtr<SceneNode>(SceneNode::SOA_Cullable);
	auto sun_light = MakeSharedPtr<DirectionalLightSource>();
	sun_light->Attrib(LightSource::LSA_NoShadow);
	sun_light->Color(float3(3, 3, 3));
	sun_light_node_->TransformToParent(
		MathLib::to_matrix(MathLib::axis_to_axis(float3(0, 0, 1), float3(0.267835f, -0.0517653f, -0.960315f))));
	sun_light_node_->AddComponent(sun_light);
	root_node.AddChild(sun_light_node_);
	
	Color fog_color(0.61f, 0.52f, 0.62f, 1);
	if (Context::Instance().Config().graphics_cfg.gamma)
	{
		fog_color.r() = MathLib::srgb_to_linear(fog_color.r());
		fog_color.g() = MathLib::srgb_to_linear(fog_color.g());
		fog_color.b() = MathLib::srgb_to_linear(fog_color.b());
	}

	auto terrain_renderable = MakeSharedPtr<ProceduralTerrain>();
	terrain_ = MakeSharedPtr<HQTerrainSceneObject>(terrain_renderable);
	terrain_renderable->TextureLayer(0, ASyncLoadTexture("RealSand40BoH.dds", EAH_GPU_Read | EAH_Immutable));
	terrain_renderable->TextureLayer(1, ASyncLoadTexture("snow_DM.dds", EAH_GPU_Read | EAH_Immutable));
	terrain_renderable->TextureLayer(2, ASyncLoadTexture("GrassGreenTexture0002.dds", EAH_GPU_Read | EAH_Immutable));
	terrain_renderable->TextureLayer(3, ASyncLoadTexture("Ground.dds", EAH_GPU_Read | EAH_Immutable));
	terrain_renderable->TextureScale(0, float2(7, 7));
	terrain_renderable->TextureScale(1, float2(1, 1));
	terrain_renderable->TextureScale(2, float2(3, 3));
	terrain_renderable->TextureScale(3, float2(11, 11));
	root_node.AddChild(terrain_);

	auto sky_box = MakeSharedPtr<RenderableFoggySkyBox>();
	sky_box->CompressedCubeMap(y_cube, c_cube);
	sky_box->FogColor(fog_color);
	root_node.AddChild(MakeSharedPtr<SceneNode>(sky_box, SceneNode::SOA_NotCastShadow));

	auto sun_flare = MakeSharedPtr<LensFlareSceneObject>();
	sun_flare->Direction(MathLib::transform_normal(float3(0, 0, -1), sun_light_node_->TransformToParent()));
	root_node.AddChild(sun_flare);

	fog_pp_ = SyncLoadPostProcess("Fog.ppml", "fog");
	fog_pp_->SetParam(1, float3(fog_color.r(), fog_color.g(), fog_color.b()));
	fog_pp_->SetParam(2, 1.0f / 5000);
	fog_pp_->SetParam(3, this->ActiveCamera().FarPlane());
	deferred_rendering_->AtmosphericPostProcess(fog_pp_);

	light_shaft_pp_ = MakeSharedPtr<LightShaftPostProcess>();
	light_shaft_pp_->SetParam(1, sun_light->Color());

	fpcController_.Scalers(0.05f, 1.0f);

	InputEngine& inputEngine(Context::Instance().InputFactoryInstance().InputEngineInstance());
	InputActionMap actionMap;
	actionMap.AddActions(actions, actions + std::size(actions));

	action_handler_t input_handler = MakeSharedPtr<input_signal>();
	input_handler->Connect(
		[this](InputEngine const & sender, InputAction const & action)
		{
			this->InputHandler(sender, action);
		});
	inputEngine.ActionMap(actionMap, input_handler);

	UIManager::Instance().Load(ResLoader::Instance().Open("Foliage.uiml"));
	dialog_params_ = UIManager::Instance().GetDialog("Parameters");
	id_light_shaft_ = dialog_params_->IDFromName("LightShaft");
	id_fps_camera_ = dialog_params_->IDFromName("FPSCamera");

	dialog_params_->Control<UICheckBox>(id_light_shaft_)->OnChangedEvent().Connect(
		[this](UICheckBox const & sender)
		{
			this->LightShaftHandler(sender);
		});

	dialog_params_->Control<UICheckBox>(id_fps_camera_)->OnChangedEvent().Connect(
		[this](UICheckBox const & sender)
		{
			this->FPSCameraHandler(sender);
		});
}

void FoliageApp::OnResize(uint32_t width, uint32_t height)
{
	App3DFramework::OnResize(width, height);

	RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
	deferred_rendering_->SetupViewport(0, re.CurFrameBuffer(), 0);

	UIManager::Instance().SettleCtrls();
}

void FoliageApp::InputHandler(InputEngine const & /*sender*/, InputAction const & action)
{
	switch (action.first)
	{
	case Exit:
		this->Quit();
		break;
	}
}

void FoliageApp::LightShaftHandler(UICheckBox const & sender)
{
	light_shaft_on_ = sender.GetChecked();
}

void FoliageApp::FPSCameraHandler(UICheckBox const & sender)
{
	if (sender.GetChecked())
	{
		fpcController_.AttachCamera(this->ActiveCamera());
	}
	else
	{
		fpcController_.DetachCamera();
	}
}

void FoliageApp::DoUpdateOverlay()
{
	UIManager::Instance().Render();

	std::wostringstream stream;
	stream.precision(2);
	stream << std::fixed << this->FPS() << " FPS";

	font_->RenderText(0, 0, Color(1, 1, 0, 1), L"Foliage", 16);
	font_->RenderText(0, 18, Color(1, 1, 0, 1), stream.str(), 16);

	stream.str(L"");
	stream << deferred_rendering_->NumObjectsRendered() << " Scene objects "
		<< deferred_rendering_->NumRenderablesRendered() << " Renderables "
		<< deferred_rendering_->NumPrimitivesRendered() << " Primitives "
		<< deferred_rendering_->NumVerticesRendered() << " Vertices";
	font_->RenderText(0, 36, Color(1, 1, 1, 1), stream.str(), 16);

	if (!terrain_->FirstComponentOfType<ProceduralTerrain>()->UseDrawIndirect())
	{
		stream.str(L"");
		stream << terrain_->FirstComponentOfType<ProceduralTerrain>()->Num3DPlants() << " 3D plants "
			<< terrain_->FirstComponentOfType<ProceduralTerrain>()->NumImpostorPlants() << " impostor plants";
		font_->RenderText(0, 54, Color(1, 1, 1, 1), stream.str(), 16);
	}
}

uint32_t FoliageApp::DoUpdate(uint32_t pass)
{
	uint32_t ret = deferred_rendering_->Update(pass);
	if (ret & App3DFramework::URV_Finished)
	{
		if (light_shaft_on_)
		{
			light_shaft_pp_->SetParam(0, -MathLib::transform_normal(float3(0, 0, 1), sun_light_node_->TransformToParent()) * 10000.0f +
											this->ActiveCamera().EyePos());
			light_shaft_pp_->InputPin(0, deferred_rendering_->PrevFrameResolvedShadingTex(0));
			light_shaft_pp_->InputPin(1, deferred_rendering_->PrevFrameResolvedDepthTex(0));
			light_shaft_pp_->Apply();
		}
	}

	return ret;
}
