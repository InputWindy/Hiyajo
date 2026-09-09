#include "ImGuiTheme.h"
#include "ExampleEditorTheme.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace Maho
{

namespace
{

/** A single editable color slot: the live ImGui col id + UI label + group. */
struct FThemeEntry
{
	ImGuiCol Id;
	const char* Name;
	const char* Group;
};

/** The editable color surface exposed to the theme panel. Index order is the
 *  stable, public flat-buffer order (see GetEditorThemeColorCount). */
const FThemeEntry GEntries[] = {
	// Text.
	{ ImGuiCol_Text, "Text", "Text" },
	{ ImGuiCol_TextDisabled, "TextDisabled", "Text" },
	{ ImGuiCol_TextLink, "TextLink", "Text" },
	{ ImGuiCol_TextSelectedBg, "TextSelectedBg", "Text" },
	{ ImGuiCol_CheckMark, "CheckMark", "Text" },

	// Faces -- the black backgrounds.
	{ ImGuiCol_WindowBg, "WindowBg", "Faces" },
	{ ImGuiCol_ChildBg, "ChildBg", "Faces" },
	{ ImGuiCol_PopupBg, "PopupBg", "Faces" },
	{ ImGuiCol_MenuBarBg, "MenuBarBg", "Faces" },
	{ ImGuiCol_TableHeaderBg, "TableHeaderBg", "Faces" },
	{ ImGuiCol_TitleBg, "TitleBg", "Faces" },
	{ ImGuiCol_TitleBgActive, "TitleBgActive", "Faces" },
	{ ImGuiCol_TitleBgCollapsed, "TitleBgCollapsed", "Faces" },

	// Border / recess.
	{ ImGuiCol_Border, "Border", "Borders" },
	{ ImGuiCol_BorderShadow, "BorderShadow", "Borders" },
	{ ImGuiCol_Separator, "Separator", "Borders" },
	{ ImGuiCol_SeparatorHovered, "SeparatorHovered", "Borders" },
	{ ImGuiCol_SeparatorActive, "SeparatorActive", "Borders" },
	{ ImGuiCol_TableBorderStrong, "TableBorderStrong", "Borders" },
	{ ImGuiCol_TableBorderLight, "TableBorderLight", "Borders" },
	{ ImGuiCol_TableRowBg, "TableRowBg", "Borders" },
	{ ImGuiCol_TableRowBgAlt, "TableRowBgAlt", "Borders" },

	// Input frames (recessed inside a panel).
	{ ImGuiCol_FrameBg, "FrameBg", "Input frames" },
	{ ImGuiCol_FrameBgHovered, "FrameBgHovered", "Input frames" },
	{ ImGuiCol_FrameBgActive, "FrameBgActive", "Input frames" },

	// Interactive / emphasis faces.
	{ ImGuiCol_Button, "Button", "Interactive" },
	{ ImGuiCol_ButtonHovered, "ButtonHovered", "Interactive" },
	{ ImGuiCol_ButtonActive, "ButtonActive", "Interactive" },
	{ ImGuiCol_Header, "Header", "Interactive" },
	{ ImGuiCol_HeaderHovered, "HeaderHovered", "Interactive" },
	{ ImGuiCol_HeaderActive, "HeaderActive", "Interactive" },
	{ ImGuiCol_ScrollbarBg, "ScrollbarBg", "Interactive" },
	{ ImGuiCol_ScrollbarGrab, "ScrollbarGrab", "Interactive" },
	{ ImGuiCol_ScrollbarGrabHovered, "ScrollbarGrabHovered", "Interactive" },
	{ ImGuiCol_ScrollbarGrabActive, "ScrollbarGrabActive", "Interactive" },
	{ ImGuiCol_SliderGrab, "SliderGrab", "Interactive" },
	{ ImGuiCol_SliderGrabActive, "SliderGrabActive", "Interactive" },
	{ ImGuiCol_ResizeGrip, "ResizeGrip", "Interactive" },
	{ ImGuiCol_ResizeGripHovered, "ResizeGripHovered", "Interactive" },
	{ ImGuiCol_ResizeGripActive, "ResizeGripActive", "Interactive" },

	// Tabs.
	{ ImGuiCol_Tab, "Tab", "Tabs" },
	{ ImGuiCol_TabHovered, "TabHovered", "Tabs" },
	{ ImGuiCol_TabSelected, "TabSelected", "Tabs" },
	{ ImGuiCol_TabSelectedOverline, "TabSelectedOverline", "Tabs" },
	{ ImGuiCol_TabDimmed, "TabDimmed", "Tabs" },
	{ ImGuiCol_TabDimmedSelected, "TabDimmedSelected", "Tabs" },
	{ ImGuiCol_TabDimmedSelectedOverline, "TabDimmedSelectedOverline", "Tabs" },

	// Docking.
	{ ImGuiCol_DockingPreview, "DockingPreview", "Docking" },
	{ ImGuiCol_DockingEmptyBg, "DockingEmptyBg", "Docking" },

	// Plot.
	{ ImGuiCol_PlotLines, "PlotLines", "Plot" },
	{ ImGuiCol_PlotLinesHovered, "PlotLinesHovered", "Plot" },
	{ ImGuiCol_PlotHistogram, "PlotHistogram", "Plot" },
	{ ImGuiCol_PlotHistogramHovered, "PlotHistogramHovered", "Plot" },

	// Diagnostics / overlays.
	{ ImGuiCol_DragDropTarget, "DragDropTarget", "Overlays" },
	{ ImGuiCol_NavCursor, "NavCursor", "Overlays" },
	{ ImGuiCol_NavWindowingHighlight, "NavWindowingHighlight", "Overlays" },
	{ ImGuiCol_NavWindowingDimBg, "NavWindowingDimBg", "Overlays" },
	{ ImGuiCol_ModalWindowDimBg, "ModalWindowDimBg", "Overlays" },
};

constexpr int GEntryCount = static_cast<int>(sizeof(GEntries) / sizeof(GEntries[0]));

/** A single editable numeric style slot: layout info + group. Arity = floats. */
struct FStyleEntry
{
	const char* Name;
	const char* Group;
	int         Arity;
};

/** The editable numeric style surface (rounding / padding / spacing ...).
 *  Index order is the stable, public flat-buffer order. Scalar fields use
 *  only v0; two-vector fields use v0,v1. Enum-style fields (window menu
 *  button position, color button position) are not exposed. */
const FStyleEntry GStyles[] = {
	// General.
	{ "Alpha", "General", 1 },
	{ "DisabledAlpha", "General", 1 },
	{ "WindowPadding", "General", 2 },
	{ "WindowRounding", "General", 1 },
	{ "WindowBorderSize", "General", 1 },
	{ "WindowMinSize", "General", 2 },
	{ "DockingSeparatorSize", "General", 1 },

	// Child / popup.
	{ "ChildRounding", "Child", 1 },
	{ "ChildBorderSize", "Child", 1 },
	{ "PopupRounding", "Popup", 1 },
	{ "PopupBorderSize", "Popup", 1 },

	// Frame.
	{ "FramePadding", "Frame", 2 },
	{ "FrameRounding", "Frame", 1 },
	{ "FrameBorderSize", "Frame", 1 },

	// Spacing.
	{ "ItemSpacing", "Spacing", 2 },
	{ "ItemInnerSpacing", "Spacing", 2 },
	{ "CellPadding", "Spacing", 2 },
	{ "IndentSpacing", "Spacing", 1 },
	{ "ColumnsMinSpacing", "Spacing", 1 },

	// Scrollbar / grab.
	{ "ScrollbarSize", "Scrollbar", 1 },
	{ "ScrollbarRounding", "Scrollbar", 1 },
	{ "GrabMinSize", "Grab", 1 },
	{ "GrabRounding", "Grab", 1 },

	// Tabs.
	{ "TabRounding", "Tabs", 1 },
	{ "TabBorderSize", "Tabs", 1 },
	{ "TabBarBorderSize", "Tabs", 1 },
	{ "TabBarOverlineSize", "Tabs", 1 },
	{ "TabCloseButtonMinWidthSelected", "Tabs", 1 },
	{ "TabCloseButtonMinWidthUnselected", "Tabs", 1 },

	// Text align.
	{ "ButtonTextAlign", "Text align", 2 },
	{ "SelectableTextAlign", "Text align", 2 },

	// Separator.
	{ "SeparatorTextBorderSize", "Separator", 1 },
	{ "SeparatorTextAlign", "Separator", 2 },
	{ "SeparatorTextPadding", "Separator", 2 },
};

constexpr int GStyleCount = static_cast<int>(sizeof(GStyles) / sizeof(GStyles[0]));

/** Built-in style defaults as a flat [GStyleCount*2] buffer. */
[[nodiscard]] std::array<float, GStyleCount * 2> BuildDefaultStyles()
{
	std::array<float, GStyleCount * 2> S = {};
	S[0] = 1.0f;                  // Alpha
	S[2] = 0.38f;                 // DisabledAlpha
	S[4] = 5.0f; S[5] = 5.0f;     // WindowPadding
	S[6] = 0.0f;                  // WindowRounding
	S[8] = 0.0f;                  // WindowBorderSize
	S[10] = 95.0f; S[11] = 48.0f; // WindowMinSize
	S[12] = 4.0f;                 // DockingSeparatorSize
	S[14] = 0.0f;                 // ChildRounding
	S[16] = 0.0f;                 // ChildBorderSize
	S[18] = 2.0f;                 // PopupRounding
	S[20] = 1.0f;                 // PopupBorderSize
	S[22] = 0.0f; S[23] = 5.0f;   // FramePadding
	S[24] = 2.0f;                 // FrameRounding
	S[26] = 0.0f;                 // FrameBorderSize
	S[28] = 5.0f; S[29] = 5.0f;   // ItemSpacing
	S[30] = 1.0f; S[31] = 1.0f;   // ItemInnerSpacing
	S[32] = 5.0f; S[33] = 5.0f;   // CellPadding
	S[34] = 16.0f;                // IndentSpacing
	S[36] = 4.0f;                 // ColumnsMinSpacing
	S[38] = 15.0f;                // ScrollbarSize
	S[40] = 1.0f;                 // ScrollbarRounding
	S[42] = 40.0f;                // GrabMinSize
	S[44] = 2.0f;                 // GrabRounding
	S[46] = 3.0f;                 // TabRounding
	S[48] = 0.0f;                 // TabBorderSize
	S[50] = 0.0f;                 // TabBarBorderSize
	S[52] = 2.0f;                 // TabBarOverlineSize
	S[54] = -1.0f;                // TabCloseButtonMinWidthSelected
	S[56] = 0.0f;                 // TabCloseButtonMinWidthUnselected
	S[58] = 0.5f; S[59] = 0.5f;   // ButtonTextAlign
	S[60] = 0.0f; S[61] = 0.0f;   // SelectableTextAlign
	S[62] = 1.0f;                 // SeparatorTextBorderSize
	S[64] = 0.0f; S[65] = 0.5f;   // SeparatorTextAlign
	S[66] = 10.0f; S[67] = 2.0f;  // SeparatorTextPadding
	return S;
}

/** The built-in style defaults, built once. */
const float* DefaultStyles()
{
	static const std::array<float, GStyleCount * 2> S = BuildDefaultStyles();
	return S.data();
}

[[nodiscard]] std::array<ImVec4, ImGuiCol_COUNT> BuildDefaultColors()
{
	std::array<ImVec4, ImGuiCol_COUNT> C = {};

	// Defaults = the exact near-black gray theme the user tuned in the theme panel
	// (Saved/editor_theme.theme). Snapshotted here so a fresh install boots into
	// the same look even with no theme file present. Values are RGBA copies.
	C[ImGuiCol_Text] = ImVec4(1.000000f, 1.000000f, 1.000000f, 1.000000f);
	C[ImGuiCol_TextDisabled] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TextLink] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TextSelectedBg] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_CheckMark] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);

	// Faces -- near-black backgrounds.
	C[ImGuiCol_WindowBg] = ImVec4(0.044053f, 0.044053f, 0.044053f, 1.000000f);
	C[ImGuiCol_ChildBg] = ImVec4(0.026432f, 0.026432f, 0.026432f, 1.000000f);
	C[ImGuiCol_PopupBg] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_MenuBarBg] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TableHeaderBg] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TitleBg] = ImVec4(0.026432f, 0.026432f, 0.026432f, 1.000000f);
	C[ImGuiCol_TitleBgActive] = ImVec4(0.026432f, 0.026432f, 0.026432f, 1.000000f);
	C[ImGuiCol_TitleBgCollapsed] = ImVec4(0.026432f, 0.026432f, 0.026432f, 1.000000f);

	// Border / recess.
	C[ImGuiCol_Border] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_BorderShadow] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_Separator] = ImVec4(0.026432f, 0.026432f, 0.026432f, 1.000000f);
	C[ImGuiCol_SeparatorHovered] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_SeparatorActive] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TableBorderStrong] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TableBorderLight] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TableRowBg] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TableRowBgAlt] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);

	// Input frames (recessed inside a panel).
	C[ImGuiCol_FrameBg] = ImVec4(0.026432f, 0.026315f, 0.026315f, 1.000000f);
	C[ImGuiCol_FrameBgHovered] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_FrameBgActive] = ImVec4(0.058824f, 0.058824f, 0.058824f, 1.000000f);

	// Interactive / emphasis faces.
	C[ImGuiCol_Button] = ImVec4(0.083700f, 0.083700f, 0.083700f, 1.000000f);
	C[ImGuiCol_ButtonHovered] = ImVec4(0.083700f, 0.083700f, 0.083700f, 1.000000f);
	C[ImGuiCol_ButtonActive] = ImVec4(0.158590f, 0.155796f, 0.155796f, 1.000000f);
	C[ImGuiCol_Header] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_HeaderHovered] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_HeaderActive] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_ScrollbarBg] = ImVec4(0.066079f, 0.066079f, 0.066079f, 1.000000f);
	C[ImGuiCol_ScrollbarGrab] = ImVec4(0.105727f, 0.105727f, 0.105727f, 1.000000f);
	C[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.105727f, 0.105727f, 0.105727f, 1.000000f);
	C[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.154185f, 0.154185f, 0.154185f, 1.000000f);
	C[ImGuiCol_SliderGrab] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_SliderGrabActive] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_ResizeGrip] = ImVec4(0.105727f, 0.105727f, 0.105727f, 1.000000f);
	C[ImGuiCol_ResizeGripHovered] = ImVec4(0.105727f, 0.105727f, 0.105727f, 1.000000f);
	C[ImGuiCol_ResizeGripActive] = ImVec4(0.154185f, 0.154185f, 0.154185f, 1.000000f);

	// Tabs.
	C[ImGuiCol_Tab] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TabHovered] = ImVec4(0.044053f, 0.044053f, 0.044053f, 1.000000f);
	C[ImGuiCol_TabSelected] = ImVec4(0.044053f, 0.044053f, 0.044053f, 1.000000f);
	C[ImGuiCol_TabSelectedOverline] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TabDimmed] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TabDimmedSelected] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);

	// Docking preview / empty area.
	C[ImGuiCol_DockingPreview] = ImVec4(0.264317f, 0.264317f, 0.264317f, 1.000000f);
	C[ImGuiCol_DockingEmptyBg] = ImVec4(0.264317f, 0.264317f, 0.264317f, 1.000000f);

	// Diagnostics / overlays.
	C[ImGuiCol_PlotLines] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_PlotLinesHovered] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_PlotHistogram] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_PlotHistogramHovered] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_DragDropTarget] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_NavCursor] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_NavWindowingHighlight] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_NavWindowingDimBg] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);
	C[ImGuiCol_ModalWindowDimBg] = ImVec4(1.000000f, 1.000000f, 1.000000f, 0.000000f);

	return C;
}

