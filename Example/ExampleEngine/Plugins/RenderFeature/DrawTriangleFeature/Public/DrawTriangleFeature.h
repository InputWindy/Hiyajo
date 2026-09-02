#pragma once

#include "DrawTriangleFeatureApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>

namespace Maho
{

/**
 * Shader type consumed by FRender::TryGetShader<T>. Pure STATIC source accessors --
 * the compile + bytecode cache + content hashes live in the shared TShaderHandle
 * state, so a feature never owns the bytecode or a native module. The static bodies
 * are defined in the .cpp (the GLSL strings stay private to the feature).
 */
struct FTriangleShader
{
	static const char* GetVertexSource();
	static const char* GetFragmentSource();
	static const char* GetVertexEntryPoint();
	static const char* GetFragmentEntryPoint();
};

/**
 * DrawTriangleFeature - a render feature that compiles a fullscreen triangle
 * shader and draws it into the shared FScene::SceneColor target each frame
 * (dynamic rendering, Load over the scene clear). In IRender it fills a pipeline
 * config + shader type + the render attachments and hands them to FRender::AddPass,
 * which resolves the PSO from the pool, starts the render pass and binds the
 * pipeline implicitly -- the feature owns no pipeline, layout, shader module or raw
 * RHI pointer.
 */
class FDrawTriangleFeature : public FLayer<IRender>
{
MAHO_DECLARE_LAYER(FDrawTriangleFeature, "DrawTriangleFeature.dll");

	FDrawTriangleFeature();
	~FDrawTriangleFeature() override;

private:
	void Render(FRender& R) override;
};

} // namespace Maho
