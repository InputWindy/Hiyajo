#include "ImGuiTheme.h"

#include <imgui.h>

namespace Maho
{

namespace
{

[[nodiscard]] ImVec4 Rgba(int R, int G, int B, float A = 1.0f)
{
	return ImVec4(
		static_cast<float>(R) / 255.0f,
		static_cast<float>(G) / 255.0f,
		static_cast<float>(B) / 255.0f,
		A);
}

} // namespace

void ApplyMahoNightTheme()
{
	ImGuiStyle& Style = ImGui::GetStyle();

	Style.Alpha = 1.0f;
	Style.DisabledAlpha = 0.38f;
	Style.WindowPadding = ImVec2(8.0f, 8.0f);
	Style.WindowRounding = 8.0f; // align with Win11 main-window corner radius
	Style.WindowBorderSize = 1.0f; // floating / undocked window edge
	Style.WindowMinSize = ImVec2(96.0f, 48.0f);
	Style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	Style.WindowMenuButtonPosition = ImGuiDir_None;
	Style.ChildRounding = 8.0f;
	// Keep > 0 so non-padded child paths still work; panels that need padding
	// use ImGuiChildFlags_AlwaysUseWindowPadding and omit Borders (no outer stroke).
	Style.ChildBorderSize = 1.0f;
	Style.PopupRounding = 2.0f;
	Style.PopupBorderSize = 1.0f;
	Style.FramePadding = ImVec2(0.0f, 5.0f);
	Style.FrameRounding = 2.0f;
	Style.FrameBorderSize = 1.0f;
	Style.ItemSpacing = ImVec2(6.0f, 5.0f);
	Style.ItemInnerSpacing = ImVec2(5.0f, 3.0f);
	Style.CellPadding = ImVec2(5.0f, 3.0f);
	Style.IndentSpacing = 16.0f;
	Style.ColumnsMinSpacing = 4.0f;
	Style.ScrollbarSize = 12.0f;
	Style.ScrollbarRounding = 2.0f;
	Style.GrabMinSize = 10.0f;
	Style.GrabRounding = 2.0f;
	Style.TabRounding = 3.0f;
	Style.TabBorderSize = 0.0f;
	Style.TabBarBorderSize = 0.0f; // hide tab-bar bottom separator (uses TabSelected color)
	Style.TabBarOverlineSize = 2.0f;
	Style.TabCloseButtonMinWidthSelected = -1.0f;
	Style.TabCloseButtonMinWidthUnselected = 0.0f;
	Style.ColorButtonPosition = ImGuiDir_Right;
	Style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	Style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
	Style.SeparatorTextBorderSize = 1.0f;
	Style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
	Style.SeparatorTextPadding = ImVec2(10.0f, 2.0f);
	Style.DockingSeparatorSize = 2.0f; // dock gutter thickness (fill stays transparent via Border)

	// Chrome hierarchy:
	//   MenuBar -> chassis TabWell (dock gutters + tab strip) -> selected TabFrame (border color)
	const ImVec4 MenuBar = Rgba(12, 12, 14);
	const ImVec4 TabWell = Rgba(14, 14, 16);       // deepest chassis / dock gutters / tab strip bg
	const ImVec4 Panel = Rgba(38, 39, 43, 0.72f);  // translucent so desktop wallpaper shows through
	const ImVec4 Well = Rgba(26, 27, 30);
	const ImVec4 Raised = Rgba(52, 54, 60);
	const ImVec4 Hover = Rgba(66, 70, 78);
	const ImVec4 Pressed = Rgba(30, 31, 35);
	const ImVec4 EdgeStrong = Rgba(96, 100, 112);
	const ImVec4 EdgeSoft = Rgba(48, 50, 56);
	const ImVec4 Text = Rgba(236, 237, 240);
	const ImVec4 TextMuted = Rgba(124, 128, 138);

	ImVec4* Colors = Style.Colors;
	Colors[ImGuiCol_Text] = Text;
	Colors[ImGuiCol_TextDisabled] = TextMuted;
	Colors[ImGuiCol_WindowBg] = Panel;
	Colors[ImGuiCol_ChildBg] = Panel;
	Colors[ImGuiCol_PopupBg] = Rgba(28, 29, 33, 0.98f);
	// Docked-window outer border (RenderWindowOuterBorders). The dock gutter (splitter) is
	// chrome -- the chassis TabWell -- so the window edge must use the SAME dark chrome, or a
	// light 1px border gets drawn at each panel's edge and reads as a "border" line between
	// adjacent docked panels (Viewport | Console) at rest. TabWell keeps the border structure
	// (floating windows still have an edge) while blending into the dark gutter. Hover/active
	// feedback is independent (SeparatorHovered/SeparatorActive below), so resize drag stays
	// usable.
	Colors[ImGuiCol_Border] = TabWell;
	Colors[ImGuiCol_BorderShadow] = Rgba(0, 0, 0, 0.50f);
	Colors[ImGuiCol_FrameBg] = Well;
	Colors[ImGuiCol_FrameBgHovered] = Raised;
	Colors[ImGuiCol_FrameBgActive] = Pressed;
	// Docked tab strip (the dock node's title bar, drawn by DockNodeUpdate with the global
	// TitleBg). The chrome hierarchy makes this the chassis TabWell; the 0.75 alpha rendered a
	// hair lighter than the opaque gutter, so at rest the tab strip read as a faint lighter band
	// above the selected tab. Use the exact gutter color so the whole strip is uniform chrome.
	Colors[ImGuiCol_TitleBg] = TabWell;
	Colors[ImGuiCol_TitleBgActive] = TabWell;
	Colors[ImGuiCol_TitleBgCollapsed] = TabWell;
	Colors[ImGuiCol_MenuBarBg] = MenuBar;
	Colors[ImGuiCol_ScrollbarBg] = TabWell;
	Colors[ImGuiCol_ScrollbarGrab] = Raised;
	Colors[ImGuiCol_ScrollbarGrabHovered] = Hover;
	Colors[ImGuiCol_ScrollbarGrabActive] = EdgeStrong;
	Colors[ImGuiCol_CheckMark] = Rgba(150, 155, 164);
	Colors[ImGuiCol_SliderGrab] = Rgba(110, 114, 124);
	Colors[ImGuiCol_SliderGrabActive] = Rgba(150, 155, 164);
	Colors[ImGuiCol_Button] = Raised;
	Colors[ImGuiCol_ButtonHovered] = Hover;
	Colors[ImGuiCol_ButtonActive] = Pressed;
	Colors[ImGuiCol_Header] = Rgba(52, 54, 60, 0.75f);
	Colors[ImGuiCol_HeaderHovered] = EdgeSoft;
	Colors[ImGuiCol_HeaderActive] = EdgeStrong;
	// Dock separator rest state. The splitter's fill is the Separator color overlaid on a
	// WindowBg strip (SplitterBehavior). The dock gutter is CHROME (the chassis TabWell behind
	// the tab strip), so the rest state must be TabWell, not the panel color -- otherwise the
	// light WindowBg base strip + a light overlay reads as a bright "border" line between the
	// two panels at rest. TabWell makes the gutter blend into the dark tab strip above the
	// console; Hover/active stay the bright resize accents below.
	Colors[ImGuiCol_Separator] = TabWell;
	Colors[ImGuiCol_SeparatorHovered] = EdgeSoft;
	Colors[ImGuiCol_SeparatorActive] = EdgeStrong;
	Colors[ImGuiCol_ResizeGrip] = Rgba(255, 255, 255, 0.12f);
	// Dock splitter: the rest-state gutter uses Border (kept subtle); hover/active must be
	// BRIGHTER than that or the splitter reads inverted (rest brighter than hover). These also
	// tint window-edge resize feedback, which dockspace windows keep transparent so the change
	// stays scoped to the splitter.
	Colors[ImGuiCol_ResizeGripHovered] = Rgba(142, 147, 156);
	Colors[ImGuiCol_ResizeGripActive] = Rgba(172, 177, 186);
	// Tabs: the selected tab face = the docked panel's outer border (ImGuiCol_Border, drawn by the
	// dock HOST window = TabWell), so the tab is not brighter than the console frame it sits on.
	// Unselected tabs stay fully transparent so only their label shows against the darkest chassis.
	// Hover stays a hair lighter (Panel) so a hovered tab is still detectable without a bright block.
	Colors[ImGuiCol_Tab] = ImVec4(Panel.x, Panel.y, Panel.z, 0.0f);
	Colors[ImGuiCol_TabHovered] = Panel;
	Colors[ImGuiCol_TabSelected] = TabWell;
	Colors[ImGuiCol_TabSelectedOverline] = Panel;
	Colors[ImGuiCol_TabDimmed] = ImVec4(Panel.x, Panel.y, Panel.z, 0.0f);
	Colors[ImGuiCol_TabDimmedSelected] = TabWell;
	Colors[ImGuiCol_TabDimmedSelectedOverline] = Panel;
	Colors[ImGuiCol_DockingPreview] = Rgba(110, 114, 124, 0.30f);
	Colors[ImGuiCol_DockingEmptyBg] = Rgba(14, 14, 16, 0.0f); // let editor wallpaper show in empty dock areas
	Colors[ImGuiCol_PlotLines] = Rgba(150, 165, 190);
	Colors[ImGuiCol_PlotLinesHovered] = Rgba(170, 174, 182);
	Colors[ImGuiCol_PlotHistogram] = EdgeStrong;
	Colors[ImGuiCol_PlotHistogramHovered] = Rgba(170, 174, 182);
	Colors[ImGuiCol_TableHeaderBg] = TabWell;
	Colors[ImGuiCol_TableBorderStrong] = EdgeStrong;
	Colors[ImGuiCol_TableBorderLight] = EdgeSoft;
	Colors[ImGuiCol_TableRowBg] = Rgba(0, 0, 0, 0.0f);
	Colors[ImGuiCol_TableRowBgAlt] = Rgba(255, 255, 255, 0.025f);
	Colors[ImGuiCol_TextLink] = TextMuted;
	Colors[ImGuiCol_TextSelectedBg] = Rgba(110, 114, 124, 0.35f);
	Colors[ImGuiCol_DragDropTarget] = EdgeStrong;
	Colors[ImGuiCol_NavCursor] = Rgba(150, 155, 164);
	Colors[ImGuiCol_NavWindowingHighlight] = Rgba(255, 255, 255, 0.55f);
	Colors[ImGuiCol_NavWindowingDimBg] = Rgba(0, 0, 0, 0.70f);
	Colors[ImGuiCol_ModalWindowDimBg] = Rgba(0, 0, 0, 0.78f);
}

} // namespace Maho
