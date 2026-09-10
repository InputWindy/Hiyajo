#include "ContentBrowser.h"

#include "imgui.h"

#include <AssetTypes.h>
#include <Log.h>
#include <Paths.h>
#include <Resource.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <span>
#include <system_error>

namespace Maho
{

namespace
{
/** ImGui drag & drop payload id for "an asset in this browser". Project-owned so a
 *  foreign panel can recognise a content-browser drag by identity; the payload value is
 *  the asset's virtual path. This panel never accepts its own payload (dragging an asset
 *  back onto the browser is a no-op). */
constexpr const char* AssetDragPayload = "MAHO_ASSET_V1";

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

} // namespace

void FContentBrowser::Draw(FExampleEditor& Editor)
{
	const std::uint32_t DockId = Editor.GetEditorDockSpaceId();
	if (DockId != 0)
	{
		ImGui::SetNextWindowDockID(static_cast<ImGuiID>(DockId), ImGuiCond_FirstUseEver);
	}

	// First frame: populate the tree. Afterwards only an explicit refresh re-walks
	// (a directory scan is a filesystem hit -- not a per-frame cost).
	if (!bScanned)
	{
		Rescan();
	}

	if (!ImGui::Begin("Content Browser", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	// -- hover ownership of this frame's OS drop ---------------------------------
	// GLFW pushes the drop POINT into the cursor stream right before the drop event
	// (DragQueryPoint -> _glfwInputCursorPos), so the ImGui mouse position IS the drop
	// position this frame. Claim the batch only when the drop landed on this window;
	// otherwise leave it for the panel the user actually dropped onto.
	const ImVec2 WinMin = ImGui::GetWindowPos();
	const ImVec2 WinSize = ImGui::GetWindowSize();
	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	const bool bOwnsDrop =
		ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
		|| (Mouse.x >= WinMin.x && Mouse.x <= WinMin.x + WinSize.x
			&& Mouse.y >= WinMin.y && Mouse.y <= WinMin.y + WinSize.y);

	// -- toolbar: refresh + search ----------------------------------------------
	if (ImGui::Button("Refresh"))
	{
		Rescan();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##ContentBrowserSearch", "Search assets (name substring)...", SearchBuffer, IM_ARRAYSIZE(SearchBuffer));
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F5, false))
	{
		Rescan();
	}

	// -- breadcrumb: /Game/Textures/Env, every segment clickable -----------------
	{
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
				if (!bFirst)
				{
					ImGui::SameLine(0.0f, 0.0f);
					ImGui::TextUnformatted("/");
					ImGui::SameLine(0.0f, 0.0f);
				}
				ImGui::PushID(Accumulated.c_str());
				if (ImGui::SmallButton(Segment.c_str()))
				{
					CurrentPath = Accumulated;
				}
				ImGui::PopID();
				bFirst = false;
			}
			if (Slash == std::string::npos)
			{
				break;
			}
			Start = Slash + 1;
		}
	}

	// The scanned node for the current directory (falls back to the first root after a
	// rescan removed it -- e.g. the directory deleted while the panel was open).
	const FNode* Current = FindNode(CurrentPath);
	if (Current == nullptr && !Roots.empty())
	{
		CurrentPath = Roots.front().VirtualPath;
		Current = &Roots.front();
	}

	const float ListHeight = -ImGui::GetFrameHeightWithSpacing();

	// -- left pane: the directory tree ------------------------------------------
	ImGui::BeginChild("##ContentBrowserTree", ImVec2(220.0f, ListHeight), ImGuiChildFlags_Borders);
	for (const FNode& Root : Roots)
	{
		DrawTree(Root);
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// -- right pane: the items of the current directory (search-filtered) --------
	ImGui::BeginChild("##ContentBrowserItems", ImVec2(0.0f, ListHeight), ImGuiChildFlags_Borders);
	if (Current != nullptr)
	{
		DrawItems(*Current);
	}
	ImGui::EndChild();

	// -- status line: last scan / import result ---------------------------------
	ImGui::PushStyleColor(ImGuiCol_Text, bStatusIsError
		? ImVec4(0.90f, 0.35f, 0.35f, 1.0f)
		: ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
	ImGui::TextUnformatted(Status.empty() ? "-" : Status.c_str());
	ImGui::PopStyleColor();

	ImGui::End();

	if (bOwnsDrop && !Editor.GetDroppedFiles().empty())
	{
		ImportDroppedFiles(Editor);
	}
}

void FContentBrowser::DrawTree(const FNode& Node)
{
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (Node.bDirectory)
	{
		Flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	if (Node.VirtualPath == CurrentPath)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (Node.Children.empty())
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	ImGui::PushID(Node.VirtualPath.c_str());
	const bool bOpen = ImGui::TreeNodeEx("##node", Flags, "%s", Node.Name.c_str());
	// Clicking a directory's label browses into it (the arrow still folds the subtree).
	if (Node.bDirectory && ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		CurrentPath = Node.VirtualPath;
	}
	if (bOpen && !Node.Children.empty())
	{
		for (const FNode& Child : Node.Children)
		{
			DrawTree(Child);
		}
	}
	if (bOpen && !(Flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
	{
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void FContentBrowser::DrawItems(const FNode& Node)
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
		ImGui::TextUnformatted(bSearching ? "No matching asset." : "No asset here.");
		return;
	}

	// Cell grid: a fixed-size selectable per item, wrapped every N columns.
	constexpr float CellWidth = 150.0f;
	constexpr float CellHeight = 34.0f;
	const float AvailWidth = ImGui::GetContentRegionAvail().x;
	// (std::max) - parenthesized so the Windows max() macro cannot swallow the call.
	const int Columns = (std::max)(1, static_cast<int>(AvailWidth / (CellWidth + ImGui::GetStyle().ItemSpacing.x)));

	for (int i = 0; i < static_cast<int>(Items.size()); ++i)
	{
		const FNode& Item = *Items[i];
		if (i > 0 && i % Columns != 0)
		{
			ImGui::SameLine();
		}

		ImGui::PushID(Item.VirtualPath.c_str());
		const bool bSelected = (Item.VirtualPath == SelectedPath);
		const std::string Label = (Item.bDirectory ? "[+] " : "    ") + Item.Name;
		if (ImGui::Selectable(Label.c_str(), bSelected, 0, ImVec2(CellWidth, CellHeight)))
		{
			if (Item.bDirectory)
			{
				CurrentPath = Item.VirtualPath;
			}
			else
			{
				SelectedPath = Item.VirtualPath;
				SetStatus(false, Item.VirtualPath);
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", Item.VirtualPath.c_str());
		}
		// Asset drag source: the payload is this project's own id, the value is the
		// asset's virtual path, and the preview shows it. Nothing here consumes it.
		if (!Item.bDirectory && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(AssetDragPayload, Item.VirtualPath.c_str(), Item.VirtualPath.size());
			ImGui::TextUnformatted(Item.VirtualPath.c_str());
			ImGui::EndDragDropSource();
		}
		ImGui::PopID();
	}
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

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_CONTENTBROWSER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FContentBrowser::CreateLayer();
}
