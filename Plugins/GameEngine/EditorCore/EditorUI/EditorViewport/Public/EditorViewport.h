#pragma once

#include "EditorViewportApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>

namespace Maho
{

// EditorViewport - a single editor component plugin. It mounts ONLY the IEditorPanel
// stage interface; the editor HOST (FExampleEditor) drives it every frame through
// Cast<IEditorPanel>() -> Draw(). It draws the scene mirror (SceneColor) into its own
// ImGui window inside the host's dock space. The component carries no ImGui/RHI resource
// ownership -- the host owns the editor's ImGui context, font and EditorRT.
//
// An editor component may mount IEditorInit / IEditorShutdown too (driven by the host's
// FLayerCollector install/uninstall graph) if it needs per-install setup; this one does not.
class FEditorViewport : public FLayer<IEditorPanel>
{
	MAHO_DECLARE_LAYER(FEditorViewport);

public:
	void Draw(FExampleEditor& Editor) override;
};

} // namespace Maho
