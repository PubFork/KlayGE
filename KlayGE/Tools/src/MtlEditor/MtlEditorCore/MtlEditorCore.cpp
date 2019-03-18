#include <KlayGE/KlayGE.hpp>
#include <KFL/CXX17/iterator.hpp>
#include <KFL/ErrorHandling.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/ResLoader.hpp>
#include <KlayGE/FrameBuffer.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KlayGE/RenderMaterial.hpp>
#include <KlayGE/RenderableHelper.hpp>
#include <KlayGE/Camera.hpp>
#include <KlayGE/SkyBox.hpp>
#include <KlayGE/SceneNodeHelper.hpp>
#include <KlayGE/DeferredRenderingLayer.hpp>
#include <KlayGE/UI.hpp>
#include <KlayGE/Mesh.hpp>
#include <KlayGE/Imposter.hpp>
#include <KlayGE/Window.hpp>
#include <KlayGE/SceneManager.hpp>
#include <KlayGE/Input.hpp>
#include <KlayGE/InputFactory.hpp>
#include <KFL/CXX17/filesystem.hpp>

#include <sstream>
#include <fstream>

#include "MtlEditorCore.hpp"

using namespace std;
using namespace KlayGE;

namespace
{
	class RenderAxis : public Renderable
	{
	public:
		BOOST_TYPE_INDEX_REGISTER_RUNTIME_CLASS((Renderable))

		RenderAxis()
			: Renderable(L"Axis")
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			effect_ = SyncLoadRenderEffect("MVUtil.fxml");
			simple_forward_tech_ = effect_->TechniqueByName("AxisTech");
			mvp_param_ = effect_->ParameterByName("mvp");

			float4 xyzs[] =
			{
				float4(0, 0, 0, 0),
				float4(1, 0, 0, 0),
				float4(0, 0, 0, 1),
				float4(0, 1, 0, 1),
				float4(0, 0, 0, 2),
				float4(0, 0, 1, 2),
			};

			rls_[0] = rf.MakeRenderLayout();
			rls_[0]->TopologyType(RenderLayout::TT_LineList);

			GraphicsBufferPtr pos_vb = rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, sizeof(xyzs), xyzs);

			rls_[0]->BindVertexStream(pos_vb, VertexElement(VEU_Position, 0, EF_ABGR32F));

			pos_aabb_ = MathLib::compute_aabbox(&xyzs[0], &xyzs[0] + std::size(xyzs));
			tc_aabb_ = AABBox(float3(0, 0, 0), float3(0, 0, 0));

			effect_attrs_ |= EA_SimpleForward;
		}

		void OnRenderBegin()
		{
			Camera const & camera = Context::Instance().AppInstance().ActiveCamera();
			*mvp_param_ = model_mat_ * camera.ViewProjMatrix();
		}
	};

	class RenderGrid : public Renderable
	{
	public:
		BOOST_TYPE_INDEX_REGISTER_RUNTIME_CLASS((Renderable))

		RenderGrid()
			: Renderable(L"Grid")
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			effect_ = SyncLoadRenderEffect("MVUtil.fxml");
			simple_forward_tech_ = effect_->TechniqueByName("GridTech");
			mvp_param_ = effect_->ParameterByName("mvp");

			float3 xyzs[(21 + 21) * 2];
			for (int i = 0; i < 21; ++ i)
			{
				xyzs[i * 2 + 0] = float3(-10.0f + i, 0, -10);
				xyzs[i * 2 + 1] = float3(-10.0f + i, 0, +10);

				xyzs[(i + 21) * 2 + 0] = float3(-10, 0, -10.0f + i);
				xyzs[(i + 21) * 2 + 1] = float3(+10, 0, -10.0f + i);
			}

			rls_[0] = rf.MakeRenderLayout();
			rls_[0]->TopologyType(RenderLayout::TT_LineList);

			GraphicsBufferPtr pos_vb = rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, sizeof(xyzs), xyzs);

			rls_[0]->BindVertexStream(pos_vb, VertexElement(VEU_Position, 0, EF_BGR32F));

			pos_aabb_ = MathLib::compute_aabbox(&xyzs[0], &xyzs[0] + std::size(xyzs));
			tc_aabb_ = AABBox(float3(0, 0, 0), float3(0, 0, 0));

			effect_attrs_ |= EA_SimpleForward;
		}

		void OnRenderBegin()
		{
			Camera const & camera = Context::Instance().AppInstance().ActiveCamera();
			*mvp_param_ = model_mat_ * camera.ViewProjMatrix();
		}
	};

	class RenderImpostor : public Renderable
	{
	public:
		BOOST_TYPE_INDEX_REGISTER_RUNTIME_CLASS((Renderable))

		RenderImpostor(std::string const & name, AABBox const & aabbox)
			: Renderable(L"RenderImpostor")
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			rls_[0] = rf.MakeRenderLayout();
			rls_[0]->TopologyType(RenderLayout::TT_TriangleStrip);

			float2 pos[] =
			{
				float2(-1, +1),
				float2(+1, +1),
				float2(-1, -1),
				float2(+1, -1)
			};
			GraphicsBufferPtr vb = rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, sizeof(pos), pos);
			rls_[0]->BindVertexStream(vb, VertexElement(VEU_Position, 0, EF_GR32F));

			this->BindDeferredEffect(SyncLoadRenderEffect("Imposter.fxml"));
			gbuffer_mrt_tech_ = deferred_effect_->TechniqueByName("ImpostorGBufferAlphaTestMRT");
			technique_ = gbuffer_mrt_tech_;

			pos_aabb_ = aabbox;

			imposter_ = SyncLoadImposter(name);
			this->ImpostorTexture(imposter_->RT0Texture(), imposter_->RT1Texture(), imposter_->ImposterSize() * 0.5f);
		}

		void ImpostorTexture(TexturePtr const & rt0_tex, TexturePtr const & rt1_tex, float2 const & extent)
		{
			auto& rf = Context::Instance().RenderFactoryInstance();
			textures_[RenderMaterial::TS_Normal] = rf.MakeTextureSrv(rt0_tex);
			textures_[RenderMaterial::TS_Albedo] = rf.MakeTextureSrv(rt1_tex);

			tc_aabb_.Min() = float3(-extent.x(), -extent.y(), 0);
			tc_aabb_.Max() = float3(+extent.x(), +extent.y(), 0);
		}

		void OnRenderBegin() override
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();
			RenderEngine& re = rf.RenderEngineInstance();
			Camera const * camera = re.CurFrameBuffer()->GetViewport()->camera.get();

			float4x4 billboard_mat = camera->InverseViewMatrix();
			billboard_mat(3, 0) = 0;
			billboard_mat(3, 1) = 0;
			billboard_mat(3, 2) = 0;
			*(deferred_effect_->ParameterByName("billboard_mat")) = billboard_mat;

			float2 start_tc = imposter_->StartTexCoord(camera->EyePos() - pos_aabb_.Center());
			*(deferred_effect_->ParameterByName("start_tc")) = start_tc;

			Renderable::OnRenderBegin();
		}

	private:
		ImposterPtr imposter_;
	};
}

