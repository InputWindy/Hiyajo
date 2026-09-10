#include "ContentBrowser.h"

#include <AssetTypes.h>
#include <Log.h>
#include <Paths.h>
#include <Resource.h>
#include <UIView.h>
#include <UIViewRegistry.h>
#include <Widgets/FUIBox.h>
#include <Widgets/FUIButton.h>
#include <Widgets/FUIGrid.h>
#include <Widgets/FUIInputText.h>
#include <Widgets/FUIPanel.h>
#include <Widgets/FUISelectable.h>
#include <Widgets/FUIText.h>
#include <Widgets/FUITooltip.h>
#include <Widgets/FUITreeNode.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Maho
{

namespace
{
/** Node ids: 同级唯一即可（事件路由走根→目标的 Id 路径）。 */
constexpr const char* kIdToolbar    = "ContentBrowser.Toolbar";
constexpr const char* kIdBtnRefresh = "ContentBrowser.Button.Refresh";
constexpr const char* kIdSearch     = "ContentBrowser.Search";
constexpr const char* kIdCrumbs     = "ContentBrowser.Breadcrumb";
constexpr const char* kIdPanes      = "ContentBrowser.Panes";
constexpr const char* kIdTree       = "ContentBrowser.Tree";
constexpr const char* kIdItems      = "ContentBrowser.Items";
constexpr const char* kIdGrid       = "ContentBrowser.Grid";
constexpr const char* kIdEmpty      = "ContentBrowser.Empty";
constexpr const char* kIdTip        = "ContentBrowser.Tip";
constexpr const char* kIdStatus     = "ContentBrowser.Status";

/** 条目单元尺寸。旧版 `CellWidth = 150` / `CellHeight = 34`，列间距取 ImGui 默认
 *  `ItemSpacing.x` 的量级（8），与 `FUIGrid::SetCellSpacing` 一致。 */
constexpr float kCellWidth = 150.0f;
constexpr float kCellHeight = 34.0f;
constexpr float kCellSpacing = 8.0f;

/** Browsed roots, in display order. The alias is the FPaths root name AND the leading
 *  virtual-path segment, so a node maps to a path without a second lookup table. */
constexpr const char* RootAliases[] = { "Game", "Engine" };

std::string ToLower(std::string_view Text)
{
	std::string Out(Text);
	std::transform(Out.begin(), Out.end(), Out.begin(),
		[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return Out;
}

/** Lower-cased ".ext" of a path (empty when it has none). */
std::string ExtensionLower(const std::filesystem::path& Path)
{
	return ToLower(Path.extension().string());
}

/** Raster image extensions the resource system can decode from raw bytes (no casset
 *  magic). Mirrors TextureImageCodec::IsRasterExtension -- that header lives in the
 *  Asset plugin's private tree, so this panel keeps its own copy of the accepted set. */
bool IsRasterImageExtension(std::string_view Ext)
{
	return Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg"
		|| Ext == ".bmp" || Ext == ".tif" || Ext == ".tiff"
		|| Ext == ".gif" || Ext == ".ico";
}

/** True when the leading bytes carry a known raster image signature. The importer
 *  decodes by content (TextureImageCodec), so a raster image wearing a foreign
 *  extension -- a `.casset` that is really a PNG -- still imports; the header, not the
 *  name, is what the file IS. */
bool HasRasterSignature(std::span<const std::uint8_t> Bytes)
{
	const auto StartsWith = [Bytes](std::initializer_list<std::uint8_t> Sig) -> bool
	{
		if (Bytes.size() < Sig.size())
		{
			return false;
		}
		return std::equal(Sig.begin(), Sig.end(), Bytes.begin());
	};

	return StartsWith({ 0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au })   // PNG
		|| StartsWith({ 0xFFu, 0xD8u, 0xFFu })                                 // JPEG
		|| StartsWith({ 'B', 'M' })                                            // BMP
		|| StartsWith({ 'G', 'I', 'F', '8' })                                  // GIF
		|| StartsWith({ 'I', 'I', 0x2Au, 0x00u })                              // TIFF LE
		|| StartsWith({ 'M', 'M', 0x00u, 0x2Au })                              // TIFF BE
		|| StartsWith({ 0x00u, 0x00u, 0x01u, 0x00u });                         // ICO
}

/** Child virtual path = parent + "/" + leaf. Virtual paths are always forward-slashed
 *  (display form); the physical form is FPaths::Resolve's business. */
std::string JoinVirtualPath(std::string_view Parent, std::string_view Leaf)
{
	std::string Out(Parent);
	Out += '/';
	Out += Leaf;
	return Out;
}

/** 取（必要时新建）子节点，并报告它是否是本帧新建的 —— 新建才需要播种默认配置
 *  （输入框初值 / 事件订阅；逐帧重来会累积订阅）。 */
template <typename T>
T& Ensure(UI::FUIBuilder& Parent, UI::FUIName Id, bool& bOutCreated)
{
	bOutCreated = (Parent.FindChild(Id) == nullptr);
	return Parent.AddItem<T>(Id);
}

} // namespace

UI::FUIView* FContentBrowser::EnsureView(FExampleEditor& Editor)
{
	if (View != nullptr)
	{
		return View.get();
	}

	// 注册表由 UI 插件发布；插件未起来（或已关）时返回 nullptr，下一帧再试。
	UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry();
	if (Registry == nullptr)
	{
		return nullptr;
	}

	std::unique_ptr<UI::FUIView> NewView = std::make_unique<UI::FUIView>(UI::FUIName("ContentBrowser"));
	// 外壳开窗（含宿主自己的 dockspace id）由宿主通用循环做；标题即旧窗口名。
	// 关闭框归宿主所有：旧版也没有绑定关闭语义，故这里不订阅 `WindowClosed`。
	NewView->SetWindowShell(true, "Content Browser", UI::FUIVector2{}, UI::FUIVector2{}, UI::EUIShellFlags::NoCollapse);
	NewView->SetRenderContext(Editor.GetUIRenderContext());
	Registry->RegisterView(*NewView);
	View = std::move(NewView);
	return View.get();
}

void FContentBrowser::Update(FExampleEditor& Editor)
{
	UI::FUIView* PanelView = EnsureView(Editor);
	if (PanelView == nullptr)
	{
		return;
	}

	// First frame: populate the tree. Afterwards only an explicit refresh re-walks
	// (a directory scan is a filesystem hit -- not a per-frame cost).
	if (!bScanned)
	{
		Rescan();
	}

	// -- 值回读 --------------------------------------------------------------
	// 翻译期后端原地改写节点的值（用户输入的真值在节点上），故先收回缓冲再按缓冲声明；
	// 用户编辑因此跨帧存活。
	if (const auto* SearchNode = dynamic_cast<const UI::FUIInputText*>(PanelView->Find(UI::FUIName(kIdSearch))))
	{
		std::strncpy(SearchBuffer, SearchNode->GetValue().c_str(), sizeof(SearchBuffer) - 1);
		SearchBuffer[sizeof(SearchBuffer) - 1] = '\0';
	}

	// The scanned node for the current directory (falls back to the first root after a
	// rescan removed it -- e.g. the directory deleted while the panel was open).
	const FNode* Current = FindNode(CurrentPath);
	if (Current == nullptr && !Roots.empty())
	{
		CurrentPath = Roots.front().VirtualPath;
		Current = &Roots.front();
	}

	// -- 悬停归属 / 网格列数：都用「上一帧翻译写回的运行期状态」 -----------------
	// 旧版读窗口矩形与鼠标位置判 `IsWindowHovered`；新树的事件不携带指针位置，改用
	// 「本视图内是否有节点被悬停」近似（见报告）。GLFW 把落点写进光标流后本帧才收到
	// 投放事件，故"上一帧悬停在浏览器上"与旧判定同义。
	const bool bOwnsDrop = IsAnyNodeHovered(PanelView->GetRoot());

	// 旧的 `GetContentRegionAvail().x` -> 上一帧实测的正文区宽度（外框扣面板内边距）。
	float ItemsWidth = 0.f;
	if (const UI::FUIBuilder* ItemsProbe = PanelView->Find(UI::FUIName(kIdItems)))
	{
		ItemsWidth = ItemsProbe->GetRect().W;
	}
	const float AvailWidth = (std::max)(0.f, ItemsWidth - 16.0f);   // 主题面板内边距 (8,6)
	// (std::max) - parenthesized so the Windows max() macro cannot swallow the call.
	const int Columns = (std::max)(1, static_cast<int>(AvailWidth / (kCellWidth + kCellSpacing)));

	// ---- 声明期：只改本视图的树 -------------------------------------------
	{
		UI::FUIEditScope Scope = PanelView->Edit();
		UI::FUIBuilder& Root = Scope.GetRoot();
		Root.Layout().SetDirection(UI::EUIDirection::Column);
		Root.Layout().SetSpacing(4.f);

		bool bNew = false;

		// -- 工具栏行：Refresh + 搜索框 --------------------------------------
		{
			UI::FUIBox& Toolbar = Ensure<UI::FUIBox>(Root, UI::FUIName(kIdToolbar), bNew);
			Toolbar.Layout().SetDirection(UI::EUIDirection::Row);
			Toolbar.Layout().SetSpacing(6.f);
			Toolbar.Layout().VerticalAlign = UI::EUIAlign::Stretch;   // 行内控件等高
			Toolbar.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

			UI::FUIButton& RefreshButton = Ensure<UI::FUIButton>(Toolbar, UI::FUIName(kIdBtnRefresh), bNew);
			RefreshButton.SetLabel("Refresh");
			if (bNew)
			{
				RefreshButton.OnClick([this](UI::FUIBuilder&) { Rescan(); });
			}

			UI::FUIInputText& SearchBox = Ensure<UI::FUIInputText>(Toolbar, UI::FUIName(kIdSearch), bNew);
			SearchBox.SetHint("Search assets (name substring)...");
			SearchBox.SetMaxLength(sizeof(SearchBuffer) - 1);
			if (bNew)
			{
				SearchBox.SetValue(SearchBuffer);   // 之后以用户输入为准（不再逐帧压回）
			}
			// 旧 `SetNextItemWidth(-1)`：占满本行剩余宽度。
			SearchBox.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());
		}

		// -- 面包屑：/Game/Textures/Env，每段可点 ------------------------------
		{
			UI::FUIBox& Crumbs = Ensure<UI::FUIBox>(Root, UI::FUIName(kIdCrumbs), bNew);
			Crumbs.Layout().SetDirection(UI::EUIDirection::Row);
			Crumbs.Layout().SetSpacing(0.f);   // 旧 `SameLine(0, 0)`
			Crumbs.Layout().VerticalAlign = UI::EUIAlign::Center;
			Crumbs.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());
			DeclareBreadcrumb(Crumbs);
		}

		// -- 两栏：左目录树（定宽 220）/ 右条目网格，高度吃满剩余空间 ----------
		UI::FUIBox& Panes = Ensure<UI::FUIBox>(Root, UI::FUIName(kIdPanes), bNew);
		Panes.Layout().SetDirection(UI::EUIDirection::Row);
		Panes.Layout().SetSpacing(6.f);
		Panes.Layout().VerticalAlign = UI::EUIAlign::Stretch;
		// 高度 Fill = 同级其余项都是内容高，两栏自然拿到「剩余空间」（旧 ListHeight）。
		Panes.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Fill());

		// 左：目录树。子节点逐帧重建（扫描结果可能变），滚动量落在面板自己身上。
		UI::FUIPanel& TreePane = Ensure<UI::FUIPanel>(Panes, UI::FUIName(kIdTree), bNew);
		TreePane.SetChrome(UI::EUIPanelChrome::Full);
		TreePane.SetScrollable(true);
		TreePane.Layout().SetSize(UI::FUILength::Fixed(220.f), UI::FUILength::Fill());
		TreePane.Layout().SetSpacing(1.f);
		TreePane.ResetChildren();
		for (const FNode& ScannedRoot : Roots)
		{
			DeclareTree(TreePane, ScannedRoot);
		}

		// 右：当前目录的条目（搜索时搜索框生效）。
		UI::FUIPanel& ItemsPane = Ensure<UI::FUIPanel>(Panes, UI::FUIName(kIdItems), bNew);
		ItemsPane.SetChrome(UI::EUIPanelChrome::Full);
		ItemsPane.SetScrollable(true);
		ItemsPane.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Fill());
		ItemsPane.ResetChildren();
		if (Current != nullptr)
		{
			DeclareItems(ItemsPane, *Current, Columns);
		}

		// -- 状态行：上次扫描 / 导入的结果 ------------------------------------
		UI::FUIText& StatusText = Ensure<UI::FUIText>(Root, UI::FUIName(kIdStatus), bNew);
		StatusText.SetText(Status.empty() ? "-" : Status);
		StatusText.Style()[UI::EUIState::Normal].Text = bStatusIsError
			? UI::FUIColor{ 0.90f, 0.35f, 0.35f, 1.0f }
			: UI::FUIColor{ 0.85f, 0.85f, 0.85f, 1.0f };
	}

	if (bOwnsDrop && !Editor.GetDroppedFiles().empty())
	{
		ImportDroppedFiles(Editor);
	}
}

