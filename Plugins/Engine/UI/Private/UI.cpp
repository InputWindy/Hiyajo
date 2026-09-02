#include "UI.h"

#include <Log.h>
#include <Platform.h>

#if defined(_WIN32)
#	include <windows.h>
#endif

#include "imgui.h"
#include "imgui_internal.h"

#include <cstddef>

namespace Maho
{

struct FUI::FData
{
	bool bInitialized = false;
};

FUI::FUI() : Data(std::make_unique<FData>())
{
	// Init: the context needs the window (Platform PostInit) -- declared by ME.
	MyStage<IInit>().IsWaiting<Platform::FPlatform>().ForStage<IPostInit>();

	// Input: Platform's Tick polls GLFW first, then I feed io + build the UI
	// (same frame).
	MyStage<ITick>().IsWaiting<Platform::FPlatform>().ForStage<ITick>();

	// Shutdown: my teardown logs; Log's Shutdown must run after mine.
	MyStage<IShutdown>().IsBlocking<FLog>().OnStage<IShutdown>();
}

FUI::~FUI() = default;

void FUI::Initialize(FEngineBase&)
{
	if (Data->bInitialized)
	{
		return;
	}
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P == nullptr || P->GetWindowWidth() == 0 || P->GetToolkitWindowHandle() == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUI::Initialize: no window; UI disabled");
		return;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // the editor shell docks later
	ImGui::StyleColorsDark();

	Data->bInitialized = true;
	MAHO_LOG_CORE_INFO("FUI: ImGui context created (CPU side; render backend in the UI feature)");
}

void FUI::BeginFrame(FEngineBase& Engine)
{
	if (!Data->bInitialized)
	{
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P != nullptr)
	{
		// Display size must match the render target (SceneColor = swapchain
		// extent), not the window's logical size -- ImGui lays out in DisplaySize
		// coordinates and the render feature clips against it.
		IO.DisplaySize = ImVec2(
			static_cast<float>(P->GetWindowWidth()),
			static_cast<float>(P->GetWindowHeight()));
#if defined(_WIN32)
		// Input bypasses GLFW's message-driven cursor state (the window is created
		// on a pool worker and polled on another, so WM_MOUSEMOVE never reaches
		// glfwGetCursorPos). Win32 global state works from any thread.
		if (HWND Hwnd = static_cast<HWND>(P->GetNativeWindow()))
		{
			POINT Pt{};
			if (::GetCursorPos(&Pt) && ::ScreenToClient(Hwnd, &Pt))
			{
				RECT Client{};
				::GetClientRect(Hwnd, &Client);
				const float ScaleX = Client.right > 0 ? IO.DisplaySize.x / static_cast<float>(Client.right) : 1.f;
				const float ScaleY = Client.bottom > 0 ? IO.DisplaySize.y / static_cast<float>(Client.bottom) : 1.f;
				IO.AddMousePosEvent(static_cast<float>(Pt.x) * ScaleX, static_cast<float>(Pt.y) * ScaleY);
			}
			IO.AddMouseButtonEvent(0, (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
			IO.AddMouseButtonEvent(1, (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
			IO.AddMouseButtonEvent(2, (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
		}
#endif
	}

	// Renderer-backend NewFrame duty (mirrors the imgui_impl_* backends, which
	// must be called before ImGui::NewFrame()): the font atlas is built lazily by
	// ImFontAtlas::Build() and ImGui::NewFrame() asserts IsBuilt(). Calling
	// GetTexDataAsRGBA32() triggers that build on the first frame and returns the
	// existing pixels afterwards; the GPU upload happens later, in the ImGuiRender
	// feature's EnsureBackend, which calls it again and gets the same pixels.
	unsigned char* FontPixels = nullptr;
	int FontW = 0, FontH = 0, FontBpp = 0;
	IO.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH, &FontBpp);
	if (FontPixels == nullptr || FontW <= 0 || FontH <= 0)
	{
		MAHO_LOG_CORE_ERROR("FUI: font atlas not built");
		return;
	}

	ImGui::NewFrame();
}

void FUI::Tick(FEngineBase& Engine)
{
	ImGui::ShowDemoWindow();
}

void FUI::EndFrame(FEngineBase& Engine)
{
	ImGui::Render();

	for (auto* UIRender : Engine.Select<IProcessUIData>().Data)
	{
		(dynamic_cast<IProcessUIData*>(UIRender))->ProcessUIData(ImGui::GetDrawData());
	}
}

void FUI::Shutdown(FEngineBase&)
{
	if (Data != nullptr && Data->bInitialized)
	{
		ImGui::DestroyContext();
		Data->bInitialized = false;
	}
}

// --- IUIComponent: thin translations into the ImGui API -----------------------

bool FUI::BeginWindow(const char* Name, bool* bOpen, ImGuiWindowFlags Flags)
{
	return ImGui::Begin(Name, bOpen, Flags);
}

void FUI::EndWindow() { ImGui::End(); }

void FUI::BeginChild(const char* Id, const ImVec2& Size, ImGuiChildFlags ChildFlags, ImGuiWindowFlags Flags)
{
	ImGui::BeginChild(Id, Size, ChildFlags, Flags);
}

void FUI::EndChild() { ImGui::EndChild(); }

void FUI::TextUnformatted(const char* Text) { ImGui::TextUnformatted(Text); }
void FUI::TextDisabled(const char* Text) { ImGui::TextDisabled("%s", Text); }
void FUI::TextWrapped(const char* Text) { ImGui::TextWrapped("%s", Text); }
void FUI::LabelText(const char* Label, const char* Text) { ImGui::LabelText(Label, "%s", Text); }
void FUI::SameLine(float Offset, float Spacing) { ImGui::SameLine(Offset, Spacing); }
void FUI::Separator() { ImGui::Separator(); }
void FUI::Spacing() { ImGui::Spacing(); }
void FUI::NewLine() { ImGui::NewLine(); }
void FUI::Dummy(const ImVec2& Size) { ImGui::Dummy(Size); }
void FUI::PushID(const char* Id) { ImGui::PushID(Id); }
void FUI::PopID() { ImGui::PopID(); }
void FUI::SetCursorPos(const ImVec2& Pos) { ImGui::SetCursorPos(Pos); }
void FUI::SetCursorPosX(float X) { ImGui::SetCursorPosX(X); }
void FUI::SetCursorPosY(float Y) { ImGui::SetCursorPosY(Y); }
void FUI::SetCursorScreenPos(const ImVec2& Pos) { ImGui::SetCursorScreenPos(Pos); }
float FUI::GetFrameHeight() { return ImGui::GetFrameHeight(); }
ImVec2 FUI::GetContentRegionAvail() { return ImGui::GetContentRegionAvail(); }

bool FUI::Button(const char* Label, const ImVec2& Size) { return ImGui::Button(Label, Size); }
bool FUI::SmallButton(const char* Label) { return ImGui::SmallButton(Label); }
bool FUI::Checkbox(const char* Label, bool* bValue) { return ImGui::Checkbox(Label, bValue); }
bool FUI::Selectable(const char* Label, bool bSelected, ImGuiSelectableFlags Flags) { return ImGui::Selectable(Label, bSelected, Flags); }
bool FUI::InputText(const char* Label, char* Buffer, std::size_t BufferSize, ImGuiInputTextFlags Flags) { return ImGui::InputText(Label, Buffer, BufferSize, Flags); }
bool FUI::DragFloat(const char* Label, float* v, float vSpeed, float vMin, float vMax, const char* Format) { return ImGui::DragFloat(Label, v, vSpeed, vMin, vMax, Format); }
bool FUI::DragFloat3(const char* Label, float v[3], float vSpeed, float vMin, float vMax, const char* Format) { return ImGui::DragFloat3(Label, v, vSpeed, vMin, vMax, Format); }
bool FUI::DragInt(const char* Label, int* v, float vSpeed, int vMin, int vMax, const char* Format) { return ImGui::DragInt(Label, v, vSpeed, vMin, vMax, Format); }
bool FUI::SliderFloat(const char* Label, float* v, float vMin, float vMax, const char* Format) { return ImGui::SliderFloat(Label, v, vMin, vMax, Format); }
bool FUI::CollapsingHeader(const char* Label, ImGuiTreeNodeFlags Flags) { return ImGui::CollapsingHeader(Label, Flags); }
bool FUI::TreeNodeEx(const char* Label, ImGuiTreeNodeFlags Flags) { return ImGui::TreeNodeEx(Label, Flags); }
void FUI::TreePop() { ImGui::TreePop(); }
void FUI::BeginDisabled(bool bDisabled) { ImGui::BeginDisabled(bDisabled); }
void FUI::EndDisabled() { ImGui::EndDisabled(); }
void FUI::OpenPopup(const char* Id) { ImGui::OpenPopup(Id); }
bool FUI::BeginPopup(const char* Id, ImGuiWindowFlags Flags) { return ImGui::BeginPopup(Id, Flags); }
bool FUI::BeginPopupModal(const char* Name, bool* bOpen, ImGuiWindowFlags Flags) { return ImGui::BeginPopupModal(Name, bOpen, Flags); }
void FUI::EndPopup() { ImGui::EndPopup(); }

bool FUI::BeginMenuBar() { return ImGui::BeginMenuBar(); }
void FUI::EndMenuBar() { ImGui::EndMenuBar(); }
bool FUI::BeginMenu(const char* Label, bool bEnabled) { return ImGui::BeginMenu(Label, bEnabled); }
void FUI::EndMenu() { ImGui::EndMenu(); }
bool FUI::MenuItem(const char* Label, const char* Shortcut, bool bSelected, bool bEnabled) { return ImGui::MenuItem(Label, Shortcut, bSelected, bEnabled); }

void FUI::SetNextWindowPos(const ImVec2& Pos, ImGuiCond Cond, const ImVec2& Pivot) { ImGui::SetNextWindowPos(Pos, Cond, Pivot); }
void FUI::SetNextWindowSize(const ImVec2& Size, ImGuiCond Cond) { ImGui::SetNextWindowSize(Size, Cond); }
void FUI::SetNextWindowViewport(ImGuiID ViewportId) { ImGui::SetNextWindowViewport(ViewportId); }
void FUI::SetNextItemWidth(float Width) { ImGui::SetNextItemWidth(Width); }
void FUI::PushStyleColor(ImGuiCol Index, const ImVec4& Color) { ImGui::PushStyleColor(Index, Color); }
void FUI::PopStyleColor(int Count) { ImGui::PopStyleColor(Count); }
void FUI::PushStyleVar(ImGuiStyleVar Index, float Value) { ImGui::PushStyleVar(Index, Value); }
void FUI::PushStyleVar(ImGuiStyleVar Index, const ImVec2& Value) { ImGui::PushStyleVar(Index, Value); }
void FUI::PopStyleVar(int Count) { ImGui::PopStyleVar(Count); }

const ImGuiViewport* FUI::GetMainViewport() { return ImGui::GetMainViewport(); }
void FUI::SetScrollHereY(float CenterRatio) { ImGui::SetScrollHereY(CenterRatio); }
float FUI::GetScrollY() { return ImGui::GetScrollY(); }
float FUI::GetScrollMaxY() { return ImGui::GetScrollMaxY(); }
void FUI::SetScrollY(float ScrollY) { ImGui::SetScrollY(ScrollY); }

ImGuiID FUI::GetID(const char* Str) { return ImGui::GetID(Str); }
void FUI::DockSpace(ImGuiID Id, const ImVec2& Size, ImGuiDockNodeFlags Flags, const ImGuiWindowClass* WindowClass) { ImGui::DockSpace(Id, Size, Flags, WindowClass); }
void FUI::DockBuilderRemoveNode(ImGuiID NodeId) { ImGui::DockBuilderRemoveNode(NodeId); }
void FUI::DockBuilderAddNode(ImGuiID NodeId, ImGuiDockNodeFlags Flags) { ImGui::DockBuilderAddNode(NodeId, Flags); }
void FUI::DockBuilderSetNodeSize(ImGuiID NodeId, const ImVec2& Size) { ImGui::DockBuilderSetNodeSize(NodeId, Size); }
void FUI::DockBuilderSplitNode(ImGuiID NodeId, ImGuiDir SplitDir, float SizeRatio, ImGuiID* pOutIdNew, ImGuiID* pOutIdRemaining) { ImGui::DockBuilderSplitNode(NodeId, SplitDir, SizeRatio, pOutIdNew, pOutIdRemaining); }
void FUI::DockBuilderDockWindow(const char* WindowName, ImGuiID NodeId) { ImGui::DockBuilderDockWindow(WindowName, NodeId); }
void FUI::DockBuilderFinish(ImGuiID NodeId) { ImGui::DockBuilderFinish(NodeId); }

void* FUI::GetWindowDrawList() { return static_cast<void*>(ImGui::GetWindowDrawList()); }

void FUI::ShowDemoWindow() { ImGui::ShowDemoWindow(); }

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_UI_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FUI::CreateLayer();
}
