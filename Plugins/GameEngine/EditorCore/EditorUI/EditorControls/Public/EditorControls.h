#pragma once

#include "EditorControlsApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>

namespace Maho
{

// EditorControls - an IEditorPanel component. A simple editor-mode toolbar/playback
// panel that reads/writes the shared FEditorContext (e.g. reset the selected entity).
// Demonstrates a control-style component that owns no scene resources.
class FEditorControls : public FLayer<IEditorPanel>
{
	MAHO_DECLARE_LAYER(FEditorControls, "EditorControls.dll");

public:
	void Draw(FExampleEditor& Editor) override;
};

} // namespace Maho