/** The built-in color defaults, built once. */
const std::array<ImVec4, ImGuiCol_COUNT>& DefaultColors()
{
	static const std::array<ImVec4, ImGuiCol_COUNT> C = BuildDefaultColors();
	return C;
}

} // namespace

const ImVec4& GetEditorBgColor()
{
	return DefaultColors()[ImGuiCol_WindowBg];
}

int GetEditorThemeColorCount()
{
	return GEntryCount;
}

const char* GetEditorThemeColorName(int index)
{
	if (index < 0 || index >= GEntryCount)
	{
		return "";
	}
	return GEntries[index].Name;
}

const char* GetEditorThemeColorGroup(int index)
{
	if (index < 0 || index >= GEntryCount)
	{
		return "";
	}
	return GEntries[index].Group;
}

bool GetEditorThemeColorDefault(int index, float out[4])
{
	if (index < 0 || index >= GEntryCount || out == nullptr)
	{
		return false;
	}
	const ImVec4& C = DefaultColors()[GEntries[index].Id];
	out[0] = C.x;
	out[1] = C.y;
	out[2] = C.z;
	out[3] = C.w;
	return true;
}

void ApplyEditorTheme(const float* flatColors)
{
	if (flatColors == nullptr)
	{
		return;
	}
	ImVec4* Colors = ImGui::GetStyle().Colors;
	for (int i = 0; i < GEntryCount; ++i)
	{
		const float* f = flatColors + i * 4;
		Colors[GEntries[i].Id] = ImVec4(f[0], f[1], f[2], f[3]);
	}
}

