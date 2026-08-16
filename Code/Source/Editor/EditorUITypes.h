#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace Maho
{

class FEngineBase;
class FEditorLayer;
class FEditorUIRegistry;

enum class EEditorUIRegion : std::uint8_t
{
	MainMenu = 0,
	ToolbarPrimary,
	ToolbarSecondary,
	DockPanel,
	CentralViewport,
	Modal,
};

struct FEditorUICatalog
{
	std::string Name;
	std::int32_t Order = 0;
};

struct FEditorUIDrawContext
{
	FEngineBase* App = nullptr;
	FEditorLayer* Editor = nullptr;
	FEditorUIRegistry* Registry = nullptr;
	/** Secondary toolbar square button size (height of strip). */
	float ToolbarButtonSize = 0.0f;
};

using FEditorUIDrawFn = std::function<void(FEditorUIDrawContext&)>;

struct FEditorMenuItemDesc
{
	FEditorUICatalog Catalog;
	std::string Id;
	std::string Label;
	std::int32_t Order = 0;
	FEditorUIDrawFn Draw;
};

struct FEditorToolbarItemDesc
{
	EEditorUIRegion Region = EEditorUIRegion::ToolbarPrimary;
	FEditorUICatalog Catalog;
	std::string Id;
	std::int32_t Order = 0;
	FEditorUIDrawFn Draw;
};

struct FEditorDockPanelDesc
{
	FEditorUICatalog Catalog;
	std::string Id;
	std::string Title;
	bool* bOpen = nullptr;
	bool bDefaultOpen = true;
	bool bTransient = false;
	std::int32_t Order = 0;
	FEditorUIDrawFn Draw;
};

struct FEditorViewportOverlayDesc
{
	FEditorUICatalog Catalog;
	std::string Id;
	std::int32_t Order = 0;
	FEditorUIDrawFn Draw;
};

struct FEditorModalDesc
{
	FEditorUICatalog Catalog;
	std::string Id;
	std::string Title;
	std::int32_t Order = 0;
	FEditorUIDrawFn Draw;
};

} // namespace Maho