void FContentBrowser::DeclareBreadcrumb(UI::FUIBuilder& Row)
{
	Row.ResetChildren();

	std::string Accumulated;
	std::size_t Start = 0;
	const std::string Path = CurrentPath.empty() ? std::string(RootAliases[0]) : CurrentPath;
	bool bFirst = true;
	while (Start <= Path.size())
	{
		const std::size_t Slash = Path.find('/', Start);
		const std::string Segment = Path.substr(Start, Slash == std::string::npos ? std::string::npos : Slash - Start);
		if (!Segment.empty())
		{
			Accumulated = Accumulated.empty() ? Segment : JoinVirtualPath(Accumulated, Segment);
			// 旧版是 `TextUnformatted("/")` + `SmallButton(段名)`；这里合成一个按钮，
			// 视觉等价（`/Game/Textures/Env`），并省掉分隔符节点的独立 Id。
			UI::FUIButton& Crumb = Row.AddItem<UI::FUIButton>(
				UI::FUIName(std::string("ContentBrowser.Crumb.") + Accumulated));
			Crumb.SetLabel(bFirst ? Segment : ("/" + Segment));
			Crumb.OnClick([this, Target = Accumulated](UI::FUIBuilder&) { CurrentPath = Target; });
			bFirst = false;
		}
		if (Slash == std::string::npos)
		{
			break;
		}
		Start = Slash + 1;
	}
}