namespace KlayGE
{
	MtlEditorCore::MtlEditorCore(void* native_wnd)
				: App3DFramework("MtlEditor", native_wnd),
					fps_controller_(false), tb_controller_(false), is_fps_camera_(false),
					skinning_(true), curr_frame_(0), imposter_mode_(false), mouse_down_in_wnd_(false), mouse_tracking_mode_(false),
					update_selective_buffer_(false), selected_obj_(0)
	{
		ResLoader::Instance().AddPath("../../Tools/media/MtlEditor");
	}

	void MtlEditorCore::Resize(uint32_t width, uint32_t height)
	{
		Context::Instance().RenderFactoryInstance().RenderEngineInstance().Resize(width, height);

		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		RenderEngine& re = rf.RenderEngineInstance();
		deferred_rendering_->SetupViewport(0, re.CurFrameBuffer(), 0);

		auto const fmt = re.DeviceCaps().BestMatchTextureRenderTargetFormat({ EF_ABGR8, EF_ARGB8 }, 1, 0);
		BOOST_ASSERT(fmt != EF_Unknown);

		selective_tex_ = rf.MakeTexture2D(width, height, 1, 1, fmt, 1, 0, EAH_GPU_Write);
		selective_cpu_tex_ = rf.MakeTexture2D(width, height, 1, 1, fmt, 1, 0, EAH_CPU_Read);
		selective_fb_->Attach(FrameBuffer::Attachment::Color0, rf.Make2DRtv(selective_tex_, 0, 1, 0));
		selective_fb_->Attach(rf.Make2DDsv(width, height, EF_D24S8, 1, 0));

		update_selective_buffer_ = true;
	}

