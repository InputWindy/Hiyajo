#pragma once

#include "EditorInspectorApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>

namespace Maho
{

// EditorInspector - an IEditorPanel component. Reads the FEditorContext SelectedEntityId
// (published by the Outliner) and lets the user edit that entity's FTransform in place.
class FEditorInspector : public FLayer<IEditorPanel>
{
	MAHO_DECLARE_LAYER(FEditorInspector, "EditorInspector.dll");

public:
	void Draw(FExampleEditor& Editor) override;
};

} // namespace Maho
