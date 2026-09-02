#pragma once

#include "UIApi.h"
#include "UIComponent.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Engine/Engine.h>

#include <cstddef>
#include <memory>

namespace Maho
{

/** Capability interface: a layer that consults + processes the UI layer's raw
 *  draw data (ImDrawData*, kept as void* so this header stays backend-agnostic).
 *  FRender composes it via IPlugin<IProcessUIData>; FUI discovers every
 *  implementer through Engine.Select<IProcessUIData>() each frame and feeds it
 *  the freshly built ImGui draw data. */
class MAHO_UI_API IProcessUIData
{
public:
	virtual ~IProcessUIData() = default;
	virtual void ProcessUIData(void*) = 0;
};

/**
 * UI engine layer -- hosts the Dear ImGui context (CPU side) and drives it from
 * the engine stages: IInit creates the context (from the Platform layer's
 * window), ITick feeds input + builds the UI + feeds the raw draw data to every
 * IProcessUIData implementer (FRender) via Engine.Select<IProcessUIData>(),
 * IShutdown tears the context down. Rendering the draw data is the UI render
 * feature's job (a custom FRHI backend in the project's UIFeature). This layer
 * lives in the host engine, not inside the render graph.
 */
class MAHO_UI_API FUI : public FLayer<IInit, IBeginFrame, ITick, IEndFrame, IShutdown>, public IUIComponent
{
MAHO_DECLARE_LAYER(FUI, "UI.dll");

	FUI();
	~FUI() override;

public:
	void Initialize(FEngineBase& Engine) override;
	void BeginFrame(FEngineBase& Engine) override;
	void Tick(FEngineBase& Engine) override;
	void EndFrame(FEngineBase& Engine) override;
	void Shutdown(FEngineBase& Engine) override;

	// IUIComponent -- thin ImGui translation layer consumed by the editor shell.
	bool BeginWindow(const char* Name, bool* bOpen, ImGuiWindowFlags Flags) override;
	void EndWindow() override;
	void BeginChild(const char* Id, const ImVec2& Size, ImGuiChildFlags ChildFlags, ImGuiWindowFlags Flags) override;
	void EndChild() override;
	void TextUnformatted(const char* Text) override;
	void TextDisabled(const char* Text) override;
	void TextWrapped(const char* Text) override;
	void LabelText(const char* Label, const char* Text) override;
	void SameLine(float Offset, float Spacing) override;
	void Separator() override;
	void Spacing() override;
	void NewLine() override;
	void Dummy(const ImVec2& Size) override;
	void PushID(const char* Id) override;
	void PopID() override;
	void SetCursorPos(const ImVec2& Pos) override;
	void SetCursorPosX(float X) override;
	void SetCursorPosY(float Y) override;
	void SetCursorScreenPos(const ImVec2& Pos) override;
	float GetFrameHeight() override;
	ImVec2 GetContentRegionAvail() override;
	bool Button(const char* Label, const ImVec2& Size) override;
	bool SmallButton(const char* Label) override;
	bool Checkbox(const char* Label, bool* bValue) override;
	bool Selectable(const char* Label, bool bSelected, ImGuiSelectableFlags Flags) override;
	bool InputText(const char* Label, char* Buffer, std::size_t BufferSize, ImGuiInputTextFlags Flags) override;
	bool DragFloat(const char* Label, float* v, float vSpeed, float vMin, float vMax, const char* Format) override;
	bool DragFloat3(const char* Label, float v[3], float vSpeed, float vMin, float vMax, const char* Format) override;
	bool DragInt(const char* Label, int* v, float vSpeed, int vMin, int vMax, const char* Format) override;
	bool SliderFloat(const char* Label, float* v, float vMin, float vMax, const char* Format) override;
	bool CollapsingHeader(const char* Label, ImGuiTreeNodeFlags Flags) override;
	bool TreeNodeEx(const char* Label, ImGuiTreeNodeFlags Flags) override;
	void TreePop() override;
	void BeginDisabled(bool bDisabled) override;
	void EndDisabled() override;
	void OpenPopup(const char* Id) override;
	bool BeginPopup(const char* Id, ImGuiWindowFlags Flags) override;
	bool BeginPopupModal(const char* Name, bool* bOpen, ImGuiWindowFlags Flags) override;
	void EndPopup() override;
	bool BeginMenuBar() override;
	void EndMenuBar() override;
	bool BeginMenu(const char* Label, bool bEnabled) override;
	void EndMenu() override;
	bool MenuItem(const char* Label, const char* Shortcut, bool bSelected, bool bEnabled) override;
	void SetNextWindowPos(const ImVec2& Pos, ImGuiCond Cond, const ImVec2& Pivot) override;
	void SetNextWindowSize(const ImVec2& Size, ImGuiCond Cond) override;
	void SetNextWindowViewport(ImGuiID ViewportId) override;
	void SetNextItemWidth(float Width) override;
	void PushStyleColor(ImGuiCol Index, const ImVec4& Color) override;
	void PopStyleColor(int Count) override;
	void PushStyleVar(ImGuiStyleVar Index, float Value) override;
	void PushStyleVar(ImGuiStyleVar Index, const ImVec2& Value) override;
	void PopStyleVar(int Count) override;
	const ImGuiViewport* GetMainViewport() override;
	void SetScrollHereY(float CenterRatio) override;
	float GetScrollY() override;
	float GetScrollMaxY() override;
	void SetScrollY(float ScrollY) override;
	ImGuiID GetID(const char* Str) override;
	void DockSpace(ImGuiID Id, const ImVec2& Size, ImGuiDockNodeFlags Flags, const ImGuiWindowClass* WindowClass) override;
	void DockBuilderRemoveNode(ImGuiID NodeId) override;
	void DockBuilderAddNode(ImGuiID NodeId, ImGuiDockNodeFlags Flags) override;
	void DockBuilderSetNodeSize(ImGuiID NodeId, const ImVec2& Size) override;
	void DockBuilderSplitNode(ImGuiID NodeId, ImGuiDir SplitDir, float SizeRatio, ImGuiID* pOutIdNew, ImGuiID* pOutIdRemaining) override;
	void DockBuilderDockWindow(const char* WindowName, ImGuiID NodeId) override;
	void DockBuilderFinish(ImGuiID NodeId) override;
	void* GetWindowDrawList() override;
	void ShowDemoWindow() override;

private:
	struct FData;
	std::unique_ptr<FData> Data;
};

} // namespace Maho
