#pragma once

#include <cstdint>

namespace Maho
{

/**
 * Rendering pipeline fixed stages, linear execution within a frame.
 * Engine-defined, not extensible.
 *
 * History: Old ERenderStage exposed to IRenderExtension for self-dispatch
 * is deprecated. That enum is kept as a type alias for internal use only.
 */
enum class ERenderPipelineStage : uint8_t
{
	/** Scene data upload, GPU buffer updates, prepare resources for later stages. */
	BeginFrame = 0,

	/** Depth pre-pass (optional, future). */
	DepthPrePass,

	/** Shadow map rendering (optional, future). */
	ShadowMap,

	/** Opaque base pass (GBuffer + Lighting / Forward). */
	BasePass,

	/** Translucent blending (optional, future). */
	Translucent,

	/** Post-processing (Bloom, Tonemap, etc., optional, future). */
	PostProcess,

	/** Editor UI / Game HUD / Present preparation. */
	EndFrame,

	COUNT
};

/** Internal alias to the old name. */
using ERenderStage = ERenderPipelineStage;

} // namespace Maho
