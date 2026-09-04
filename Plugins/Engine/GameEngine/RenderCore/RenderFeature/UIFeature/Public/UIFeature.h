#pragma once

#include "UIFeatureApi.h"
#include <Engine/Layer.h>
#include <Render.h>
#include <RDG.h>
#include <RHI/RHIResources.h>

#include <cstdint>
#include <map>

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
 * side (FRender's ImGui context) builds the ImGui frame and stores the draw data
 * in FRender::UIData; this feature draws that data in IRenderUI over
 * the shared SceneColor (LoadOp Load, after the scene), submitted before the
 * frame feature's present blit.
 *
 * Stateless draw feature: the UI shader goes through FRender::TryGetShader<FUIShader>
 * (async compile + per-type cache, above). The font backend holds ONLY the RDG
 * font texture handle. NO raw RHI pointer lives here: the descriptor set layout, the
 * descriptor set and the sampler are each re-resolved from the resource pool
 * (content-addressable get-or-create, keyed by the PassParameter binding value that
 * produced them) on demand. The pool owns every native lifetime, so this feature
 * neither owns nor tears down a resource. Only the draw data (ImDrawData*) lives in
 * FScene.
 */
class MAHO_UIFEATURE_API FUIFeature : public FLayer<IOnInstalled, IInitViews, IRenderUI, IPreUnInstall>
{
MAHO_DECLARE_LAYER(FUIFeature, "UIFeature.dll");

	FUIFeature();

public:
	void OnInstalled(FRender& R) override;
	void InitViews(FRender& R) override;
	void RenderUI(FRender& R) override;
	void PreUnInstall(FRender& R) override;

	/** Test harness (called by FRender::Tick each UI frame): show every imported
	 *  texture mirror in an ImGui window, sized from its RDG mirror. Mirrors are
	 *  drawn between ImGui::NewFrame and ImGui::Render.
	 *
	 *  Virtual on purpose: FRender calls it through the UIFeature vtable (the UI
	 *  feature is loaded as a separate DLL that FRender never links), so a virtual
	 *  call generates no unresolved-symbol dependency across the DLL boundary. */
	virtual void DisplayMirrorImGui(FRender& R);

private:
	/** Lazily create the font backend (font texture, sampler, descriptor set+layout,
	 *  staging). Returns whether it is ready. Idempotent. */
	bool EnsureUIBackend(FRender& R);
	/** One-time font-atlas upload (a transfer submit, illegal inside a render pass).
	 *  No-op after the first call. */
	void UploadFont(FRender& R);

	/** Texture id -> UI texture registry. Every texture ImGui draws (the font atlas,
	 *  each mirror) is registered here with its RDG texture + shared sampler; ImGui's
	 *  ImTextureID is this integer id. The render path resolves a draw command's id to
	 *  its per-batch descriptor set (content-addressable) INSIDE AddPass -- so no
	 *  FRHIDescriptorSet* is ever produced or held here. */
	using FUIRegistryId = std::uintptr_t;
	struct FUIRegistryEntry
	{
		FRDGTextureRef Texture;
		FRHISampler* Sampler;   // pool-owned, content-addressable by desc
	};

	/** Register (or re-resolve) a texture for ImGui drawing; returns its id. The
	 *  sampler is created once per call (pool-cached) and never held. */
	[[nodiscard]] FUIRegistryId RegisterTexture(FRender& R, const FRDGTextureRef& Tex);
	/** Find a registered texture by its ImGui id; nullptr if unknown. */
	[[nodiscard]] const FUIRegistryEntry* FindTexture(FUIRegistryId Id) const;

	// UI font + texture registry. The UI backend holds ONLY RDG resources (the font
	// texture ref) and the id registry; every native RHI object (descriptor set
	// layout, descriptor set, sampler) is resolved in AddPass from the pass parameter
	// (content-addressable get-or-create in the pool), so this feature never owns or
	// tears down a native. The font-upload staging buffer is a one-shot transient
	// created locally inside the upload pass, never held.
	bool bUIInit = false;
	bool bFontUploaded = false;
	FRDGTextureRef FontTexture;
	FUIRegistryId FontId = 0;
	std::map<FUIRegistryId, FUIRegistryEntry> TextureRegistry;
	FUIRegistryId NextTextureId = 1;
	/** Guards the one-time AddUIBuilder registration in InitViews. */
	bool bMirrorBuilderRegistered = false;
};

} // namespace Maho
