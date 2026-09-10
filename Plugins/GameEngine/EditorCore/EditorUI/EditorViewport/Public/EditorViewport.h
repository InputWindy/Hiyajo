#pragma once

#include "EditorViewportApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>
#include <UIView.h>

#include <memory>

namespace Maho
{

/**
 * EditorViewport - a single editor component plugin. It mounts IEditorPanel (plus
 * IEditorShutdown for teardown) and owns a persistent declarative UI tree: `Update`
 * -- the declaration phase, before the host's `NewFrame` -- rebuilds that tree; the
 * host translates it in-frame. The tree is one image filling the panel, referencing
 * the live present target by NAME (`FExampleEditor::PresentTargetName()`); the host's
 * resolver maps that name to the RHI texture, so this component never sees an
 * ImTextureID and carries no ImGui/RHI resource ownership.
 */
class FEditorViewport : public FLayer<IEditorPanel, IEditorShutdown>
{
	MAHO_DECLARE_LAYER(FEditorViewport);

public:
	/** Declaration phase (host `NewFrame` before): rebuild this view's tree, then
	 *  publish the image's on-screen rect to the host (the game cursor re-bases to it). */
	void Update(FExampleEditor& Editor) override;
	/** Unregisters the view (the registry only holds a raw pointer, never owns). */
	void Shutdown(FExampleEditor& Editor) override;

private:
	/** Lazy view creation + registration: the UI plugin may not be up during install. */
	UI::FUIView* EnsureView(FExampleEditor& Editor);

	/** Persistent UI tree, owned here, registered in the UI view registry. */
	std::unique_ptr<UI::FUIView> View;
};

} // namespace Maho