void EditorThemeReset()
{
	ImVec4* Colors = ImGui::GetStyle().Colors;
	const auto& Def = DefaultColors();
	for (int i = 0; i < GEntryCount; ++i)
	{
		Colors[GEntries[i].Id] = Def[GEntries[i].Id];
	}
}

int GetEditorThemeStyleCount()
{
	return GStyleCount;
}

const char* GetEditorThemeStyleName(int index)
{
	if (index < 0 || index >= GStyleCount)
	{
		return "";
	}
	return GStyles[index].Name;
}

const char* GetEditorThemeStyleGroup(int index)
{
	if (index < 0 || index >= GStyleCount)
	{
		return "";
	}
	return GStyles[index].Group;
}

int GetEditorThemeStyleArity(int index)
{
	if (index < 0 || index >= GStyleCount)
	{
		return 0;
	}
	return GStyles[index].Arity;
}

bool GetEditorThemeStyleDefault(int index, float out[2])
{
	if (index < 0 || index >= GStyleCount || out == nullptr)
	{
		return false;
	}
	const float* S = DefaultStyles();
	out[0] = S[index * 2];
	out[1] = S[index * 2 + 1];
	return true;
}

void ApplyEditorThemeStyles(const float* flatStyles)
{
	if (flatStyles == nullptr)
	{
		return;
	}
	ImGuiStyle& S = ImGui::GetStyle();
	for (int i = 0; i < GStyleCount; ++i)
	{
		const float* f = flatStyles + i * 2;
		switch (i)
		{
		case 0: S.Alpha = f[0]; break;
		case 1: S.DisabledAlpha = f[0]; break;
		case 2: S.WindowPadding = ImVec2(f[0], f[1]); break;
		case 3: S.WindowRounding = f[0]; break;
		case 4: S.WindowBorderSize = f[0]; break;
		case 5: S.WindowMinSize = ImVec2(f[0], f[1]); break;
		case 6: S.DockingSeparatorSize = f[0]; break;
		case 7: S.ChildRounding = f[0]; break;
		case 8: S.ChildBorderSize = f[0]; break;
		case 9: S.PopupRounding = f[0]; break;
		case 10: S.PopupBorderSize = f[0]; break;
		case 11: S.FramePadding = ImVec2(f[0], f[1]); break;
		case 12: S.FrameRounding = f[0]; break;
		case 13: S.FrameBorderSize = f[0]; break;
		case 14: S.ItemSpacing = ImVec2(f[0], f[1]); break;
		case 15: S.ItemInnerSpacing = ImVec2(f[0], f[1]); break;
		case 16: S.CellPadding = ImVec2(f[0], f[1]); break;
		case 17: S.IndentSpacing = f[0]; break;
		case 18: S.ColumnsMinSpacing = f[0]; break;
		case 19: S.ScrollbarSize = f[0]; break;
		case 20: S.ScrollbarRounding = f[0]; break;
		case 21: S.GrabMinSize = f[0]; break;
		case 22: S.GrabRounding = f[0]; break;
		case 23: S.TabRounding = f[0]; break;
		case 24: S.TabBorderSize = f[0]; break;
		case 25: S.TabBarBorderSize = f[0]; break;
		case 26: S.TabBarOverlineSize = f[0]; break;
		case 27: S.TabCloseButtonMinWidthSelected = f[0]; break;
		case 28: S.TabCloseButtonMinWidthUnselected = f[0]; break;
		case 29: S.ButtonTextAlign = ImVec2(f[0], f[1]); break;
		case 30: S.SelectableTextAlign = ImVec2(f[0], f[1]); break;
		case 31: S.SeparatorTextBorderSize = f[0]; break;
		case 32: S.SeparatorTextAlign = ImVec2(f[0], f[1]); break;
		case 33: S.SeparatorTextPadding = ImVec2(f[0], f[1]); break;
		default: break;
		}
	}
}

