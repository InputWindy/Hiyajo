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

/** Shift a color toward white (t>0) or toward black (t<0) by a fixed amount.
 *  The whole theme derives every state color from the three palette knobs via
 *  this single rule, so retuning the palette retunes every widget at once. */
[[nodiscard]] ImVec4 Shade(const ImVec4& C, float T)
{
	T = T > 1.0f ? 1.0f : (T < -1.0f ? -1.0f : T);
	if (T >= 0.0f)
	{
		const float K = 1.0f - T;
		return ImVec4(C.x + (1.0f - C.x) * T, C.y + (1.0f - C.y) * T, C.z + (1.0f - C.z) * T, C.w);
	}
	else
	{
		const float K = 1.0f + T;
		return ImVec4(C.x * K, C.y * K, C.z * K, C.w);
	}
}

// ── 3-knob palette ─────────────────────────────────────────────────────
// The entire editor deploys exactly three colors. Hover / active / pressed /
// selected are derived from `Accent` by Shade() only -- no per-widget hue.
//   Bg     black background (window faces, tab strip backplate, panels)
//   Border deep-grey component border / recess (panels edge, dock gutter, ...)
//   Accent bright-grey buttons / emphasis / readable text
const ImVec4 Bg     = Rgba(18, 19, 22);
const ImVec4 Border = Rgba(54, 56, 63);
const ImVec4 Accent = Rgba(150, 155, 166);

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
	Style.ItemInnerSpacing = ImVec2(2.0f, 3.0f);
	Style.CellPadding = ImVec2(5.0f, 3.0f);
	Style.IndentSpacing = 16.0f;
	Style.ColumnsMinSpacing = 4.0f;
	Style.ScrollbarSize = 12.0f;
	Style.ScrollbarRounding = 2.0f;
	Style.GrabMinSize = 10.0f;
	Style.GrabRounding = 2.0f;
	// The selected tab keeps its rounded top (a classic "active page" tab shape).
	// Because BOTH the dock tab strip backplate (TitleBg) and the selected-tab fill
	// (TabSelected) are the SAME Bg color, the rounded top-left corner reveals nothing
	// but the background -- no dark notch, no left seam. (The notch appears only when
	// the backplate is a DIFFERENT color than the fill; keeping both = background makes
	// the seam invisible.)
	Style.TabRounding = 3.0f;
	Style.TabBorderSize = 0.0f;
	Style.TabBarBorderSize = 0.0f; // hide tab-bar bottom separator (uses overline color)
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

	ImVec4* Colors = Style.Colors;

	// Text / emphasis.
	Colors[ImGuiCol_Text] = Accent;
	Colors[ImGuiCol_TextDisabled] = Shade(Accent, -0.35f);
	Colors[ImGuiCol_TextLink] = Accent;
	Colors[ImGuiCol_TextSelectedBg] = Shade(Accent, -0.75f);
	Colors[ImGuiCol_CheckMark] = Accent;

	// Faces -- all black background.
	Colors[ImGuiCol_WindowBg] = Bg;
	Colors[ImGuiCol_ChildBg] = Bg;
	Colors[ImGuiCol_PopupBg] = Bg;
	Colors[ImGuiCol_MenuBarBg] = Bg;
	Colors[ImGuiCol_TableHeaderBg] = Bg;
	// Docked tab strip backplate == Bg, and the selected-tab fill == Bg, so the
	// rounded corner merges into the backplate (no seam). See TabRounding note.
	Colors[ImGuiCol_TitleBg] = Bg;
	Colors[ImGuiCol_TitleBgActive] = Bg;
	Colors[ImGuiCol_TitleBgCollapsed] = Bg;

	// Border / recess.
	Colors[ImGuiCol_Border] = Border;
	Colors[ImGuiCol_BorderShadow] = Rgba(0, 0, 0, 0.50f);
	Colors[ImGuiCol_Separator] = Border;
	Colors[ImGuiCol_SeparatorHovered] = Shade(Accent, -0.15f);
	Colors[ImGuiCol_SeparatorActive] = Accent;
	Colors[ImGuiCol_TableBorderStrong] = Accent;
	Colors[ImGuiCol_TableBorderLight] = Border;
	Colors[ImGuiCol_TableRowBg] = Rgba(0, 0, 0, 0.0f);
	Colors[ImGuiCol_TableRowBgAlt] = Shade(Accent, -0.85f);

	// Input frames (recessed inside a panel).
	Colors[ImGuiCol_FrameBg] = Shade(Accent, -0.62f);
	Colors[ImGuiCol_FrameBgHovered] = Shade(Accent, -0.45f);
	Colors[ImGuiCol_FrameBgActive] = Shade(Accent, -0.35f);

	// Interactive / emphasis faces (buttons, headers, grabbers) -- all derived
	// from Accent via Shade() so one knob retunes them together.
	Colors[ImGuiCol_Button] = Shade(Accent, -0.55f);
	Colors[ImGuiCol_ButtonHovered] = Shade(Accent, -0.35f);
	Colors[ImGuiCol_ButtonActive] = Shade(Accent, -0.70f);
	Colors[ImGuiCol_Header] = Shade(Accent, -0.60f);
	Colors[ImGuiCol_HeaderHovered] = Shade(Accent, -0.40f);
	Colors[ImGuiCol_HeaderActive] = Shade(Accent, -0.20f);
	Colors[ImGuiCol_ScrollbarBg] = Border;
	Colors[ImGuiCol_ScrollbarGrab] = Shade(Accent, -0.45f);
	Colors[ImGuiCol_ScrollbarGrabHovered] = Shade(Accent, -0.25f);
	Colors[ImGuiCol_ScrollbarGrabActive] = Accent;
	Colors[ImGuiCol_SliderGrab] = Accent;
	Colors[ImGuiCol_SliderGrabActive] = Shade(Accent, +0.10f);
	Colors[ImGuiCol_ResizeGrip] = Rgba(255, 255, 255, 0.10f);
	Colors[ImGuiCol_ResizeGripHovered] = Accent;
	Colors[ImGuiCol_ResizeGripActive] = Accent;

	// Tabs. Non-selected = fully transparent (label only). The selected/hover face
	// is the SAME Bg as the backplate so the rounded top merges seamlessly.
	Colors[ImGuiCol_Tab] = ImVec4(Bg.x, Bg.y, Bg.z, 0.0f);
	Colors[ImGuiCol_TabHovered] = Bg;
	Colors[ImGuiCol_TabSelected] = Bg;
	Colors[ImGuiCol_TabSelectedOverline] = Accent;
	Colors[ImGuiCol_TabDimmed] = ImVec4(Bg.x, Bg.y, Bg.z, 0.0f);
	Colors[ImGuiCol_TabDimmedSelected] = Bg;
	Colors[ImGuiCol_TabDimmedSelectedOverline] = Accent;

	// Docking preview / empty area.
	Colors[ImGuiCol_DockingPreview] = Rgba(Accent.x, Accent.y, Accent.z, 0.30f);
	Colors[ImGuiCol_DockingEmptyBg] = ImVec4(Bg.x, Bg.y, Bg.z, 0.0f);

	// Diagnostics / overlays.
	Colors[ImGuiCol_PlotLines] = Shade(Accent, -0.20f);
	Colors[ImGuiCol_PlotLinesHovered] = Accent;
	Colors[ImGuiCol_PlotHistogram] = Accent;
	Colors[ImGuiCol_PlotHistogramHovered] = Accent;
	Colors[ImGuiCol_DragDropTarget] = Accent;
	Colors[ImGuiCol_NavCursor] = Accent;
	Colors[ImGuiCol_NavWindowingHighlight] = Rgba(255, 255, 255, 0.55f);
	Colors[ImGuiCol_NavWindowingDimBg] = Rgba(0, 0, 0, 0.70f);
	Colors[ImGuiCol_ModalWindowDimBg] = Rgba(0, 0, 0, 0.78f);
}

} // namespace Maho