	void MtlEditorCore::OnCreate()
	{
		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		RenderEngine& re = rf.RenderEngineInstance();

		font_ = SyncLoadFont("gkai00mp.kfont");

		deferred_rendering_ = Context::Instance().DeferredRenderingLayerInstance();
		deferred_rendering_->SSVOEnabled(0, false);

		SceneNode& root_node = Context::Instance().SceneManagerInstance().SceneRootNode();

		ambient_light_ = MakeSharedPtr<AmbientLightSource>();
		ambient_light_->Color(float3(0.1f, 0.1f, 0.1f));
		root_node.AddComponent(ambient_light_);

		main_light_ = MakeSharedPtr<DirectionalLightSource>();
		main_light_->Attrib(LightSource::LSA_NoShadow);
		main_light_->Color(float3(1.0f, 1.0f, 1.0f));

		main_light_node_ = MakeSharedPtr<SceneNode>(SceneNode::SOA_Cullable | SceneNode::SOA_Moveable);
		main_light_node_->AddComponent(main_light_);
		auto* main_light_node_ptr = main_light_node_.get();
		main_light_node_->OnMainThreadUpdate().Connect([this, main_light_node_ptr](float app_time, float elapsed_time) {
			KFL_UNUSED(app_time);
			KFL_UNUSED(elapsed_time);
			main_light_node_ptr->TransformToParent(Context::Instance().AppInstance().ActiveCamera().InverseViewMatrix());
		});
		root_node.AddChild(main_light_node_);

		axis_ = MakeSharedPtr<SceneNode>(MakeSharedPtr<RenderAxis>(),
			SceneNode::SOA_Cullable | SceneNode::SOA_Moveable | SceneNode::SOA_NotCastShadow);
		root_node.AddChild(axis_);

		grid_ = MakeSharedPtr<SceneNode>(MakeSharedPtr<RenderGrid>(),
			SceneNode::SOA_Cullable | SceneNode::SOA_Moveable | SceneNode::SOA_NotCastShadow);
		root_node.AddChild(grid_);

		Color clear_clr(0.2f, 0.4f, 0.6f, 1);
		if (Context::Instance().Config().graphics_cfg.gamma)
		{
			clear_clr.r() = 0.029f;
			clear_clr.g() = 0.133f;
			clear_clr.b() = 0.325f;
		}
		;
		auto const fmt = re.DeviceCaps().BestMatchTextureFormat({ EF_ABGR8, EF_ARGB8 });
		BOOST_ASSERT(fmt != EF_Unknown);
		uint32_t texel = ((fmt == EF_ABGR8) ? clear_clr.ABGR() : clear_clr.ARGB());
		ElementInitData init_data[6];
		for (int i = 0; i < 6; ++ i)
		{
			init_data[i].data = &texel;
			init_data[i].row_pitch = sizeof(uint32_t);
			init_data[i].slice_pitch = init_data[i].row_pitch;
		}
		default_cube_map_ = rf.MakeTextureCube(1, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_Immutable, init_data);

		sky_box_ = MakeSharedPtr<SceneNode>(MakeSharedPtr<RenderableSkyBox>(), SceneNode::SOA_NotCastShadow);
		sky_box_->FirstComponentOfType<RenderableSkyBox>()->CubeMap(default_cube_map_);
		root_node.AddChild(sky_box_);

		ambient_light_->SkylightTex(default_cube_map_);

		selected_bb_ = MakeSharedPtr<SceneNode>(MakeSharedPtr<RenderableLineBox>(),
			SceneNode::SOA_Moveable | SceneNode::SOA_NotCastShadow);
		selected_bb_->Visible(false);
		root_node.AddChild(selected_bb_);
		selected_bb_->FirstComponentOfType<RenderableLineBox>()->SetColor(Color(1, 1, 1, 1));

		this->LookAt(float3(-5, 5, -5), float3(0, 1, 0), float3(0.0f, 1.0f, 0.0f));
		this->Proj(0.1f, 100);

		tb_controller_.AttachCamera(this->ActiveCamera());

		selective_fb_ = rf.MakeFrameBuffer();
		selective_fb_->GetViewport()->camera = re.CurFrameBuffer()->GetViewport()->camera;
	}

	void MtlEditorCore::OnDestroy()
	{
		selective_fb_.reset();
		selective_tex_.reset();
		selective_cpu_tex_.reset();

		tb_controller_.DetachCamera();
		fps_controller_.DetachCamera();

		model_.reset();
		sky_box_.reset();
		grid_.reset();
		axis_.reset();
		main_light_.reset();
		ambient_light_.reset();
		selected_bb_.reset();

		font_.reset();
	}

	void MtlEditorCore::OnResize(uint32_t width, uint32_t height)
	{
		App3DFramework::OnResize(width, height);

		RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
		deferred_rendering_->SetupViewport(0, re.CurFrameBuffer(), 0);
	}