void EditorThemeStylesReset()
{
	ApplyEditorThemeStyles(DefaultStyles());
}

bool SaveEditorTheme(const char* path, const float* flatColors, const float* flatStyles)
{
	if (path == nullptr || flatColors == nullptr || flatStyles == nullptr)
	{
		return false;
	}
	// Ensure the parent directory exists so a fresh "Saved/" path works.
	const std::filesystem::path P(path);
	if (!P.parent_path().empty())
	{
		std::error_code EC;
		std::filesystem::create_directories(P.parent_path(), EC);
	}
	FILE* F = std::fopen(path, "w");
	if (F == nullptr)
	{
		return false;
	}
	std::fprintf(F, "MahoTheme 1\nColors %d\n", GEntryCount);
	for (int i = 0; i < GEntryCount; ++i)
	{
		const float* f = flatColors + i * 4;
		std::fprintf(F, "%s %.6f %.6f %.6f %.6f\n", GEntries[i].Name, f[0], f[1], f[2], f[3]);
	}
	std::fprintf(F, "Styles %d\n", GStyleCount);
	for (int i = 0; i < GStyleCount; ++i)
	{
		const float* f = flatStyles + i * 2;
		std::fprintf(F, "%s %.6f %.6f\n", GStyles[i].Name, f[0], f[1]);
	}
	std::fclose(F);
	return true;
}

