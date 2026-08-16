#pragma once

#include "Editor/EditorUITypes.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Maho
{

class FEngineBase;
class FEditorLayer;

/**
 * Region-based editor UI contribution registry (menus, toolbars, dock panels, modals).
 * Owned by FEditorLayer. Contributions are grouped by Catalog (separators between groups).
 */
class FEditorUIRegistry
{
public:
	FEditorUIRegistry() = default;

	void Clear();

	void RegisterMenuItem(FEditorMenuItemDesc Desc);
	void RegisterToolbarItem(FEditorToolbarItemDesc Desc);
	void RegisterDockPanel(FEditorDockPanelDesc Desc);
	void RegisterViewportOverlay(FEditorViewportOverlayDesc Desc);
	void RegisterModal(FEditorModalDesc Desc);
	void Unregister(std::string_view Id);

	void OpenDockPanel(std::string_view Id);
	void CloseDockPanel(std::string_view Id);
	void OpenModal(std::string_view Id);
	void CloseModal(std::string_view Id);

	void DrawMenuPopup(std::string_view CatalogName, FEditorUIDrawContext& Ctx);
	void DrawToolbar(EEditorUIRegion Region, FEditorUIDrawContext& Ctx);
	void DrawDockPanels(FEditorUIDrawContext& Ctx);
	void DrawViewportOverlays(FEditorUIDrawContext& Ctx);
	void DrawModals(FEditorUIDrawContext& Ctx);

	/** Window menu: catalog-grouped toggles for non-transient (and open transient) docks. */
	void DrawDockPanelMenuToggles(FEditorUIDrawContext& Ctx);

	[[nodiscard]] bool* FindDockOpenFlag(std::string_view Id);
	[[nodiscard]] const std::vector<FEditorDockPanelDesc>& GetDockPanels() const
	{
		return DockPanels;
	}

	/** Top-level menu catalogs (File / Window / Help / …) sorted for the shell. */
	[[nodiscard]] std::vector<FEditorUICatalog> GetMenuCatalogs() const;

private:
	template <typename T>
	static void SortByCatalogThenOrder(std::vector<T>& Items);

	std::vector<FEditorMenuItemDesc> MenuItems;
	std::vector<FEditorToolbarItemDesc> ToolbarItems;
	std::vector<FEditorDockPanelDesc> DockPanels;
	std::vector<FEditorViewportOverlayDesc> ViewportOverlays;
	std::vector<FEditorModalDesc> Modals;
	std::unordered_map<std::string, bool> ModalOpen;
};

[[nodiscard]] FEditorUIRegistry* TryGetEditorUIRegistry(FEngineBase& App);

} // namespace Maho
