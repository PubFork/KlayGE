// SceneObjectHelper.hpp
// KlayGE 一些常用的场景对象 头文件
// Ver 3.12.0
// 版权所有(C) 龚敏敏, 2005-2011
// Homepage: http://www.klayge.org
//
// 3.10.0
// SceneObjectSkyBox和SceneObjectHDRSkyBox增加了Technique() (2010.1.4)
//
// 3.9.0
// 增加了SceneObjectHDRSkyBox (2009.5.4)
//
// 3.1.0
// 初次建立 (2005.10.31)
//
// 修改记录
//////////////////////////////////////////////////////////////////////////////////

#ifndef _SCENEOBJECTHELPER_HPP
#define _SCENEOBJECTHELPER_HPP

#pragma once

#include <KlayGE/PreDeclare.hpp>
#include <KlayGE/SceneNode.hpp>
#include <KFL/AABBox.hpp>
#include <KlayGE/Mesh.hpp>

namespace KlayGE
{
	KLAYGE_CORE_API RenderModelPtr LoadLightSourceProxyModel(LightSourcePtr const& light,
		std::function<StaticMeshPtr(std::wstring_view)> CreateMeshFactoryFunc = CreateMeshFactory<RenderableLightSourceProxy>);

	class KLAYGE_CORE_API SceneObjectCameraProxy
	{
	public:
		explicit SceneObjectCameraProxy(CameraPtr const & camera);
		SceneObjectCameraProxy(CameraPtr const & camera, RenderModelPtr const & camera_model);
		SceneObjectCameraProxy(CameraPtr const & camera, std::function<StaticMeshPtr(std::wstring_view)> CreateMeshFactoryFunc);

		void Scaling(float x, float y, float z);
		void Scaling(float3 const & s);

		RenderModelPtr const & CameraModel() const
		{
			return camera_model_;
		}

		SceneNodePtr const & RootNode() const
		{
			return camera_model_->RootNode();
		}

	private:
		RenderModelPtr LoadModel(std::function<StaticMeshPtr(std::wstring_view)> CreateMeshFactoryFunc);

	protected:
		float4x4 model_scaling_;

		CameraPtr camera_;
		RenderModelPtr camera_model_;
	};
}

#endif		// _RENDERABLEHELPER_HPP
