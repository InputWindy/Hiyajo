#pragma once

#include "ContentBrowserApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Maho
{

/**
 * ContentBrowser - an editor component plugin mounting ONLY the IEditorPanel stage,
 * driven by the editor host (FExampleEditor) every frame.
 *
 * It is a view over the engine's virtual filesystem: the project's Content root
 * ("Game") and the engine's Content root ("Engine") registered by FPaths are scanned
 * on demand and shown as an UE-style two-pane browser (directory tree + item grid,
 * breadcrumb + search over both). Only container assets (.casset) are listed -- the
 * panel is an asset view, not a folder view.
 *
 * Drops: the host hands the panel the frame's OS-dropped files (FExampleEditor::
 * GetDroppedFiles); the panel claims them only while its window is hovered and routes
 * each one into the resource system (casset -> its recorded type, raster image ->
 * FTexture2D, anything else -> a logged "unsupported"). The panel never writes to
 * disk -- importing registers a resource, the file stays where the OS put it.
 *
 * The panel carries no ImGui/RHI resource ownership: the host owns the ImGui context.
 */
class FContentBrowser : public FLayer<IEditorPanel>
{
	MAHO_DECLARE_LAYER(FContentBrowser);

public:
	void Draw(FExampleEditor& Editor) override;

private:
	/** One scanned directory. VirtualPath is the display path ("Game/Textures"), the
	 *  entry name is its last segment. Dirs come first, then assets, alphabetically. */
	struct FNode
	{
		std::string Name;
		std::string VirtualPath;
		bool bDirectory = false;
		std::vector<FNode> Children;
	};

	/** Rescan both roots into Roots. On-demand only (first show / Refresh / F5) --
	 *  a directory walk is a filesystem hit, not a per-frame cost. */
	void Rescan();
	/** Collect the ".casset" files under PhysicalDir, named VirtualPrefix/... . */
	static void ScanDirectory(const std::filesystem::path& PhysicalDir, std::string VirtualPrefix, FNode& Out);
	/** The scanned node for a virtual directory path, or nullptr. */
	[[nodiscard]] const FNode* FindNode(std::string_view VirtualPath) const;
	/** The virtual path a physical drop belongs to: "Game/..." / "Engine/..." when it
	 *  lives under a registered root, otherwise the physical path itself (FPaths passes
	 *  an unmapped path through verbatim, so the resource key stays well-defined). */
	[[nodiscard]] std::string ToVirtualPath(const std::filesystem::path& PhysicalPath) const;

	/** Draw the tree pane / the item pane (search-filtered). */
	void DrawTree(const FNode& Node);
	void DrawItems(const FNode& Node);

	/** Claim + import the frame's dropped files (hover-gated by the caller). */
	void ImportDroppedFiles(FExampleEditor& Editor);
	/** Import one dropped file. Returns false when nothing was enqueued. */
	bool ImportFile(const std::string& PhysicalPath);

	void SetStatus(bool bError, std::string Text);

	std::vector<FNode> Roots;        // "Game", "Engine" (missing roots are skipped)
	bool bScanned = false;
	std::string CurrentPath = "Game";
	std::string SelectedPath;
	char SearchBuffer[128] = {};
	/** Result of the last refresh/import, shown in the panel's status line. */
	std::string Status;
	bool bStatusIsError = false;
};

} // namespace Maho