void FContentBrowser::DeclareTree(UI::FUIBuilder& Parent, const FNode& Node)
{
	// 展开态：节点自己的展开位是持久字段，但树逐帧整体重建会丢掉它，故以面板侧的
	// `Expanded` 为权威；缺省目录展开（旧 `ImGuiTreeNodeFlags_DefaultOpen`）。
	const auto It = Expanded.find(Node.VirtualPath);
	const bool bOpen = (It != Expanded.end()) ? It->second : Node.bDirectory;

	UI::FUITreeNode& TreeNode = Parent.AddItem<UI::FUITreeNode>(UI::FUIName(Node.VirtualPath));
	TreeNode.SetLabel(Node.Name);
	TreeNode.SetOpen(bOpen);
	TreeNode.SetSelected(Node.VirtualPath == CurrentPath);
	// 整行可点：新控件把「箭头折叠」与「标签浏览」合成一次点击，故只有"向展开方向"的
	// 点击才跟随浏览，避免"折叠某目录"顺带切换右栏。
	TreeNode.OnToggled([this, Path = Node.VirtualPath, bIsDirectory = Node.bDirectory](UI::FUIBuilder&, bool bNewOpen)
	{
		Expanded[Path] = bNewOpen;
		if (bIsDirectory && bNewOpen)
		{
			CurrentPath = Path;
		}
	});

	if (!bOpen)
	{
		return;
	}
	for (const FNode& Child : Node.Children)
	{
		DeclareTree(TreeNode, Child);
	}
}

