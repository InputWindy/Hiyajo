#pragma once

#include "EditorOutlinerApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>

namespace Maho
{

// EditorOutliner - an IEditorPanel component. Lists the game-world alive entities each
// frame and publishes the clicked Index into the shared FEditorContext::SelectedEntityId
// (a later panel -- the Inspector -- reads it). Draw is driven by the host's frame loop.
class FEditorOutliner : public FLayer<IEditorPanel>
{
	MAHO_DECLARE_LAYER(FEditorOutliner, "EditorOutliner.dll");

public:
	void Draw(FExampleEditor& Editor) override;
};

} // namespace Maho
