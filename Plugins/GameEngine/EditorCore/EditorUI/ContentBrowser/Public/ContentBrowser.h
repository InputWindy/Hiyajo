#pragma once

#include "ContentBrowserApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>
#include <UIView.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

/**
 * ContentBrowser - an editor component plugin mounting the panel stage plus the
 * shutdown stage, driven by the editor host (FExampleEditor) every frame.
 *
 * It is a view over the engine's virtual filesystem: the project's Content root
 * ("Game") and the engine's Content root ("Engine") registered by FPaths are scanned
 * on demand and shown as an UE-style two-pane browser (directory tree + item grid,
 * breadcrumb + search over both). Only container assets (.casset) are listed -- the
 * panel is an asset view, not a folder view.
 *
 * The panel owns a persistent declarative UI tree (`UI::FUIView`): `Update` -- the
 * declaration phase, which the host runs before it begins its UI frame -- rebuilds
 * that tree; the host then translates it in-frame. The plugin never touches the
 * backend (no ImGui type or call lives here anymore).
 *
 * Drops: the host hands the panel the frame's OS-dropped files (FExampleEditor::
 * GetDroppedFiles); the panel claims them only while one of its own nodes was hovered
 * and routes each one into the resource system (casset -> its recorded type, raster
 * image -> FTexture2D, anything else -> a logged "unsupported"). The panel never
 * writes to disk -- importing registers a resource, the file stays where the OS put it.
 *
 * The panel carries no backend/RHI resource ownership: the host owns the UI context.
 */
class FContentBrowser : public FLayer<IEditorPanel, IEditorShutdown>
{
	MAHO_DECLARE_LAYER(FContentBrowser);

public:
	/** 声明期（宿主 `NewFrame` 之前）：只重建本视图的树，不碰后端。 */
	void Update(FExampleEditor& Editor) override;
	/** 注销视图（注册表只持裸指针，从不删除）。 */
	void Shutdown(FExampleEditor& Editor) override;

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

	/** Rescan both roots into Roots. On-demand only (first show / Refresh) --
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

	/** Lazy view creation + registration: the UI plugin may not be up during Init. */
	UI::FUIView* EnsureView(FExampleEditor& Editor);

	/** 声明期：把一棵扫描子树的树节点铺到 Parent 下（递归）。 */
	void DeclareTree(UI::FUIBuilder& Parent, const FNode& Node);
	/** 声明期：面包屑行（每段一个按钮，展开方向的那个目录即当前目录）。 */
	void DeclareBreadcrumb(UI::FUIBuilder& Row);
	/** 声明期：当前目录（搜索时 = 当前根下匹配的资产）的条目网格。 */
	void DeclareItems(UI::FUIBuilder& Parent, const FNode& Node, int Columns);

	/** 本视图内是否有节点被悬停 —— 旧 `IsWindowHovered` 的近似（见报告）：新树的事件
	 *  不携带指针位置，只能按上一帧翻译写回的节点悬停位判定。 */
	[[nodiscard]] bool IsAnyNodeHovered(const UI::FUIBuilder& Node) const;

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

	/** Persistent UI tree, owned here, registered in the UI view registry. */
	std::unique_ptr<UI::FUIView> View;

	/** 树节点的展开态。展开位本来是节点自己的持久字段，但树每帧整体重建会把它丢掉，
	 *  故面板侧记一份权威值（回调在 `Update` 之前抽干，故它与节点同帧一致）。
	 *  缺省：目录展开（旧 `ImGuiTreeNodeFlags_DefaultOpen`）。 */
	std::unordered_map<std::string, bool> Expanded;
};

} // namespace Maho
