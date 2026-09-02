#pragma once

#include "UIApi.h"

#include <cstddef>

// The FUI layer owns the Dear ImGui context and exposes the editor shell a thin,
// backend-agnostic component interface. Every method translates directly to an
// ImGui call; the Editor project plugin builds its whole shell through this
// interface and never touches ImGui itself. imgui.h comes transitively from
// linking the UI target (UI.cmake adds <imgui_SOURCE_DIR> as a PUBLIC include).
#include "imgui.h"

namespace Maho
{

/**
 * Backend-agnostic UI component interface (thin ImGui translation layer).
 * FUI implements it (each method forwards to the corresponding ImGui call); the
 * Editor plugin consumes it so the editor shell stays decoupled from the raw
 * ImGui API and from the FUI internals. This is a pure capability interface --
 * any layer may request it, but only FUI supplies it.
 */
class MAHO_UI_API IUIComponent
{
public:
	virtual ~IUIComponent() = default;

	// -- windows -----------------------------------------------------------
	virtual bool BeginWindow(const char* Name, bool* bOpen, ImGuiWindowFlags Flags = 0) = 0;
	virtual void EndWindow() = 0;
	virtual void BeginChild(const char* Id, const ImVec2& Size, ImGuiChildFlags ChildFlags = 0, ImGuiWindowFlags Flags = 0) = 0;
	virtual void EndChild() = 0;

	// -- text / layout -----------------------------------------------------
	virtual void TextUnformatted(const char* Text) = 0;
	virtual void TextDisabled(const char* Text) = 0;
	virtual void TextWrapped(const char* Text) = 0;
	virtual void LabelText(const char* Label, const char* Text) = 0;
	virtual void SameLine(float Offset = 0.0f, float Spacing = -1.0f) = 0;
	virtual void Separator() = 0;
	virtual void Spacing() = 0;
	virtual void NewLine() = 0;
	virtual void Dummy(const ImVec2& Size) = 0;
	virtual void PushID(const char* Id) = 0;
	virtual void PopID() = 0;
	virtual void SetCursorPos(const ImVec2& Pos) = 0;
	virtual void SetCursorPosX(float X) = 0;
	virtual void SetCursorPosY(float Y) = 0;
	virtual void SetCursorScreenPos(const ImVec2& Pos) = 0;
	virtual float GetFrameHeight() = 0;
	virtual ImVec2 GetContentRegionAvail() = 0;

	// -- widgets -----------------------------------------------------------
	virtual bool Button(const char* Label, const ImVec2& Size = ImVec2(0, 0)) = 0;
	virtual bool SmallButton(const char* Label) = 0;
	virtual bool Checkbox(const char* Label, bool* bValue) = 0;
	virtual bool Selectable(const char* Label, bool bSelected = false, ImGuiSelectableFlags Flags = 0) = 0;
	virtual bool InputText(const char* Label, char* Buffer, std::size_t BufferSize, ImGuiInputTextFlags Flags = 0) = 0;
	virtual bool DragFloat(const char* Label, float* v, float vSpeed = 0.1f, float vMin = 0.0f, float vMax = 0.0f, const char* Format = "%.3f") = 0;
	virtual bool DragFloat3(const char* Label, float v[3], float vSpeed = 0.1f, float vMin = 0.0f, float vMax = 0.0f, const char* Format = "%.3f") = 0;
	virtual bool DragInt(const char* Label, int* v, float vSpeed = 1.0f, int vMin = 0, int vMax = 0, const char* Format = "%d") = 0;
	virtual bool SliderFloat(const char* Label, float* v, float vMin, float vMax, const char* Format = "%.3f") = 0;
	virtual bool CollapsingHeader(const char* Label, ImGuiTreeNodeFlags Flags = 0) = 0;
	virtual bool TreeNodeEx(const char* Label, ImGuiTreeNodeFlags Flags = 0) = 0;
	virtual void TreePop() = 0;
	virtual void BeginDisabled(bool bDisabled) = 0;
	virtual void EndDisabled() = 0;
	virtual void OpenPopup(const char* Id) = 0;
	virtual bool BeginPopup(const char* Id, ImGuiWindowFlags Flags = 0) = 0;
	virtual bool BeginPopupModal(const char* Name, bool* bOpen = nullptr, ImGuiWindowFlags Flags = 0) = 0;
	virtual void EndPopup() = 0;