void FContentBrowser::DeclareItems(UI::FUIBuilder& Parent, const FNode& Node, int Columns)
{
	const std::string Needle = ToLower(SearchBuffer);
	const bool bSearching = !Needle.empty();

	// Flatten what to show: the current directory's children, or -- while searching --
	// every asset under the current ROOT whose name matches (the tree still shows the
	// paths those assets live on, per the browse requirement).
	std::vector<const FNode*> Items;
	if (bSearching)
	{
		const std::string RootPath = Node.VirtualPath.substr(0, Node.VirtualPath.find('/'));
		const FNode* Root = FindNode(RootPath);
		if (Root != nullptr)
		{
			std::vector<const FNode*> Pending{ Root };
			while (!Pending.empty())
			{
				const FNode* Cur = Pending.back();
				Pending.pop_back();
				for (const FNode& Child : Cur->Children)
				{
					if (Child.bDirectory)
					{
						Pending.push_back(&Child);
					}
					else if (ToLower(Child.Name).find(Needle) != std::string::npos)
					{
						Items.push_back(&Child);
					}
				}
			}
		}
	}
	else
	{
		for (const FNode& Child : Node.Children)
		{
			Items.push_back(&Child);
		}
	}

	if (Items.empty())
	{
		UI::FUIText& Empty = Parent.AddItem<UI::FUIText>(UI::FUIName(kIdEmpty));
		Empty.SetText(bSearching ? "No matching asset." : "No asset here.");
		return;
	}

	// Cell grid: a fixed-size selectable per item. 列数由面板按上一帧实测宽度算好
	// （旧版直接问 `GetContentRegionAvail()`），故这里只声明列数/单元尺寸。
	UI::FUIGrid& Grid = Parent.AddItem<UI::FUIGrid>(UI::FUIName(kIdGrid));
	Grid.SetColumns(Columns);
	Grid.SetCellSize(kCellWidth, kCellHeight);
	Grid.SetCellSpacing(kCellSpacing);
	if (!SelectedPath.empty())
	{
		Grid.SetSelected(UI::FUIName(SelectedPath));   // 选中框（直接子项 Id 匹配）
	}

	for (const FNode* ItemPtr : Items)
	{
		const FNode& Item = *ItemPtr;
		UI::FUISelectable& Cell = Grid.AddItem<UI::FUISelectable>(UI::FUIName(Item.VirtualPath));
		Cell.SetLabel((Item.bDirectory ? "[+] " : "    ") + Item.Name);
		// 选中高亮走基类的选中位（样式解析按它取 Selected 组）—— FUISelectable 自己的
		// SetSelected 只写它的镜像成员，不参与样式解析，故这里显式限定基类。
		static_cast<UI::FUIBuilder&>(Cell).SetSelected(Item.VirtualPath == SelectedPath);
		Cell.OnSelected([this, Path = Item.VirtualPath, bIsDirectory = Item.bDirectory](UI::FUIBuilder&)
		{
			if (bIsDirectory)
			{
				CurrentPath = Path;
			}
			else
			{
				SelectedPath = Path;
				SetStatus(false, Path);
			}
		});

		if (!Item.bDirectory)
		{
			// 资产拖放源：载荷是资产的虚拟路径。旧版用本项目自己的载荷类型
			// ("MAHO_ASSET_V1")；新树的拖放通道把类型固定为 kUIPayloadType，值是一个
			// FUIName（见报告）。
			Cell.SetDragSource(UI::FUIName(Item.VirtualPath));
		}

		// 悬停提示 = 资产的虚拟路径（旧 `SetTooltip`）。提示节点必须挂在被悬停的控件
		// 之下：翻译器按父节点的悬停位开提示窗。
		UI::FUITooltip& Tip = Cell.AddItem<UI::FUITooltip>(UI::FUIName(kIdTip));
		Tip.SetText(Item.VirtualPath);
	}
}