bool LoadEditorTheme(const char* path, float* outColors, float* outStyles)
{
	if (path == nullptr)
	{
		return false;
	}
	FILE* F = std::fopen(path, "r");
	if (F == nullptr)
	{
		return false;
	}

	char tag[32] = { 0 };
	int version = 0;
	if (std::fscanf(F, "%31s %d", tag, &version) != 2 || std::strcmp(tag, "MahoTheme") != 0)
	{
		std::fclose(F);
		return false;
	}

	bool any = false;
	char kw[32] = { 0 };
	int n = 0;
	while (std::fscanf(F, "%31s", kw) == 1)
	{
		if (std::strcmp(kw, "Colors") == 0 && outColors != nullptr)
		{
			if (std::fscanf(F, "%d", &n) == 1 && n == GEntryCount)
			{
				char name[64] = { 0 };
				for (int k = 0; k < n; ++k)
				{
					float r, g, b, a;
					if (std::fscanf(F, "%63s %f %f %f %f", name, &r, &g, &b, &a) != 5)
					{
						break;
					}
					for (int i = 0; i < GEntryCount; ++i)
					{
						if (std::strcmp(GEntries[i].Name, name) == 0)
						{
							float* f = outColors + i * 4;
							f[0] = r; f[1] = g; f[2] = b; f[3] = a;
							any = true;
							break;
						}
					}
				}
			}
		}
		else if (std::strcmp(kw, "Styles") == 0 && outStyles != nullptr)
		{
			if (std::fscanf(F, "%d", &n) == 1 && n == GStyleCount)
			{
				char name[64] = { 0 };
				for (int k = 0; k < n; ++k)
				{
					float v0, v1;
					if (std::fscanf(F, "%63s %f %f", name, &v0, &v1) != 3)
					{
						break;
					}
					for (int i = 0; i < GStyleCount; ++i)
					{
						if (std::strcmp(GStyles[i].Name, name) == 0)
						{
							float* f = outStyles + i * 2;
							f[0] = v0; f[1] = v1;
							any = true;
							break;
						}
					}
				}
			}
		}
	}
	std::fclose(F);
	return any;
}

const char* GetEditorThemeDefaultPath()
{
	return "Saved/editor_theme.theme";
}

void ApplyMahoNightTheme()
{
	ImGuiStyle& Style = ImGui::GetStyle();

	// Enum-style fields (not numerically exposed).
	Style.WindowMenuButtonPosition = ImGuiDir_None;
	Style.ColorButtonPosition = ImGuiDir_Right;

	// Numeric style defaults, then any persisted overrides.
	ApplyEditorThemeStyles(DefaultStyles());

	// Color defaults, then any persisted overrides.
	EditorThemeReset();

	float Colors[GEntryCount * 4];
	float Styles[GStyleCount * 2];
	for (int i = 0; i < GEntryCount; ++i)
	{
		GetEditorThemeColorDefault(i, &Colors[i * 4]);
	}
	for (int i = 0; i < GStyleCount; ++i)
	{
		GetEditorThemeStyleDefault(i, &Styles[i * 2]);
	}
	if (LoadEditorTheme(GetEditorThemeDefaultPath(), Colors, Styles))
	{
		ApplyEditorTheme(Colors);
		ApplyEditorThemeStyles(Styles);
	}
}

} // namespace Maho
