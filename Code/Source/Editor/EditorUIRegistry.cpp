#include "Editor/EditorUIRegistry.h"

#include <Core/EngineBase.h>
#include <Core/Misc/Log.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <utility>

namespace Maho
{

namespace
{

template <typename T>
void EraseById(std::vector<T>& Items, std::string_view Id)
{
	Items.erase(
		std::remove_if(
			Items.begin(),
			Items.end(),
			[&](const T& Item)
			{
				return Item.Id == Id;
			}),
		Items.end());
}

[[nodiscard]] bool ValidateCatalog(const FEditorUICatalog& Catalog, std::string_view Kind, std::string_view Id)
{
	if (Catalog.Name.empty())
	{
		MAHO_ERROR("FEditorUIRegistry: refusing {} '{}' — empty Catalog.Name", Kind, Id);
		return false;
	}
	return true;
}

} // namespace

template <typename T>
void FEditorUIRegistry::SortByCatalogThenOrder(std::vector<T>& Items)
{
	std::stable_sort(Items.begin(), Items.end(), [](const T& A, const T& B)
	{
		if (A.Catalog.Order != B.Catalog.Order)
		{
			return A.Catalog.Order < B.Catalog.Order;
		}
		if (A.Catalog.Name != B.Catalog.Name)
		{
			return A.Catalog.Name < B.Catalog.Name;
		}
		if (A.Order != B.Order)
		{
			return A.Order < B.Order;
		}
		return A.Id < B.Id;
	});
}

void FEditorUIRegistry::Clear()
{
	MenuItems.clear();
	ToolbarItems.clear();
	DockPanels.clear();
	ViewportOverlays.clear();
	Modals.clear();
	ModalOpen.clear();
}

void FEditorUIRegistry::RegisterMenuItem(FEditorMenuItemDesc Desc)
{
	if (!ValidateCatalog(Desc.Catalog, "menu item", Desc.Id))
	{
		return;
	}
	EraseById(MenuItems, Desc.Id);
	MenuItems.push_back(std::move(Desc));
	SortByCatalogThenOrder(MenuItems);
}

void FEditorUIRegistry::RegisterToolbarItem(FEditorToolbarItemDesc Desc)
{
	if (!ValidateCatalog(Desc.Catalog, "toolbar item", Desc.Id))
	{
		return;
	}
	if (Desc.Region != EEditorUIRegion::ToolbarPrimary && Desc.Region != EEditorUIRegion::ToolbarSecondary)
	{
		MAHO_ERROR("FEditorUIRegistry: toolbar item '{}' has invalid Region", Desc.Id);
		return;
	}
	EraseById(ToolbarItems, Desc.Id);
	ToolbarItems.push_back(std::move(Desc));
	SortByCatalogThenOrder(ToolbarItems);
}

void FEditorUIRegistry::RegisterDockPanel(FEditorDockPanelDesc Desc)
{
	if (!ValidateCatalog(Desc.Catalog, "dock panel", Desc.Id))
	{
		return;
	}
	EraseById(DockPanels, Desc.Id);
	DockPanels.push_back(std::move(Desc));
	SortByCatalogThenOrder(DockPanels);
}

void FEditorUIRegistry::RegisterViewportOverlay(FEditorViewportOverlayDesc Desc)
{
	if (!ValidateCatalog(Desc.Catalog, "viewport overlay", Desc.Id))
	{
		return;
	}
	EraseById(ViewportOverlays, Desc.Id);
	ViewportOverlays.push_back(std::move(Desc));
	SortByCatalogThenOrder(ViewportOverlays);
}

void FEditorUIRegistry::RegisterModal(FEditorModalDesc Desc)
{
	if (!ValidateCatalog(Desc.Catalog, "modal", Desc.Id))
	{
		return;
	}
	EraseById(Modals, Desc.Id);
	ModalOpen.try_emplace(Desc.Id, false);
	Modals.push_back(std::move(Desc));
	SortByCatalogThenOrder(Modals);
}

void FEditorUIRegistry::Unregister(std::string_view Id)
{
	EraseById(MenuItems, Id);
	EraseById(ToolbarItems, Id);
	EraseById(DockPanels, Id);
	EraseById(ViewportOverlays, Id);
	EraseById(Modals, Id);
	ModalOpen.erase(std::string(Id));
}

void FEditorUIRegistry::OpenDockPanel(std::string_view Id)
{
	for (FEditorDockPanelDesc& Panel : DockPanels)
	{
		if (Panel.Id == Id && Panel.bOpen)
		{
			*Panel.bOpen = true;
			return;
		}
	}
}

void FEditorUIRegistry::CloseDockPanel(std::string_view Id)
{
	for (FEditorDockPanelDesc& Panel : DockPanels)
	{
		if (Panel.Id == Id && Panel.bOpen)
		{
			*Panel.bOpen = false;
			return;
		}
	}
}

void FEditorUIRegistry::OpenModal(std::string_view Id)
{
	auto It = ModalOpen.find(std::string(Id));
	if (It != ModalOpen.end())
	{
		It->second = true;
	}
}

void FEditorUIRegistry::CloseModal(std::string_view Id)
{
	auto It = ModalOpen.find(std::string(Id));
	if (It != ModalOpen.end())
	{
		It->second = false;
	}
}

std::vector<FEditorUICatalog> FEditorUIRegistry::GetMenuCatalogs() const
{
	std::vector<FEditorUICatalog> Out;
	for (const FEditorMenuItemDesc& Item : MenuItems)
	{
		bool bFound = false;
		for (const FEditorUICatalog& C : Out)
		{
			if (C.Name == Item.Catalog.Name)
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			Out.push_back(Item.Catalog);
		}
	}
	std::stable_sort(Out.begin(), Out.end(), [](const FEditorUICatalog& A, const FEditorUICatalog& B)
	{
		if (A.Order != B.Order)
		{
			return A.Order < B.Order;
		}
		return A.Name < B.Name;
	});
	return Out;
}

void FEditorUIRegistry::DrawMenuPopup(std::string_view CatalogName, FEditorUIDrawContext& Ctx)
{
	std::string PrevCatalog;
	bool bAny = false;
	for (const FEditorMenuItemDesc& Item : MenuItems)
	{
		if (Item.Catalog.Name != CatalogName || !Item.Draw)
		{
			continue;
		}
		if (bAny && Item.Catalog.Name != PrevCatalog)
		{
			ImGui::Separator();
		}
		Item.Draw(Ctx);
		PrevCatalog = Item.Catalog.Name;
		bAny = true;
	}
}

void FEditorUIRegistry::DrawToolbar(EEditorUIRegion Region, FEditorUIDrawContext& Ctx)
{
	const float Spacing = (Region == EEditorUIRegion::ToolbarSecondary) ? 0.0f : -1.0f;
	std::string PrevCatalog;
	bool bAny = false;
	for (const FEditorToolbarItemDesc& Item : ToolbarItems)
	{
		if (Item.Region != Region || !Item.Draw)
		{
			continue;
		}
		if (bAny)
		{
			if (Item.Catalog.Name != PrevCatalog)
			{
				ImGui::SameLine(0.0f, Spacing);
				ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
				ImGui::SameLine(0.0f, Spacing);
			}
			else
			{
				ImGui::SameLine(0.0f, Spacing);
			}
		}
		Item.Draw(Ctx);
		PrevCatalog = Item.Catalog.Name;
		bAny = true;
	}
}

void FEditorUIRegistry::DrawDockPanels(FEditorUIDrawContext& Ctx)
{
	for (const FEditorDockPanelDesc& Panel : DockPanels)
	{
		if (!Panel.Draw || !Panel.bOpen || !*Panel.bOpen)
		{
			continue;
		}
		Panel.Draw(Ctx);
	}
}

void FEditorUIRegistry::DrawViewportOverlays(FEditorUIDrawContext& Ctx)
{
	std::string PrevCatalog;
	bool bAny = false;
	for (const FEditorViewportOverlayDesc& Item : ViewportOverlays)
	{
		if (!Item.Draw)
		{
			continue;
		}
		if (bAny && Item.Catalog.Name != PrevCatalog)
		{
			ImGui::Separator();
		}
		Item.Draw(Ctx);
		PrevCatalog = Item.Catalog.Name;
		bAny = true;
	}
}

void FEditorUIRegistry::DrawModals(FEditorUIDrawContext& Ctx)
{
	for (const FEditorModalDesc& Modal : Modals)
	{
		auto It = ModalOpen.find(Modal.Id);
		if (It == ModalOpen.end() || !It->second || !Modal.Draw)
		{
			continue;
		}
		if (!ImGui::IsPopupOpen(Modal.Title.c_str()))
		{
			ImGui::OpenPopup(Modal.Title.c_str());
		}
		const ImVec2 Center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(Center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(Modal.Title.c_str(), &It->second, ImGuiWindowFlags_AlwaysAutoResize))
		{
			Modal.Draw(Ctx);
			ImGui::EndPopup();
		}
	}
}

void FEditorUIRegistry::DrawDockPanelMenuToggles(FEditorUIDrawContext& Ctx)
{
	(void)Ctx;
	std::string PrevCatalog;
	bool bAny = false;
	for (const FEditorDockPanelDesc& Panel : DockPanels)
	{
		if (!Panel.bOpen)
		{
			continue;
		}
		if (!bAny || Panel.Catalog.Name != PrevCatalog)
		{
			if (bAny)
			{
				ImGui::Spacing();
			}
			// "—— Catalog ——" : label centered in the separator line.
			ImGui::SetWindowFontScale(0.75f);
			ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.5f, 0.5f));
			ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(8.0f, 2.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.74f, 1.0f));
			ImGui::SeparatorText(Panel.Catalog.Name.c_str());
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
			ImGui::SetWindowFontScale(1.0f);
		}
		ImGui::MenuItem(Panel.Title.c_str(), nullptr, Panel.bOpen);
		PrevCatalog = Panel.Catalog.Name;
		bAny = true;
	}
}

bool* FEditorUIRegistry::FindDockOpenFlag(std::string_view Id)
{
	for (FEditorDockPanelDesc& Panel : DockPanels)
	{
		if (Panel.Id == Id)
		{
			return Panel.bOpen;
		}
	}
	return nullptr;
}

} // namespace Maho