bool FContentBrowser::IsAnyNodeHovered(const UI::FUIBuilder& Node) const
{
	if (Node.GetState().bHovered)
	{
		return true;
	}
	for (const std::unique_ptr<UI::FUIBuilder>& Child : Node.GetChildren())
	{
		if (Child && IsAnyNodeHovered(*Child))
		{
			return true;
		}
	}
	return false;
}

void FContentBrowser::Rescan()
{
	Roots.clear();
	std::error_code Ec;
	Paths::FPaths* P = Paths::GetPaths();
	if (P == nullptr)
	{
		bScanned = true;
		SetStatus(true, "Paths layer is not initialized - nothing to browse");
		MAHO_LOG(ELogLevel::Error, "ContentBrowser", "Scan skipped: the Paths layer is not initialized");
		return;
	}

	std::size_t AssetCount = 0;
	for (const char* Alias : RootAliases)
	{
		if (!P->HasRoot(Alias))
		{
			continue;
		}
		// A missing directory is a valid (empty) content root, not an error: the panel
		// simply shows nothing under that root.
		const std::filesystem::path Physical = P->Resolve(Alias);
		Ec.clear();
		if (!std::filesystem::is_directory(Physical, Ec))
		{
			continue;
		}

		Roots.push_back(FNode{ Alias, Alias, true, {} });
		ScanDirectory(Physical, Alias, Roots.back());
	}

	const std::function<std::size_t(const FNode&)> CountAssets = [&](const FNode& N) -> std::size_t
	{
		std::size_t Count = 0;
		for (const FNode& Child : N.Children)
		{
			Count += Child.bDirectory ? CountAssets(Child) : 1;
		}
		return Count;
	};
	for (const FNode& Root : Roots)
	{
		AssetCount += CountAssets(Root);
	}

	bScanned = true;
	if (FindNode(CurrentPath) == nullptr && !Roots.empty())
	{
		CurrentPath = Roots.front().VirtualPath;
	}
	SetStatus(false, "Scan complete - " + std::to_string(AssetCount) + " asset(s) under "
		+ std::to_string(Roots.size()) + " root(s)");
	MAHO_LOG(ELogLevel::Info, "ContentBrowser", "Scan complete: {} asset(s) under {} root(s)", AssetCount, Roots.size());
}

