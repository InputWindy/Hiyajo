#pragma once

#include "UIFeatureApi.h"
#include <Engine/Layer.h>
#include <Render.h>
#include <RDG.h>
#include <RHI/RHIResources.h>

namespace Maho
{

/**
 * Shader type consumed by FRender::TryGetShader<T>. Pure STATIC source accessors --
 * the compile + bytecode cache + content hashes live in the shared TShaderHandle
 * state, so a feature never owns the bytecode or a native module. The static bodies
 * are defined in the .cpp (the GLSL strings stay private to the feature). Exactly
 * the same contract as FTriangleShader -- the UI shader path is the generic one.
 */
struct FUIShader
{
	static const char* GetVertexSource();
	static const char* GetFragmentSource();
	static const char* GetVertexEntryPoint();
	static const char* GetFragmentEntryPoint();
};

/**
 * ImGui render feature - the render-side half of the UI integration. The CPU
 * side (the UI engine layer's ITick) builds the ImGui frame and pushes the draw
 * data to FScene via the sink; this feature draws that data in IRenderUI over
 * the shared SceneColor (LoadOp Load, after the scene), submitted before the
 * frame feature's present blit.
 *
 * Stateless draw feature: the UI shader goes through FRender::TryGetShader<FUIShader>
 * (async compile + per-type cache, above). The UI font backend resources (font
 * texture, sampler, descriptor set + layout, staging) are held HERE as BORROWED
 * pool handles -- the pool owns every native lifetime, so this feature neither owns
 * nor tears down a resource. Only the draw data (ImDrawData*) lives in FScene.
 */
class MAHO_UIFEATURE_API FUIFeature : public FLayer<IInitViews, IRenderUI>
{
MAHO_DECLARE_LAYER(FUIFeature, "UIFeature.dll");

	FUIFeature();

public:
	void InitViews(FRender& R) override;
	void RenderUI(FRender& R) override;

private:
	/** Lazily create the font backend (font texture, sampler, descriptor set+layout,
	 *  staging). Returns whether it is ready. Idempotent. */
	bool EnsureUIBackend(FRender& R);
	/** One-time font-atlas upload (a transfer submit, illegal inside a render pass).
	 *  No-op after the first call. */
	void UploadFont(FRender& R);

	// UI font backend -- borrowed pool handles (the pool owns the natives and
	// destroys them at Shutdown; FRender tears this feature down before the pool).
	// Not a lifecycle-owning backend: no unique_ptr, no manual teardown.
	bool bUIInit = false;
	bool bFontUploaded = false;
	FRDGTextureRef FontTexture;
	FRHISampler* FontSampler = nullptr;
	FRHIDescriptorSetLayout* FontSetLayout = nullptr;
	FRHIDescriptorSet* FontDescriptorSet = nullptr;
	FRDGBufferRef FontStaging;
};

} // namespace Maho