	bool MtlEditorCore::OpenModel(std::string const & name)
	{
		if (!last_file_path_.empty())
		{
			ResLoader::Instance().DelPath(last_file_path_);
		}

		std::filesystem::path mesh_path = name;
		last_file_path_ = mesh_path.parent_path().string();
		ResLoader::Instance().AddPath(last_file_path_);

		std::filesystem::path imposter_path = mesh_path;
		imposter_path.replace_extension(".impml");
		std::string imposter_name = imposter_path.string();

		SceneNode& root_node = Context::Instance().SceneManagerInstance().SceneRootNode();

		if (object_)
		{
			root_node.RemoveChild(object_);
			object_.reset();

			ResLoader::Instance().Unload(model_);
			model_.reset();
		}
		if (skeleton_object_)
		{
			root_node.RemoveChild(skeleton_object_);
			skeleton_object_.reset();

			skeleton_model_.reset();
		}
		if (imposter_)
		{
			root_node.RemoveChild(imposter_);
			imposter_.reset();
		}

		std::string mesh_name = mesh_path.string();
		model_ = SyncLoadModel(mesh_name, EAH_GPU_Read | EAH_Immutable,
			SceneNode::SOA_Cullable, AddToSceneRootHelper,
			CreateModelFactory<DetailedSkinnedModel>, CreateMeshFactory<DetailedSkinnedMesh>);
		checked_pointer_cast<DetailedSkinnedModel>(model_)->SetTime(0);
		object_ = model_->RootNode();
		for (size_t i = 0; i < model_->NumMeshes(); ++ i)
		{
			model_->Mesh(i)->ObjectID(static_cast<uint32_t>(i + 1));
		}

		if (checked_pointer_cast<DetailedSkinnedModel>(model_)->NumJoints() > 0)
		{
			skeleton_model_ = MakeSharedPtr<SkeletonMesh>(*model_);
			skeleton_object_ = MakeSharedPtr<SceneNode>(skeleton_model_, 0);
			root_node.AddChild(skeleton_object_);
		}

		if (!ResLoader::Instance().Locate(imposter_name).empty())
		{
			imposter_ = MakeSharedPtr<SceneNode>(
				MakeSharedPtr<RenderImpostor>(imposter_name, model_->RootNode()->PosBoundOS()), 0);
			root_node.AddChild(imposter_);
			imposter_->Visible(false);
		}

		imposter_mode_ = false;

		AABBox const & bb = model_->RootNode()->PosBoundOS();
		float3 center = bb.Center();
		float3 half_size = bb.HalfSize();
		this->LookAt(center + float3(half_size.x() * 2, half_size.y() * 2.5f, half_size.z() * 3), float3(0, center.y(), 0), float3(0.0f, 1.0f, 0.0f));
		float far_plane = std::max(200.0f, MathLib::length(half_size) * 5);
		this->Proj(0.1f, far_plane);
		deferred_rendering_->LightDistance(far_plane);

		if (is_fps_camera_)
		{
			fps_controller_.AttachCamera(this->ActiveCamera());
		}
		else
		{
			tb_controller_.AttachCamera(this->ActiveCamera());
		}
		tb_controller_.Scalers(0.01f, MathLib::length(half_size) * 0.001f);
		fps_controller_.Scalers(0.01f, 0.2f);

		update_selective_buffer_ = true;
		selected_obj_ = 0;
		this->UpdateSelectedMesh();

		return true;
	}

	void MtlEditorCore::SaveAsModel(std::string const & name)
	{
		SaveModel(*model_, name);
	}

	void MtlEditorCore::DoUpdateOverlay()
	{
		RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();

		std::wostringstream stream;
		stream.precision(2);
		stream << std::fixed << this->FPS() << " FPS";

		font_->RenderText(0, 0, Color(1, 1, 0, 1), re.ScreenFrameBuffer()->Description(), 16);
		font_->RenderText(0, 18, Color(1, 1, 0, 1), stream.str(), 16);
	}

	uint32_t MtlEditorCore::DoUpdate(uint32_t pass)
	{
		if (0 == pass)
		{
			Camera const & camera = this->ActiveCamera();

			if (model_)
			{
				AABBox bb = MathLib::transform_aabb(model_->RootNode()->PosBoundWS(), camera.ViewMatrix())
					| MathLib::transform_aabb(grid_->FirstComponentOfType<Renderable>()->PosBound(), camera.ViewMatrix());
				float near_plane = std::max(0.01f, bb.Min().z() * 0.8f);
				float far_plane = std::max(near_plane + 0.1f, bb.Max().z() * 1.2f);
				this->Proj(near_plane, far_plane);
			}

			RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
			uint32_t width = re.CurFrameBuffer()->Width();
			uint32_t height = re.CurFrameBuffer()->Height();
			float x = (width - 70.5f) / width * 2 - 1;
			float y = 1 - (height - 70.5f) / height * 2;
			float3 origin = MathLib::transform_coord(float3(x, y, 0.1f), camera.InverseViewProjMatrix());

			float x1 = (width - 20.5f) / width * 2 - 1;
			float3 x_dir = MathLib::transform_coord(float3(x1, y, 0.1f), camera.InverseViewProjMatrix());
			float len = MathLib::length(x_dir - origin);

			float4x4 scaling = MathLib::scaling(len, len, len);
			float4x4 trans = MathLib::translation(origin);
			axis_->TransformToParent(scaling * trans);
		}

		uint32_t deferrd_pass_start;
		if (model_ && update_selective_buffer_)
		{
			deferrd_pass_start = 2;

			RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
			if (0 == pass)
			{
				axis_->Visible(false);
				grid_->Visible(false);
				sky_box_->Visible(false);
				selected_bb_->Visible(false);
				if (imposter_)
				{
					imposter_->Visible(false);
				}
				for (uint32_t i = 0; i < model_->NumMeshes(); ++ i)
				{
					model_->Mesh(i)->SelectMode(true);
				}

				re.BindFrameBuffer(selective_fb_);
				re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth | FrameBuffer::CBM_Stencil,
					Color(0.0f, 0.0f, 0.0f, 1.0f), 1, 0);
				return App3DFramework::URV_NeedFlush;
			}
			else if (1 == pass)
			{
				axis_->Visible(true);
				grid_->Visible(true);
				sky_box_->Visible(true);
				selected_bb_->Visible(selected_obj_ > 0);
				if (imposter_)
				{
					imposter_->Visible(imposter_mode_);
				}
				for (uint32_t i = 0; i < model_->NumMeshes(); ++ i)
				{
					model_->Mesh(i)->SelectMode(false);
				}

				re.BindFrameBuffer(FrameBufferPtr());
				return 0;
			}
		}
		else
		{
			deferrd_pass_start = 0;
		}