void FContentBrowser::ScanDirectory(const std::filesystem::path& PhysicalDir, std::string VirtualPrefix, FNode& Out)
{
	// Only container assets are listed -- the browser is an asset view, not a folder
	// view; a .png sitting next to a .casset is invisible here.
	std::error_code Ec;
	for (std::filesystem::recursive_directory_iterator It(PhysicalDir, std::filesystem::directory_options::skip_permission_denied, Ec), End;
		It != End; It.increment(Ec))
	{
		if (Ec)
		{
			break;
		}
		const std::filesystem::directory_entry& Entry = *It;
		// Hidden entries (dot-prefixed) are not part of the content view, and a hidden
		// directory's subtree is skipped entirely.
		const std::string EntryName = Entry.path().filename().string();
		if (!EntryName.empty() && EntryName.front() == '.')
		{
			std::error_code DirEc;
			if (Entry.is_directory(DirEc))
			{
				It.disable_recursion_pending();
			}
			continue;
		}
		std::error_code QueryEc;
		if (!Entry.is_regular_file(QueryEc))
		{
			continue;
		}
		if (ExtensionLower(Entry.path()) != ".casset")
		{
			continue;
		}

		std::error_code RelEc;
		const std::filesystem::path Rel = std::filesystem::relative(Entry.path(), PhysicalDir, RelEc);
		if (RelEc || Rel.empty())
		{
			continue;
		}

		// Create/descend the intermediate directory nodes so the tree mirrors the disk.
		FNode* Cur = &Out;
		for (const std::filesystem::path& Segment : Rel.parent_path())
		{
			const std::string Name = Segment.string();
			if (Name.empty() || Name == ".")
			{
				continue;
			}
			FNode* Found = nullptr;
			for (FNode& Child : Cur->Children)
			{
				if (Child.bDirectory && Child.Name == Name)
				{
					Found = &Child;
					break;
				}
			}
			if (Found == nullptr)
			{
				Cur->Children.push_back(FNode{ Name, JoinVirtualPath(Cur->VirtualPath, Name), true, {} });
				Found = &Cur->Children.back();
			}
			Cur = Found;
		}
		const std::string FileName = Rel.filename().string();
		Cur->Children.push_back(FNode{ FileName, JoinVirtualPath(Cur->VirtualPath, FileName), false, {} });
	}

	// Directory entries before assets, each alphabetically (UE-style listing).
	const std::function<void(FNode&)> SortRecursive = [&](FNode& N) -> void
	{
		std::sort(N.Children.begin(), N.Children.end(), [](const FNode& A, const FNode& B)
		{
			if (A.bDirectory != B.bDirectory)
			{
				return A.bDirectory;
			}
			return ToLower(A.Name) < ToLower(B.Name);
		});
		for (FNode& Child : N.Children)
		{
			SortRecursive(Child);
		}
	};
	SortRecursive(Out);
}

