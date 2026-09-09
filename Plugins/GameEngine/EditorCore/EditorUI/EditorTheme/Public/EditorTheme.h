#pragma once

#include "EditorThemeApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>

#include <vector>

namespace Maho
{

/**
 * EditorTheme - an editor component plugin that exposes every editor theme
 * color for live tuning. It mounts IEditorPanel only (no resources of its own):
 * each frame it reads the mutable flat color buffer, shows a color picker per
 * exposed slot (grouped by the theme's logical sections), and live-applies any
 * change to the host ImGui style. Save/Load persists the whole buffer to a text
 * file (the default path is also what ApplyMahoNightTheme restores at startup).
 */
class FEditorTheme : public FLayer<IEditorInit, IEditorPanel>
{
	MAHO_DECLARE_LAYER(FEditorTheme, "EditorTheme.dll");

public:
	void Init(FExampleEditor& Editor) override;
	void Draw(FExampleEditor& Editor) override;

private:
	/** Refill the Colors + Styles buffers from the built-in palette defaults. */
	void ReloadDefaults();

	/** Drains one grouped section of numeric style fields. Returns true if any
	 *  field was edited (for live re-apply). */
	bool DrawStyleSection();

	/** Flat RGBA buffer, count*4 floats (see GetEditorThemeColorCount). */
	std::vector<float> Colors;
	/** Flat style buffer, styleCount*2 floats (see GetEditorThemeStyleCount). */
	std::vector<float> Styles;
	bool                LiveApply = true;
	char                PathBuffer[512] = { 0 };
	char                StatusBuffer[256] = { 0 };
};

} // namespace Maho