		if (pass >= deferrd_pass_start)
		{
			uint32_t deferred_pass = pass - deferrd_pass_start;
			uint32_t urv = deferred_rendering_->Update(deferred_pass);
			if (urv & App3DFramework::URV_Finished)
			{
				selective_tex_->CopyToTexture(*selective_cpu_tex_);
				update_selective_buffer_ = false;
			}

			return urv;
		}
		else
		{
			KFL_UNREACHABLE("Can't be here");
		}
	}

	uint32_t MtlEditorCore::NumFrames() const
	{
		if (model_)
		{
			return checked_pointer_cast<DetailedSkinnedModel>(model_)->NumFrames();
		}
		else
		{
			return 0;
		}
	}

	char const * MtlEditorCore::SkyboxName() const
	{
		return skybox_name_.c_str();
	}

	void MtlEditorCore::SkyboxName(std::string const & name)
	{
		skybox_name_ = name;

		if (!skybox_name_.empty())
		{
			TexturePtr y_tex = SyncLoadTexture(name, EAH_GPU_Read | EAH_Immutable);
			TexturePtr c_tex;

			std::string::size_type pos = name.find_last_of('.');
			if ((pos > 0) && ('_' == name[pos - 2]))
			{
				if ('y' == name[pos - 1])
				{
					std::string c_name = name;
					c_name[pos - 1] = 'c';
					c_tex = SyncLoadTexture(c_name, EAH_GPU_Read | EAH_Immutable);
				}
				else if ('c' == name[pos - 1])
				{
					c_tex = y_tex;

					std::string y_name = name;
					y_name[pos - 1] = 'y';
					y_tex = SyncLoadTexture(y_name, EAH_GPU_Read | EAH_Immutable);
				}
			}

			if (!!c_tex)
			{
				sky_box_->FirstComponentOfType<RenderableSkyBox>()->CompressedCubeMap(y_tex, c_tex);
				ambient_light_->SkylightTex(y_tex, c_tex);
			}
			else
			{
				sky_box_->FirstComponentOfType<RenderableSkyBox>()->CubeMap(y_tex);
				ambient_light_->SkylightTex(y_tex);
			}
		}
		else
		{
			sky_box_->FirstComponentOfType<RenderableSkyBox>()->CubeMap(default_cube_map_);
			ambient_light_->SkylightTex(default_cube_map_);
		}
	}

	void MtlEditorCore::DisplaySSVO(bool ssvo)
	{
		deferred_rendering_->SSVOEnabled(0, ssvo);
	}

	void MtlEditorCore::DisplayHDR(bool hdr)
	{
		Context::Instance().RenderFactoryInstance().RenderEngineInstance().HDREnabled(hdr);
	}

	void MtlEditorCore::DisplayAA(bool aa)
	{
		Context::Instance().RenderFactoryInstance().RenderEngineInstance().PPAAEnabled(aa);
	}

	void MtlEditorCore::DisplayGamma(bool gamma)
	{
		Context::Instance().RenderFactoryInstance().RenderEngineInstance().GammaEnabled(gamma);
	}

	void MtlEditorCore::DisplayColorGrading(bool cg)
	{
		Context::Instance().RenderFactoryInstance().RenderEngineInstance().ColorGradingEnabled(cg);
	}

	float MtlEditorCore::CurrFrame() const
	{
		return curr_frame_;
	}

	void MtlEditorCore::CurrFrame(float frame)
	{
		if (skinning_)
		{
			checked_pointer_cast<DetailedSkinnedModel>(model_)->SetFrame(frame);
			curr_frame_ = frame;

			this->UpdateSelectedMesh();
		}
	}

	float MtlEditorCore::ModelFrameRate() const
	{
		return static_cast<float>(checked_pointer_cast<DetailedSkinnedModel>(model_)->FrameRate());
	}

	uint32_t MtlEditorCore::NumLods() const
	{
		return checked_pointer_cast<DetailedSkinnedModel>(model_)->NumLods();
	}

	void MtlEditorCore::ActiveLod(int32_t lod)
	{
		checked_pointer_cast<DetailedSkinnedModel>(model_)->ActiveLod(lod);
	}

	uint32_t MtlEditorCore::NumMeshes() const
	{
		return model_->NumMeshes();
	}

	wchar_t const * MtlEditorCore::MeshName(uint32_t index) const
	{
		return model_->Mesh(index)->Name().c_str();
	}

	uint32_t MtlEditorCore::NumVertexStreams(uint32_t mesh_id) const
	{
		Renderable const & mesh = *model_->Mesh(mesh_id);
		RenderLayout const & rl = mesh.GetRenderLayout();
		return rl.NumVertexStreams();
	}

	uint32_t MtlEditorCore::NumVertexStreamUsages(uint32_t mesh_id, uint32_t stream_index) const
	{
		Renderable const & mesh = *model_->Mesh(mesh_id);
		RenderLayout const & rl = mesh.GetRenderLayout();
		return static_cast<uint32_t>(rl.VertexStreamFormat(stream_index).size());
	}

	uint32_t MtlEditorCore::VertexStreamUsage(uint32_t mesh_id, uint32_t stream_index, uint32_t usage_index) const
	{
		Renderable const & mesh = *model_->Mesh(mesh_id);
		RenderLayout const & rl = mesh.GetRenderLayout();
		return (rl.VertexStreamFormat(stream_index)[usage_index].usage << 16)
			| (rl.VertexStreamFormat(stream_index)[usage_index].usage_index);
	}

	uint32_t MtlEditorCore::MaterialID(uint32_t mesh_id) const
	{
		StaticMeshPtr mesh = checked_pointer_cast<StaticMesh>(model_->Mesh(mesh_id - 1));
		return mesh->MaterialID();
	}

	uint32_t MtlEditorCore::NumMaterials() const
	{
		return static_cast<uint32_t>(model_->NumMaterials());
	}

	char const * MtlEditorCore::MaterialName(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->name.c_str();
	}

	float3 const & MtlEditorCore::AlbedoMaterial(uint32_t mtl_id) const
	{
		return *reinterpret_cast<float3*>(&model_->GetMaterial(mtl_id)->albedo);
	}

	float MtlEditorCore::MetalnessMaterial(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->metalness;
	}

	float MtlEditorCore::GlossinessMaterial(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->glossiness;
	}

	float3 const & MtlEditorCore::EmissiveMaterial(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->emissive;
	}

	float MtlEditorCore::OpacityMaterial(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->albedo.w();
	}

	char const * MtlEditorCore::Texture(uint32_t mtl_id, uint32_t slot) const
	{
		return model_->GetMaterial(mtl_id)->tex_names[slot].c_str();
	}

	uint32_t MtlEditorCore::DetailMode(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->detail_mode;
	}

	float MtlEditorCore::HeightOffset(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->height_offset_scale.x();
	}

	float MtlEditorCore::HeightScale(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->height_offset_scale.y();
	}

	float MtlEditorCore::EdgeTessHint(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->tess_factors.x();
	}

	float MtlEditorCore::InsideTessHint(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->tess_factors.y();
	}

	float MtlEditorCore::MinTess(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->tess_factors.z();
	}

	float MtlEditorCore::MaxTess(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->tess_factors.w();
	}

	bool MtlEditorCore::TransparentMaterial(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->transparent;
	}

	float MtlEditorCore::AlphaTestMaterial(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->alpha_test;
	}

	bool MtlEditorCore::SSSMaterial(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->sss;
	}

	bool MtlEditorCore::TwoSidedMaterial(uint32_t mtl_id) const
	{
		return model_->GetMaterial(mtl_id)->two_sided;
	}

	void MtlEditorCore::MaterialID(uint32_t mesh_id, uint32_t mtl_id)
	{
		StaticMeshPtr mesh = checked_pointer_cast<StaticMesh>(model_->Mesh(mesh_id - 1));
		mesh->MaterialID(mtl_id - 1);
		this->UpdateMaterial(mtl_id - 1);
	}

	void MtlEditorCore::MaterialName(uint32_t mtl_id, std::string const & name)
	{
		auto mtl = model_->GetMaterial(mtl_id).get();
		mtl->name = name;
	}

	void MtlEditorCore::AlbedoMaterial(uint32_t mtl_id, float3 const & value)
	{
		auto mtl = model_->GetMaterial(mtl_id).get();
		mtl->albedo = float4(value.x(), value.y(), value.z(), mtl->albedo.w());
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::MetalnessMaterial(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->metalness = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::GlossinessMaterial(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->glossiness = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::EmissiveMaterial(uint32_t mtl_id, float3 const & value)
	{
		model_->GetMaterial(mtl_id)->emissive = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::OpacityMaterial(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->albedo.w() = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::Texture(uint32_t mtl_id, uint32_t slot, std::string const & name)
	{
		model_->GetMaterial(mtl_id)->tex_names[slot] = name;
		this->UpdateMaterial(mtl_id);
	}

	void MtlEditorCore::DetailMode(uint32_t mtl_id, uint32_t value)
	{
		model_->GetMaterial(mtl_id)->detail_mode = static_cast<RenderMaterial::SurfaceDetailMode>(value);
		this->UpdateEffectAttrib(mtl_id);
		this->UpdateTechniques(mtl_id);
	}

	void MtlEditorCore::HeightOffset(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->height_offset_scale.x() = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::HeightScale(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->height_offset_scale.y() = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::EdgeTessHint(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->tess_factors.x() = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::InsideTessHint(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->tess_factors.y() = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::MinTess(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->tess_factors.z() = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::MaxTess(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->tess_factors.w() = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::TransparentMaterial(uint32_t mtl_id, bool value)
	{
		model_->GetMaterial(mtl_id)->transparent = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::AlphaTestMaterial(uint32_t mtl_id, float value)
	{
		model_->GetMaterial(mtl_id)->alpha_test = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::SSSMaterial(uint32_t mtl_id, bool value)
	{
		model_->GetMaterial(mtl_id)->sss = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	void MtlEditorCore::TwoSidedMaterial(uint32_t mtl_id, bool value)
	{
		model_->GetMaterial(mtl_id)->two_sided = value;
		this->UpdateEffectAttrib(mtl_id);
	}

	uint32_t MtlEditorCore::CopyMaterial(uint32_t mtl_id)
	{
		auto const & model = checked_pointer_cast<DetailedSkinnedModel>(model_);
		return model->CopyMaterial(mtl_id);
	}

	uint32_t MtlEditorCore::ImportMaterial(std::string const & name)
	{
		auto const & model = checked_pointer_cast<DetailedSkinnedModel>(model_);
		return model->ImportMaterial(name);
	}

	void MtlEditorCore::ExportMaterial(uint32_t mtl_id, std::string const & name)
	{
		auto const & model = checked_pointer_cast<DetailedSkinnedModel>(model_);
		SaveRenderMaterial(model->GetMaterial(mtl_id), name);
	}

	uint32_t MtlEditorCore::SelectedMesh() const
	{
		return selected_obj_;
	}

	void MtlEditorCore::SelectMesh(uint32_t mesh_id)
	{
		selected_obj_ = mesh_id;
		this->UpdateSelectedMesh();
	}

	void MtlEditorCore::SkinningOn(bool on)
	{
		skinning_ = on;
		if (skinning_)
		{
			checked_pointer_cast<DetailedSkinnedModel>(model_)->RebindJoints();
		}
		else
		{
			checked_pointer_cast<DetailedSkinnedModel>(model_)->UnbindJoints();
		}
	}

	void MtlEditorCore::SkeletonOn(bool on)
	{
		if (skeleton_object_)
		{
			skeleton_object_->Visible(on);
		}
	}

	void MtlEditorCore::LightOn(bool on)
	{
		main_light_->Enabled(on);
	}

	void MtlEditorCore::FPSCameraOn(bool on)
	{
		is_fps_camera_ = on;
		if (on)
		{
			tb_controller_.DetachCamera();
			fps_controller_.AttachCamera(this->ActiveCamera());
		}
		else
		{
			fps_controller_.DetachCamera();
			tb_controller_.AttachCamera(this->ActiveCamera());
		}
	}

	void MtlEditorCore::LineModeOn(bool on)
	{
		deferred_rendering_->ForceLineMode(on);
	}

	void MtlEditorCore::ImposterModeOn(bool on)
	{
		if (imposter_)
		{
			imposter_->Visible(on);
			object_->Visible(!on);

			imposter_mode_ = on;
		}
		else
		{
			imposter_mode_ = false;
		}
	}

	void MtlEditorCore::Visualize(int index)
	{
		if (model_)
		{
			RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();

			if (0 == index)
			{
				model_->ForEachMesh([](Renderable& mesh)
					{
						checked_cast<DetailedSkinnedMesh*>(&mesh)->VisualizeLighting();
					});

				deferred_rendering_->SSVOEnabled(0, false);
				re.HDREnabled(true);
				re.PPAAEnabled(1);
				re.ColorGradingEnabled(true);
			}
			else
			{
				if (index < 10)
				{
					VertexElementUsage const veu = static_cast<VertexElementUsage>(index - 1);
					model_->ForEachMesh([veu](Renderable& mesh)
						{
							checked_cast<DetailedSkinnedMesh*>(&mesh)->VisualizeVertex(veu, 0);
						});
				}
				else
				{
					int const slot = index - 10;
					model_->ForEachMesh([slot](Renderable& mesh)
						{
							checked_cast<DetailedSkinnedMesh*>(&mesh)->VisualizeTexture(slot);
						});
				}

				deferred_rendering_->SSVOEnabled(0, false);
				re.HDREnabled(false);
				re.PPAAEnabled(0);
				re.ColorGradingEnabled(false);
			}
		}
	}

	void MtlEditorCore::MouseMove(int x, int y, uint32_t button)
	{
		if (mouse_down_in_wnd_)
		{
			mouse_tracking_mode_ = true;

			int2 pt(x, y);

			if (button & (MB_Left | MB_Middle | MB_Right))
			{
				float2 move_vec = pt - last_mouse_pt_;
				if (button & MB_Left)
				{
					if (is_fps_camera_)
					{
						fps_controller_.RotateRel(move_vec.x(), move_vec.y(), 0);
						update_selective_buffer_ = true;
					}
					else
					{
						tb_controller_.Rotate(move_vec.x(), move_vec.y());
						update_selective_buffer_ = true;
					}
				}
				else if (button & MB_Middle)
				{
					if (!is_fps_camera_)
					{
						tb_controller_.Move(move_vec.x(), move_vec.y());
						update_selective_buffer_ = true;
					}
				}
				else if (button & MB_Right)
				{
					if (!is_fps_camera_)
					{
						tb_controller_.Zoom(move_vec.x(), move_vec.y());
						update_selective_buffer_ = true;
					}
				}
			}

			last_mouse_pt_ = pt;
		}
	}

	void MtlEditorCore::MouseUp(int x, int y, uint32_t button)
	{
		if (mouse_down_in_wnd_)
		{
			mouse_down_in_wnd_ = false;

			if (mouse_tracking_mode_)
			{
				mouse_tracking_mode_ = false;
			}
			else
			{
				if (button & MB_Left)
				{
					uint32_t entity_id;
					Texture::Mapper mapper(*selective_cpu_tex_, 0, 0, TMA_Read_Only,
						x, y, 1, 1);
					uint8_t* p = mapper.Pointer<uint8_t>();
					if (0 == p[3])
					{
						if (EF_ABGR8 == selective_cpu_tex_->Format())
						{
							entity_id = p[0] | (p[1] << 8) | (p[2] << 16);
						}
						else
						{
							BOOST_ASSERT(EF_ARGB8 == selective_cpu_tex_->Format());

							entity_id = p[2] | (p[1] << 8) | (p[0] << 16);
						}
					}
					else
					{
						entity_id = 0;
					}

					update_select_entity_event_(entity_id);
				}
			}
		}
	}

	void MtlEditorCore::MouseDown(int x, int y, uint32_t button)
	{
		KFL_UNUSED(button);

		mouse_down_in_wnd_ = true;
		last_mouse_pt_ = int2(x, y);
	}

	void MtlEditorCore::KeyPress(int key)
	{
		switch (key)
		{
		case 'W':
			if (is_fps_camera_)
			{
				fps_controller_.Move(0, 0, 1);
				update_selective_buffer_ = true;
			}
			break;

		case 'S':
			if (is_fps_camera_)
			{
				fps_controller_.Move(0, 0, -1);
				update_selective_buffer_ = true;
			}
			break;

		case 'A':
			if (is_fps_camera_)
			{
				fps_controller_.Move(-1, 0, 0);
				update_selective_buffer_ = true;
			}
			break;

		case 'D':
			if (is_fps_camera_)
			{
				fps_controller_.Move(1, 0, 0);
				update_selective_buffer_ = true;
			}
			break;
		}
	}

	void MtlEditorCore::UpdateSelectedMesh()
	{
		if (selected_obj_ > 0)
		{
			RenderablePtr const & mesh = model_->Mesh(selected_obj_ - 1);
			OBBox obb;
			if ((this->NumFrames() > 0) && skinning_)
			{
				obb = MathLib::convert_to_obbox(
					checked_pointer_cast<SkinnedMesh>(mesh)->FramePosBound(static_cast<uint32_t>(curr_frame_ + 0.5f)));
			}
			else
			{
				obb = MathLib::convert_to_obbox(mesh->PosBound());
			}
			selected_bb_->FirstComponentOfType<RenderableLineBox>()->SetBox(obb);
			selected_bb_->TransformToParent(object_->TransformToParent());
			selected_bb_->Visible(true);
		}
		else
		{
			selected_bb_->Visible(false);
		}
	}

	void MtlEditorCore::UpdateEffectAttrib(uint32_t mtl_id)
	{
		model_->ForEachMesh([mtl_id](Renderable& mesh)
			{
				auto& detailed_mesh = *checked_cast<DetailedSkinnedMesh*>(&mesh);
				if (detailed_mesh.MaterialID() == static_cast<int32_t>(mtl_id))
				{
					detailed_mesh.UpdateEffectAttrib();
				}
			});
	}

	void MtlEditorCore::UpdateMaterial(uint32_t mtl_id)
	{
		model_->ForEachMesh([mtl_id](Renderable& mesh)
			{
				auto& detailed_mesh = *checked_cast<DetailedSkinnedMesh*>(&mesh);
				if (detailed_mesh.MaterialID() == static_cast<int32_t>(mtl_id))
				{
					detailed_mesh.UpdateMaterial();
				}
			});
	}

	void MtlEditorCore::UpdateTechniques(uint32_t mtl_id)
	{
		model_->ForEachMesh([mtl_id](Renderable& mesh)
			{
				auto& detailed_mesh = *checked_cast<DetailedSkinnedMesh*>(&mesh);
				if (detailed_mesh.MaterialID() == static_cast<int32_t>(mtl_id))
				{
					detailed_mesh.UpdateTechniques();
				}
			});
	}
}