const FContentBrowser::FNode* FContentBrowser::FindNode(std::string_view VirtualPath) const
{
	for (const FNode& Root : Roots)
	{
		if (Root.VirtualPath == VirtualPath)
		{
			return &Root;
		}
		// Depth-first over the subtree; the node is returned by pointer into the tree,
		// which is stable until the next Rescan.
		std::vector<const FNode*> Pending{ &Root };
		while (!Pending.empty())
		{
			const FNode* Cur = Pending.back();
			Pending.pop_back();
			for (const FNode& Child : Cur->Children)
			{
				if (Child.VirtualPath == VirtualPath)
				{
					return &Child;
				}
				if (Child.bDirectory)
				{
					Pending.push_back(&Child);
				}
			}
		}
	}
	return nullptr;
}

std::string FContentBrowser::ToVirtualPath(const std::filesystem::path& PhysicalPath) const
{
	std::error_code Ec;
	const std::filesystem::path Canonical = std::filesystem::weakly_canonical(PhysicalPath, Ec);
	const std::filesystem::path Full = Ec ? PhysicalPath : Canonical;

	if (Paths::FPaths* P = Paths::GetPaths())
	{
		for (const char* Alias : RootAliases)
		{
			if (!P->HasRoot(Alias))
			{
				continue;
			}
			std::error_code RelEc;
			const std::filesystem::path Rel = std::filesystem::relative(Full, P->Resolve(Alias), RelEc);
			if (RelEc || Rel.empty() || *Rel.begin() == "..")
			{
				continue;
			}
			return JoinVirtualPath(Alias, Rel.generic_string());
		}
	}

	// Outside both roots (e.g. a file dropped straight from the desktop): the physical
	// path is the source path. FPaths::Resolve passes an unmapped path through verbatim,
	// so the resource key is still derived from it -- and nothing is copied into Content/.
	return Full.generic_string();
}

void FContentBrowser::ImportDroppedFiles(FExampleEditor& Editor)
{
	std::vector<std::string> Dropped;
	if (!Editor.ConsumeDroppedFiles(Dropped))
	{
		return;
	}
	for (const std::string& PhysicalPath : Dropped)
	{
		ImportFile(PhysicalPath);
	}
}