	// -- menus -------------------------------------------------------------
	virtual bool BeginMenuBar() = 0;
	virtual void EndMenuBar() = 0;
	virtual bool BeginMenu(const char* Label, bool bEnabled = true) = 0;
	virtual void EndMenu() = 0;
	virtual bool MenuItem(const char* Label, const char* Shortcut = nullptr, bool bSelected = false, bool bEnabled = true) = 0;

	// -- next-window / style ------------------------------------------------
	virtual void SetNextWindowPos(const ImVec2& Pos, ImGuiCond Cond = 0, const ImVec2& Pivot = ImVec2(0, 0)) = 0;
	virtual void SetNextWindowSize(const ImVec2& Size, ImGuiCond Cond = 0) = 0;
	virtual void SetNextWindowViewport(ImGuiID ViewportId) = 0;
	virtual void SetNextItemWidth(float Width) = 0;
	virtual void PushStyleColor(ImGuiCol Index, const ImVec4& Color) = 0;
	virtual void PopStyleColor(int Count = 1) = 0;
	virtual void PushStyleVar(ImGuiStyleVar Index, float Value) = 0;
	virtual void PushStyleVar(ImGuiStyleVar Index, const ImVec2& Value) = 0;
	virtual void PopStyleVar(int Count = 1) = 0;

	// -- viewport / scrolling ----------------------------------------------
	virtual const ImGuiViewport* GetMainViewport() = 0;
	virtual void SetScrollHereY(float CenterRatio) = 0;
	virtual float GetScrollY() = 0;
	virtual float GetScrollMaxY() = 0;
	virtual void SetScrollY(float ScrollY) = 0;

	// -- docking -------------------------------------------------------------
	virtual ImGuiID GetID(const char* Str) = 0;
	virtual void DockSpace(ImGuiID Id, const ImVec2& Size, ImGuiDockNodeFlags Flags, const ImGuiWindowClass* WindowClass = nullptr) = 0;
	virtual void DockBuilderRemoveNode(ImGuiID NodeId) = 0;
	virtual void DockBuilderAddNode(ImGuiID NodeId, ImGuiDockNodeFlags Flags) = 0;
	virtual void DockBuilderSetNodeSize(ImGuiID NodeId, const ImVec2& Size) = 0;
	virtual void DockBuilderSplitNode(ImGuiID NodeId, ImGuiDir SplitDir, float SizeRatio, ImGuiID* pOutIdNew, ImGuiID* pOutIdRemaining) = 0;
	virtual void DockBuilderDockWindow(const char* WindowName, ImGuiID NodeId) = 0;
	virtual void DockBuilderFinish(ImGuiID NodeId) = 0;

	// -- draw list ------------------------------------------------------------
	virtual void* GetWindowDrawList() = 0;

	// -- debug / demos --------------------------------------------------------
	// Exposes the built-in ImGui demo window ("Dear ImGui Demo") so the shell can
	// sanity-check that the UI pipeline composites correctly before building its
	// own chrome. FUI forwards directly to ImGui::ShowDemoWindow().
	virtual void ShowDemoWindow() = 0;
};

/**
 * Capacitor interface implemented by the Editor shell layer. FUI discovers every
 * implementer through Engine.Select<IEditorShell>() and drives its DrawEditor
 * between ImGui::NewFrame() and ImGui::Render() (the only place ImGui permits
 * building UI). The shell rebuilds all of its chrome / panels through the
 * supplied IUIComponent.
 */
class MAHO_UI_API IEditorShell
{
public:
	virtual ~IEditorShell() = default;
	virtual void DrawEditor(IUIComponent& UI) = 0;
};

} // namespace Maho
