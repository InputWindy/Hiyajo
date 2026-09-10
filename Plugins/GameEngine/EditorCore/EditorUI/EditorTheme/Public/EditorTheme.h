#pragma once

#include "EditorThemeApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>
#include <UIView.h>

#include <memory>
#include <vector>

namespace Maho
{

/**
 * EditorTheme - an editor component plugin that exposes every editor theme
 * color for live tuning. It mounts IEditorPanel (no resources of its own) and
 * owns a persistent declarative UI tree (`UI::FUIView`): `Update` -- the
 * declaration phase, before the host's `NewFrame` -- rebuilds that tree; the
 * host translates it in-frame. The panel shows one color editor per exposed
 * slot grouped by the theme's logical sections plus one drag component per
 * numeric style field, live-applies edits to the live style, and Save/Load
 * persists the whole buffer to a text file (the default path is also what
 * ApplyMahoNightTheme restores at startup).
 *
 * Widget values live on the tree nodes: the backend writes them in place
 * during translation, so `Update` reads them back into `Colors`/`Styles`
 * before declaring (Reset/Load refresh the buffers first and mark them
 * authoritative for that frame instead).
 */
class FEditorTheme : public FLayer<IEditorInit, IEditorPanel, IEditorShutdown>
{
	MAHO_DECLARE_LAYER(FEditorTheme);

public:
	void Init(FExampleEditor& Editor) override;
	/** Declaration phase (host `NewFrame` before): rebuilds this view's tree only. */
	void Update(FExampleEditor& Editor) override;
	/** Unregisters the view (the registry only holds a raw pointer, never owns). */
	void Shutdown(FExampleEditor& Editor) override;

private:
	/** Refill the Colors + Styles buffers from the built-in palette defaults. */
	void ReloadDefaults();

	/** Lazy view creation + registration: the UI plugin may not be up during Init. */
	UI::FUIView* EnsureView(FExampleEditor& Editor);

	/** Persistent UI tree, owned here, registered in the UI view registry. */
	std::unique_ptr<UI::FUIView> View;

	/** Flat RGBA buffer, count*4 floats (see GetEditorThemeColorCount). */
	std::vector<float> Colors;
	/** Flat style buffer, styleCount*2 floats (see GetEditorThemeStyleCount). */
	std::vector<float> Styles;
	bool                LiveApply = true;
	/** Set by the Reset/Load handlers: this frame the buffers are the truth, so
	 *  the node read-back must not push their (stale) values back. */
	bool                bBuffersAuthoritative = false;
	char                PathBuffer[512] = { 0 };
	char                StatusBuffer[256] = { 0 };
};

} // namespace Maho