bool FContentBrowser::ImportFile(const std::string& PhysicalPath)
{
	std::ifstream In(PhysicalPath, std::ios::binary);
	if (!In)
	{
		SetStatus(true, "Cannot open " + PhysicalPath);
		MAHO_LOG(ELogLevel::Error, "ContentBrowser", "Drop import failed: cannot open '{}'", PhysicalPath);
		return false;
	}
	const std::vector<std::uint8_t> Bytes((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
	In.close();

	if (Bytes.empty())
	{
		SetStatus(true, "Empty file " + PhysicalPath);
		MAHO_LOG(ELogLevel::Error, "ContentBrowser", "Drop import failed: '{}' is empty", PhysicalPath);
		return false;
	}

	Resource::FResourceSystem* System = Resource::GetResourceSystem();
	if (System == nullptr)
	{
		SetStatus(true, "Resource system is not initialized");
		MAHO_LOG(ELogLevel::Error, "ContentBrowser", "Drop import failed: the resource system is not initialized");
		return false;
	}

	const std::string SourcePath = ToVirtualPath(std::filesystem::path(PhysicalPath));
	Resource::FImportConfig Config;
	Config.SourcePath = SourcePath;

	// Import is async: this only reports whether the transfer was enqueued. The
	// resource system logs/broadcasts the outcome when the decode completes.
	const auto Enqueue = [&](bool bEnqueued, const char* TypeName) -> bool
	{
		if (!bEnqueued)
		{
			SetStatus(true, "Import rejected: " + SourcePath);
			MAHO_LOG(ELogLevel::Error, "ContentBrowser", "Import of '{}' as {} was rejected", SourcePath, TypeName);
			return false;
		}
		SetStatus(false, std::string("Importing ") + SourcePath + " as " + TypeName);
		MAHO_LOG(ELogLevel::Info, "ContentBrowser", "Importing '{}' as {}", SourcePath, TypeName);
		return true;
	};

	// The container header names the asset type; pick the matching importer.
	switch (Resource::PeekCassetAssetType(Bytes))
	{
	case Resource::EAssetType::Texture:
		return Enqueue(System->Import<Resource::FTexture2D>(Config), "Texture2D");
	case Resource::EAssetType::Material:
		return Enqueue(System->Import<Resource::FMaterial>(Config), "Material");
	case Resource::EAssetType::StaticMesh:
		return Enqueue(System->Import<Resource::FStaticMesh>(Config), "StaticMesh");
	case Resource::EAssetType::Skeleton:
		return Enqueue(System->Import<Resource::FSkeleton>(Config), "Skeleton");
	case Resource::EAssetType::Animation:
		return Enqueue(System->Import<Resource::FAnimation>(Config), "Animation");
	case Resource::EAssetType::AnimationGraph:
		return Enqueue(System->Import<Resource::FAnimationGraph>(Config), "AnimationGraph");
	case Resource::EAssetType::Prefab:
		return Enqueue(System->Import<Resource::FPrefab>(Config), "Prefab");
	default:
		break;
	}

	const std::string Ext = ExtensionLower(PhysicalPath);
	// No container header: the importer decides by content, so a raster image goes to the
	// texture importer whatever it is named (a `.casset` that is really a PNG included).
	// The extension stays a fallback signal -- a `.png` holding garbage still routes to
	// the texture importer and fails there, non-fatally.
	if (HasRasterSignature(Bytes) || IsRasterImageExtension(Ext))
	{
		return Enqueue(System->Import<Resource::FTexture2D>(Config), "Texture2D (raster source)");
	}

	if (Ext == ".casset")
	{
		// A container extension without a readable header: truncated or corrupted, not
		// something an importer can decode. Report it and stay alive.
		SetStatus(true, "Not a valid casset container: " + SourcePath);
		MAHO_LOG(ELogLevel::Error, "ContentBrowser", "Drop import failed: '{}' has a .casset extension but no valid container header", SourcePath);
		return false;
	}

	SetStatus(true, "Unsupported file " + SourcePath);
	MAHO_LOG(ELogLevel::Warn, "ContentBrowser", "Unsupported drop '{}': not a casset container and not a raster image", SourcePath);
	return false;
}

void FContentBrowser::SetStatus(bool bError, std::string Text)
{
	bStatusIsError = bError;
	Status = std::move(Text);
}

void FContentBrowser::Shutdown(FExampleEditor& Editor)
{
	(void)Editor;

	if (View == nullptr)
	{
		return;
	}
	if (UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry())
	{
		Registry->UnregisterView(*View);
	}
	View.reset();
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_CONTENTBROWSER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FContentBrowser::CreateLayer();
}
