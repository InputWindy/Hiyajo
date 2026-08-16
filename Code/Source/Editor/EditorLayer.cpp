#include "Editor/EditorLayer.h"

#include <Core/EngineBase.h>
#include <Core/Misc/ConfigFile.h>
#include <Core/Misc/Console.h>
#include "Editor/AgentChatClient.h"
#include "Editor/EditorUIRegistry.h"
#include <Platform.h>
#include <RenderSystem.h>
#include <ResourceSystem.h>
#include <Core/Misc/Log.h>
#include <Core/Misc/Paths.h>
#include <Core/Misc/Utf8Path.h>
#include <UI/ImGuiExtensions.h>

#include <ResourceIO.h>
#include "Resource/CassetPackageImporter.h"
#include "Resource/ResourceTypes.h"
#include "Resource/ResourceIOTraits.h"
#include "Resource/TextureImageCodec.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <Windows.h>
#endif

#if !defined(MAHO_ENGINE_ROOT)
#	define MAHO_ENGINE_ROOT ""
#endif

namespace ed = ax::NodeEditor;

namespace Maho
{

// Set to 1 to restore previous demo widgets inside editor panels.
#define MAHO_EDITOR_DEMO_CONTENT 0
// Output / Blueprint / Plot / file dialogs — keep code, hide from shell for now.
#define MAHO_EDITOR_EXTRA_PANELS 0
// Temporary: only ShowDemoWindow on Update (isolate ImGui::Render crash).
#define MAHO_EDITOR_DEMO_ONLY 0

namespace
{

[[nodiscard]] std::string TrimAscii(std::string Text)
{
	while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.front())))
	{
		Text.erase(Text.begin());
	}
	while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.back())))
	{
		Text.pop_back();
	}
	return Text;
}

#if MAHO_EDITOR_DEMO_CONTENT
[[nodiscard]] const char* PlayStateLabel(FEditorLayer::EPlayState State)
{
	switch (State)
	{
	case FEditorLayer::EPlayState::Playing:
		return "Playing";
	case FEditorLayer::EPlayState::Paused:
		return "Paused";
	default:
		return "Stopped";
	}
}
#endif

/** Hide dock-node title-bar close (far-right X); tab close still works when p_open != nullptr. */
void ApplyDockTabOnlyCloseClass()
{
	ImGuiWindowClass Class;
	Class.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
		| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton));
	ImGui::SetNextWindowClass(&Class);
}

[[nodiscard]] bool BeginEditorDockPanel(const char* Title, bool* bOpen, ImGuiWindowFlags Flags = 0)
{
	ApplyDockTabOnlyCloseClass();
	return ImGui::Begin(Title, bOpen, Flags);
}

// Dock / window titles must stay in sync with DockBuilderDockWindow.
constexpr const char* kWinMainViewport = ICON_FA_MAP "  MyGame";
constexpr const char* kWinContent = ICON_FA_FOLDER_TREE " Content Browser";
constexpr const char* kWinOutput = ICON_FA_TERMINAL " Output Log";
constexpr const char* kWinAgent = ICON_FA_COMMENTS " Agent";
constexpr const char* kWinBlueprint = ICON_FA_DIAGRAM_PROJECT " Blueprint";
constexpr const char* kWinSequenceGraph = ICON_FA_SHARE_NODES " Sequence Graph";
constexpr const char* kWinPlot = ICON_FA_CHART_LINE " Plot";
constexpr const char* kWinWallpaper = ICON_FA_IMAGE " Wallpaper";
constexpr const char* kWinOutliner = ICON_FA_LIST " Scene Outliner";
constexpr const char* kWinInspector = ICON_FA_MAGNIFYING_GLASS " Inspector";
constexpr const char* kModalBusyTitle = "Busy";

[[nodiscard]] const char* EngineStageLabel(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::PreInit: return "PreInit";
	case EEngineStage::Init: return "Init";
	case EEngineStage::PostInit: return "PostInit";
	case EEngineStage::Attach: return "Attach";
	case EEngineStage::Tick: return "Tick";
	case EEngineStage::Detach: return "Detach";
	case EEngineStage::PrepareExit: return "PrepareExit";
	case EEngineStage::Shutdown: return "Shutdown";
	default: return "?";
	}
}

[[nodiscard]] const char* AppStateLabel(EAppState State)
{
	switch (State)
	{
	case EAppState::Stopped: return "Stopped";
	case EAppState::Running: return "Running";
	case EAppState::WaitForExit: return "WaitForExit";
	default: return "?";
	}
}

// Stable IDs for Sequence Graph canvas (must not collide across modes).
namespace SeqGraphIds
{
	constexpr int ExtNodeBase = 1000;
	constexpr int ExtInPinBase = 11000;
	constexpr int ExtOutPinBase = 12000;
	constexpr int LifeNodeBase = 5000;
	constexpr int LifeOutPinBase = 5100;
	constexpr int LifeInPinBase = 5200;
}

[[nodiscard]] const char* ExtensionPriorityLabel(EExtensionPriority Priority)
{
	switch (Priority)
	{
	case EExtensionPriority::System: return "System";
	case EExtensionPriority::Layer: return "Layer";
	case EExtensionPriority::Overlay: return "Overlay";
	default: return "?";
	}
}

[[nodiscard]] ImVec4 ExtensionPriorityColor(EExtensionPriority Priority)
{
	switch (Priority)
	{
	case EExtensionPriority::System:
		return ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
	case EExtensionPriority::Layer:
		return ImVec4(0.70f, 0.95f, 0.55f, 1.0f);
	case EExtensionPriority::Overlay:
		return ImVec4(0.95f, 0.75f, 0.45f, 1.0f);
	default:
		return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
	}
}

[[nodiscard]] int FindExtensionIndex(
	const std::vector<IEngineExtension*>& Extensions,
	IEngineExtension* Target)
{
	for (std::size_t Index = 0; Index < Extensions.size(); ++Index)
	{
		if (Extensions[Index] == Target)
		{
			return static_cast<int>(Index);
		}
	}
	return -1;
}


[[nodiscard]] ImVec4 OutputColorForLevel(spdlog::level::level_enum Level)
{
	switch (Level)
	{
	case spdlog::level::trace:
		return ImVec4(0.50f, 0.52f, 0.56f, 1.0f);
	case spdlog::level::debug:
		return ImVec4(0.62f, 0.72f, 0.85f, 1.0f);
	case spdlog::level::info:
		return ImVec4(0.88f, 0.90f, 0.92f, 1.0f);
	case spdlog::level::warn:
		return ImVec4(0.95f, 0.80f, 0.28f, 1.0f);
	case spdlog::level::err:
		return ImVec4(0.95f, 0.42f, 0.38f, 1.0f);
	case spdlog::level::critical:
		return ImVec4(1.0f, 0.28f, 0.40f, 1.0f);
	default:
		return ImVec4(0.88f, 0.90f, 0.92f, 1.0f);
	}
}

[[nodiscard]] ImVec4 AgentColorForRole(EAgentChatRole Role)
{
	switch (Role)
	{
	case EAgentChatRole::User:
		return ImVec4(0.55f, 0.82f, 1.0f, 1.0f);
	case EAgentChatRole::Assistant:
		return ImVec4(0.88f, 0.90f, 0.92f, 1.0f);
	default:
		return ImVec4(0.70f, 0.72f, 0.55f, 1.0f);
	}
}

[[nodiscard]] const char* AgentRoleLabel(EAgentChatRole Role)
{
	switch (Role)
	{
	case EAgentChatRole::User:
		return "You";
	case EAgentChatRole::Assistant:
		return "Agent";
	default:
		return "System";
	}
}

[[nodiscard]] EResourceType InferImportTypeLocal(const std::string& SourcePath)
{
	if (TResourceIOTraits<FTexture2D>::MatchesSourcePath(SourcePath))
	{
		return EResourceType::Texture2D;
	}
	if (TResourceIOTraits<FTexture3D>::MatchesSourcePath(SourcePath))
	{
		return EResourceType::Texture3D;
	}
	if (TResourceIOTraits<FTextureCube>::MatchesSourcePath(SourcePath))
	{
		return EResourceType::TextureCube;
	}
	if (TResourceIOTraits<FTextureCubeArray>::MatchesSourcePath(SourcePath))
	{
		return EResourceType::TextureCubeArray;
	}
	if (TResourceIOTraits<FTexture2DArray>::MatchesSourcePath(SourcePath))
	{
		return EResourceType::Texture2DArray;
	}
	if (TResourceIOTraits<FPrefab>::MatchesSourcePath(SourcePath))
	{
		return EResourceType::Prefab;
	}
	return EResourceType::Unknown;
}

[[nodiscard]] bool CanImportSourcePathLocal(const std::string& SourcePath)
{
	return InferImportTypeLocal(SourcePath) != EResourceType::Unknown;
}

	[[nodiscard]] bool EnqueueTypedImport(
		FResourceSystem& Resources,
		FResourceImportConfig Config,
		EResourceType TypeHint,
		std::string& OutSoftPath)
{
	if (TypeHint == EResourceType::Unknown)
	{
		TypeHint = InferImportTypeLocal(Config.SourcePath);
	}
	Config.TypeHint = TypeHint;

	switch (TypeHint)
	{
	case EResourceType::Texture2D:
		return Resources.Import<TResourceImporter<FTexture2D>>(std::move(Config), OutSoftPath);
	case EResourceType::Texture3D:
		return Resources.Import<TResourceImporter<FTexture3D>>(std::move(Config), OutSoftPath);
	case EResourceType::TextureCube:
		return Resources.Import<TResourceImporter<FTextureCube>>(std::move(Config), OutSoftPath);
	case EResourceType::TextureCubeArray:
		return Resources.Import<TResourceImporter<FTextureCubeArray>>(std::move(Config), OutSoftPath);
	case EResourceType::Texture2DArray:
		return Resources.Import<TResourceImporter<FTexture2DArray>>(std::move(Config), OutSoftPath);
	case EResourceType::Prefab:
		return Resources.Import<TResourceImporter<FPrefab>>(std::move(Config), OutSoftPath);
	default:
		MAHO_CORE_ERROR(
			"EnqueueTypedImport: unsupported type {} for '{}'",
			static_cast<int>(TypeHint),
			Config.SourcePath);
		return false;
	}
}

#if MAHO_EDITOR_DEMO_CONTENT
struct FContentIcon
{
	const char* Glyph = ICON_FA_FILE;
	ImVec4 Color = ImVec4(0.78f, 0.82f, 0.88f, 1.0f);
};

[[nodiscard]] FContentIcon ContentIconForPath(const std::filesystem::path& Path, bool bIsDirectory)
{
	if (bIsDirectory)
	{
		return {ICON_FA_FOLDER, ImVec4(0.95f, 0.78f, 0.35f, 1.0f)};
	}

	std::string Ext = Path.extension().string();
	for (char& Ch : Ext)
	{
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	}

	if (Ext == ".cpp" || Ext == ".h" || Ext == ".hpp" || Ext == ".c" || Ext == ".cc")
	{
		return {ICON_FA_FILE_CODE, ImVec4(0.45f, 0.78f, 0.95f, 1.0f)};
	}
	if (Ext == ".lua" || Ext == ".py" || Ext == ".js" || Ext == ".ts")
	{
		return {ICON_FA_FILE_CODE, ImVec4(0.55f, 0.85f, 0.55f, 1.0f)};
	}
	if (Ext == ".bat" || Ext == ".cmd" || Ext == ".ps1" || Ext == ".sh")
	{
		return {ICON_FA_TERMINAL, ImVec4(0.55f, 0.85f, 0.70f, 1.0f)};
	}
	if (Ext == ".sln" || Ext == ".vcxproj" || Ext == ".cproject" || Ext == ".cmake" || Ext == ".txt")
	{
		return {ICON_FA_FILE_LINES, ImVec4(0.75f, 0.78f, 0.85f, 1.0f)};
	}
	if (Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".tga" || Ext == ".bmp")
	{
		return {ICON_FA_FILE_IMAGE, ImVec4(0.90f, 0.55f, 0.85f, 1.0f)};
	}
	if (Ext == ".json" || Ext == ".xml" || Ext == ".ini" || Ext == ".yaml" || Ext == ".yml")
	{
		return {ICON_FA_GEAR, ImVec4(0.70f, 0.75f, 0.82f, 1.0f)};
	}
	if (Ext == ".hlsl" || Ext == ".glsl" || Ext == ".usf" || Ext == ".ush" || Ext == ".shader")
	{
		return {ICON_FA_CUBE, ImVec4(0.70f, 0.60f, 0.95f, 1.0f)};
	}
	return {ICON_FA_FILE, ImVec4(0.78f, 0.82f, 0.88f, 1.0f)};
}
#endif

void IdentityMatrix(float* M)
{
	std::memset(M, 0, sizeof(float) * 16);
	M[0] = M[5] = M[10] = M[15] = 1.0f;
}

void LookAtRH(float* Out, float EyeX, float EyeY, float EyeZ, float AtX, float AtY, float AtZ)
{
	float Fx = AtX - EyeX;
	float Fy = AtY - EyeY;
	float Fz = AtZ - EyeZ;
	const float FLen = std::sqrt(Fx * Fx + Fy * Fy + Fz * Fz);
	Fx /= FLen;
	Fy /= FLen;
	Fz /= FLen;
	float Sx = Fy * 0.0f - Fz * 1.0f;
	float Sy = Fz * 0.0f - Fx * 0.0f;
	float Sz = Fx * 1.0f - Fy * 0.0f;
	const float SLen = std::sqrt(Sx * Sx + Sy * Sy + Sz * Sz);
	Sx /= SLen;
	Sy /= SLen;
	Sz /= SLen;
	const float Ux = Sy * Fz - Sz * Fy;
	const float Uy = Sz * Fx - Sx * Fz;
	const float Uz = Sx * Fy - Sy * Fx;
	IdentityMatrix(Out);
	Out[0] = Sx;
	Out[4] = Sy;
	Out[8] = Sz;
	Out[1] = Ux;
	Out[5] = Uy;
	Out[9] = Uz;
	Out[2] = -Fx;
	Out[6] = -Fy;
	Out[10] = -Fz;
	Out[12] = -(Sx * EyeX + Sy * EyeY + Sz * EyeZ);
	Out[13] = -(Ux * EyeX + Uy * EyeY + Uz * EyeZ);
	Out[14] = Fx * EyeX + Fy * EyeY + Fz * EyeZ;
}

void PerspectiveRH(float* Out, float FovYRadians, float Aspect, float ZNear, float ZFar)
{
	IdentityMatrix(Out);
	const float F = 1.0f / std::tan(FovYRadians * 0.5f);
	Out[0] = F / Aspect;
	Out[5] = F;
	Out[10] = ZFar / (ZNear - ZFar);
	Out[11] = -1.0f;
	Out[14] = (ZFar * ZNear) / (ZNear - ZFar);
	Out[15] = 0.0f;
}

} // namespace

FEditorLayer::FEditorLayer()
	: FLayer("EditorLayer")
{
	IdentityMatrix(ObjectMatrix);
	ObjectMatrix[12] = 0.0f;
	ObjectMatrix[13] = 0.0f;
	ObjectMatrix[14] = 0.0f;
	LookAtRH(ViewMatrix, 5.0f, 4.0f, 5.0f, 0.0f, 0.0f, 0.0f);
	PerspectiveRH(ProjectionMatrix, 45.0f * 3.14159265f / 180.0f, 1.6f, 0.1f, 100.0f);
	GizmoOperation = static_cast<int>(ImGuizmo::TRANSLATE);
}

FEditorLayer::~FEditorLayer()
{
	UnmountEditor();
}

void FEditorLayer::MountEditor()
{
	if (bEditorMounted)
	{
		return;
	}
	bEditorMounted = true;

	EnsureContentMounts();
	SelectContentFolder("/Game");
	// Defer .casset LoadPackage until the editor UI has drawn and settled.
	bStartupCassetLoadStarted = false;
	bStartupCassetLoadActive = false;
	bStartupCassetScanDone = false;
	EditorUiPresentedFrames = 0;
	StartupCassetPaths.clear();
	StartupCassetSoftPaths.clear();
	StartupCassetNextIndex = 0;
	StartupCassetLoaded = 0;
	AppendOutput("Output Log ready. Commands: `Dump` | `<Name>` | `<Name> <Value>` | `help`.");
	StartAgentChat();

	{
		ed::Config Config;
		Config.SettingsFile = "Config/SequenceGraphNodeEditor.json";
		SequenceGraphEditorContext = ed::CreateEditor(&Config);
		bSequenceGraphEditorInited = SequenceGraphEditorContext != nullptr;
		bSequenceGraphLayoutApplied = false;
	}

#if MAHO_EDITOR_DEMO_CONTENT
	ed::Config Config;
	Config.SettingsFile = "Config/NodeEditor.json";
	NodeEditorContext = ed::CreateEditor(&Config);
	bBlueprintInited = NodeEditorContext != nullptr;
#endif

	RegisterBuiltinUIContributions();
	RegisterDummyUIContributions();
}

void FEditorLayer::UnmountEditor()
{
	if (!bEditorMounted)
	{
		return;
	}
	bEditorMounted = false;

	UIRegistry.Clear();

	if (GEngine)
	{
		for (FResourceBrowserWindow& Window : OpenResourceBrowsers)
		{
			ReleaseResourceBrowserPreview(Window, *GEngine);
		}
		OpenResourceBrowsers.clear();
		ClearWallpaper(*GEngine);
	}
	else
	{
		OpenResourceBrowsers.clear();
	}
	ResourceBrowserFocusKey.clear();

	ManualImportJobs.clear();
	bManualImportActive = false;
	bStartupCassetLoadStarted = false;
	bStartupCassetLoadActive = false;
	bStartupCassetScanDone = false;
	EditorUiPresentedFrames = 0;
	StartupCassetPaths.clear();
	StartupCassetSoftPaths.clear();
	StartupCassetNextIndex = 0;
	StartupCassetLoaded = 0;
	ManualImportKickIndex = 0;
	ManualImportCompleted = 0;
	ManualImportCurrentName.clear();
	ImporterDialog = {};
	bContentBrowserDropRectValid = false;

	if (AgentChat)
	{
		AgentChat->Stop();
		AgentChat.reset();
	}
	if (SequenceGraphEditorContext)
	{
		ed::DestroyEditor(static_cast<ed::EditorContext*>(SequenceGraphEditorContext));
		SequenceGraphEditorContext = nullptr;
		bSequenceGraphEditorInited = false;
		bSequenceGraphLayoutApplied = false;
	}
	if (NodeEditorContext)
	{
		ed::DestroyEditor(static_cast<ed::EditorContext*>(NodeEditorContext));
		NodeEditorContext = nullptr;
		bBlueprintInited = false;
	}
}

FEditorUIDrawContext FEditorLayer::MakeUIDrawContext(FEngineBase& App)
{
	FEditorUIDrawContext Ctx;
	Ctx.App = &App;
	Ctx.Editor = this;
	Ctx.Registry = &UIRegistry;
	return Ctx;
}

void FEditorLayer::RegisterBuiltinUIContributions()
{
	UIRegistry.Clear();

	const FEditorUICatalog CatFileOps{ "FileOps", 10 };
	const FEditorUICatalog CatTransform{ "Transform", 20 };
	const FEditorUICatalog CatPlay{ "Play", 30 };
	const FEditorUICatalog CatBrowser{ "Browser", 10 };
	const FEditorUICatalog CatLog{ "Log", 20 };
	const FEditorUICatalog CatTools{ "Tools", 30 };
	const FEditorUICatalog CatDetails{ "Details", 40 };
	const FEditorUICatalog CatSystem{ "System", 10 };
	const FEditorUICatalog CatDebug{ "Debug", 100 };
	const FEditorUICatalog CatWindow{ "Window", 20 };
	const FEditorUICatalog CatHelp{ "Help", 30 };

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatFileOps,
		"builtin.tb1.save",
		0,
		[](FEditorUIDrawContext&)
		{
			ImGui::Button(ICON_FA_FLOPPY_DISK "##Tb1Save");
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatFileOps,
		"builtin.tb1.open",
		1,
		[](FEditorUIDrawContext&)
		{
			ImGui::Button(ICON_FA_FOLDER_OPEN "##Tb1Open");
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatFileOps,
		"builtin.tb1.search",
		2,
		[](FEditorUIDrawContext&)
		{
			ImGui::Button(ICON_FA_MAGNIFYING_GLASS "##Tb1Search");
		} });

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatTransform,
		"builtin.tb2.select",
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			if (ImGui::Button(ICON_FA_ARROW_POINTER "##Tb2Select", BtnSize))
			{
				ViewportTool = EViewportTool::Select;
			}
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatTransform,
		"builtin.tb2.translate",
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			if (ImGui::Button(ICON_FA_UP_DOWN_LEFT_RIGHT "##Tb2Translate", BtnSize))
			{
				ViewportTool = EViewportTool::Translate;
				GizmoOperation = static_cast<int>(ImGuizmo::TRANSLATE);
			}
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatTransform,
		"builtin.tb2.rotate",
		2,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##Tb2Rotate", BtnSize))
			{
				ViewportTool = EViewportTool::Rotate;
				GizmoOperation = static_cast<int>(ImGuizmo::ROTATE);
			}
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatTransform,
		"builtin.tb2.scale",
		3,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			if (ImGui::Button(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER "##Tb2Scale", BtnSize))
			{
				ViewportTool = EViewportTool::Scale;
				GizmoOperation = static_cast<int>(ImGuizmo::SCALE);
			}
		} });

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatPlay,
		"builtin.tb2.play",
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			const bool bCanPlay = PlayState == EPlayState::Stopped || PlayState == EPlayState::Paused;
			ImGui::BeginDisabled(!bCanPlay);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.86f, 0.42f, 1.0f));
			if (ImGui::Button(ICON_FA_PLAY "##Tb2Play", BtnSize))
			{
				PlayState = EPlayState::Playing;
				AppendOutput("PIE: Play");
			}
			ImGui::PopStyleColor();
			ImGui::EndDisabled();
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatPlay,
		"builtin.tb2.step",
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			const bool bCanStep = PlayState != EPlayState::Stopped;
			ImGui::BeginDisabled(!bCanStep);
			if (ImGui::Button(ICON_FA_FORWARD_STEP "##Tb2Step", BtnSize))
			{
				PlayState = EPlayState::Paused;
				AppendOutput("PIE: Step / Pause");
			}
			ImGui::EndDisabled();
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatPlay,
		"builtin.tb2.stop",
		2,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			const bool bCanStop = PlayState != EPlayState::Stopped;
			ImGui::BeginDisabled(!bCanStop);
			if (ImGui::Button(ICON_FA_STOP "##Tb2Stop", BtnSize))
			{
				PlayState = EPlayState::Stopped;
				AppendOutput("PIE: Stop");
			}
			ImGui::EndDisabled();
		} });

	UIRegistry.RegisterDockPanel({
		CatBrowser,
		"dock.content",
		kWinContent,
		&bShowContentBrowser,
		true,
		false,
		0,
		[this](FEditorUIDrawContext&)
		{
			DrawContentBrowser();
		} });
	UIRegistry.RegisterDockPanel({
		CatLog,
		"dock.output",
		kWinOutput,
		&bShowOutputPanel,
		true,
		false,
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (Ctx.App)
			{
				DrawOutputPanel(*Ctx.App);
			}
		} });
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.agent",
		kWinAgent,
		&bShowAgentPanel,
		true,
		false,
		0,
		[this](FEditorUIDrawContext&)
		{
			DrawAgentPanel();
		} });
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.wallpaper",
		kWinWallpaper,
		&bShowWallpaperPanel,
		true,
		false,
		0,
		[this](FEditorUIDrawContext&)
		{
			DrawWallpaperPanel();
		} });
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.sequence",
		kWinSequenceGraph,
		&bShowSequenceGraphPanel,
		true,
		false,
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (Ctx.App)
			{
				DrawSequenceGraphPanel(*Ctx.App);
			}
		} });
#if MAHO_EDITOR_EXTRA_PANELS
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.blueprint",
		kWinBlueprint,
		&bShowBlueprintPanel,
		true,
		false,
		2,
		[this](FEditorUIDrawContext&)
		{
			DrawBlueprintPanel();
		} });
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.plot",
		kWinPlot,
		&bShowPlotPanel,
		true,
		false,
		3,
		[this](FEditorUIDrawContext&)
		{
			DrawPlotPanel();
		} });
#endif
	const FEditorUICatalog CatScene{ "Scene", 35 };
	const FEditorUICatalog CatInspector{ "Inspector", 45 };
	UIRegistry.RegisterDockPanel({
		CatScene,
		"dock.outliner",
		kWinOutliner,
		&bShowOutliner,
		true,
		false,
		0,
		[this](FEditorUIDrawContext&)
		{
			DrawSceneOutliner();
		} });
	UIRegistry.RegisterDockPanel({
		CatInspector,
		"dock.inspector",
		kWinInspector,
		&bShowInspector,
		true,
		true,
		0,
		[this](FEditorUIDrawContext&)
		{
			DrawInspectorPanel();
		} });

	UIRegistry.RegisterModal({
		CatSystem,
		"modal.busy",
		kModalBusyTitle,
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			ImGui::TextUnformatted("Blocking work in progress…");
			ImGui::Spacing();
			if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)) && Ctx.Registry)
			{
				Ctx.Registry->CloseModal("modal.busy");
			}
		} });

	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.output_autoscroll",
		ICON_FA_SCROLL "  Output Auto-Scroll",
		10,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem(ICON_FA_SCROLL "  Output Auto-Scroll", nullptr, &bAutoScrollOutput);
		} });
	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.agent_autoscroll",
		ICON_FA_SCROLL "  Agent Auto-Scroll",
		11,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem(ICON_FA_SCROLL "  Agent Auto-Scroll", nullptr, &bAutoScrollAgent);
		} });
	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.reset_dock",
		ICON_FA_TABLE_CELLS_LARGE "  Reset Dock Layout",
		20,
		[this](FEditorUIDrawContext&)
		{
			if (ImGui::MenuItem(ICON_FA_TABLE_CELLS_LARGE "  Reset Dock Layout"))
			{
				bBuildDefaultLayout = true;
			}
		} });

	UIRegistry.RegisterMenuItem({
		CatDebug,
		"menu.debug.toggle_dummy",
		"Show Dummy UI",
		0,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem("Show Dummy UI", nullptr, &bShowDummyUI);
		} });
	UIRegistry.RegisterMenuItem({
		CatDebug,
		"menu.debug.open_details",
		"Inspector Panel",
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (ImGui::MenuItem("Inspector Panel") && Ctx.Registry)
			{
				Ctx.Registry->OpenDockPanel("dock.inspector");
			}
		} });
	UIRegistry.RegisterMenuItem({
		CatDebug,
		"menu.debug.open_outliner",
		"Scene Outliner",
		2,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (ImGui::MenuItem("Scene Outliner") && Ctx.Registry)
			{
				Ctx.Registry->OpenDockPanel("dock.outliner");
			}
		} });
	UIRegistry.RegisterMenuItem({
		CatDebug,
		"menu.debug.open_busy",
		"Open Busy Modal",
		2,
		[](FEditorUIDrawContext& Ctx)
		{
			if (ImGui::MenuItem("Open Busy Modal") && Ctx.Registry)
			{
				Ctx.Registry->OpenModal("modal.busy");
			}
		} });

#if MAHO_EDITOR_DEMO_CONTENT
	UIRegistry.RegisterMenuItem({
		CatHelp,
		"menu.help.cvar",
		ICON_FA_CIRCLE_INFO "  CVar help",
		0,
		[this](FEditorUIDrawContext&)
		{
			if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO "  CVar help"))
			{
				AppendOutput("Commands: `Dump` | `Name` | `Name Value` | `help`");
			}
		} });
	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.imgui_demo",
		ICON_FA_TABLE_CELLS "  ImGui Demo",
		30,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem(ICON_FA_TABLE_CELLS "  ImGui Demo", nullptr, &bShowDemoWindow);
		} });
	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.implot_demo",
		ICON_FA_CHART_AREA "  ImPlot Demo",
		31,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem(ICON_FA_CHART_AREA "  ImPlot Demo", nullptr, &bShowImPlotDemo);
		} });
#else
	(void)CatHelp;
#endif
}

void FEditorLayer::RegisterDummyUIContributions()
{
	const FEditorUICatalog CatDummy{ "Dummy", 900 };

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatDummy,
		"dummy.tb1.a",
		0,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::Button("DumA##Tb1");
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatDummy,
		"dummy.tb1.b",
		1,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::Button("DumB##Tb1");
		} });

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatDummy,
		"dummy.tb2.a",
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			ImGui::Button("A##Tb2Dum", BtnSize);
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatDummy,
		"dummy.tb2.b",
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			ImGui::Button("B##Tb2Dum", BtnSize);
		} });

	UIRegistry.RegisterMenuItem({
		CatDummy,
		"dummy.menu.a",
		"Dummy Menu A",
		0,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::MenuItem("Dummy Menu A");
		} });
	UIRegistry.RegisterMenuItem({
		CatDummy,
		"dummy.menu.b",
		"Dummy Menu B",
		1,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::MenuItem("Dummy Menu B");
		} });

	UIRegistry.RegisterDockPanel({
		CatDummy,
		"dummy.dock.a",
		ICON_FA_CUBE "  Dummy Dock A",
		&bShowDummyDockA,
		false,
		true,
		0,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			if (!BeginEditorDockPanel(ICON_FA_CUBE "  Dummy Dock A", &bShowDummyDockA))
			{
				ImGui::End();
				return;
			}
			ImGui::TextUnformatted("Dummy dock panel A (Catalog Dummy).");
			ImGui::End();
		} });
	UIRegistry.RegisterDockPanel({
		CatDummy,
		"dummy.dock.b",
		ICON_FA_CUBES "  Dummy Dock B",
		&bShowDummyDockB,
		false,
		true,
		1,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			if (!BeginEditorDockPanel(ICON_FA_CUBES "  Dummy Dock B", &bShowDummyDockB))
			{
				ImGui::End();
				return;
			}
			ImGui::TextUnformatted("Dummy dock panel B (Catalog Dummy).");
			ImGui::End();
		} });

	UIRegistry.RegisterViewportOverlay({
		CatDummy,
		"dummy.viewport.a",
		0,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
			ImGui::TextDisabled("Dummy Overlay A");
		} });
	UIRegistry.RegisterViewportOverlay({
		CatDummy,
		"dummy.viewport.b",
		1,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::SetCursorPos(ImVec2(12.0f, 28.0f));
			ImGui::TextDisabled("Dummy Overlay B");
		} });

	UIRegistry.RegisterMenuItem({
		FEditorUICatalog{ "Debug", 100 },
		"menu.debug.open_dummy_docks",
		"Open Dummy Docks",
		3,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (ImGui::MenuItem("Open Dummy Docks") && Ctx.Registry)
			{
				Ctx.Registry->OpenDockPanel("dummy.dock.a");
				Ctx.Registry->OpenDockPanel("dummy.dock.b");
			}
		} });
}

bool FEditorLayer::ExecuteStage(EEngineStage Stage)
{
	if (Stage == EEngineStage::Attach)
	{
#if !MAHO_EDITOR_DEMO_ONLY
		MountEditor();
#endif
		return true;
	}
	if (Stage == EEngineStage::Detach)
	{
#if !MAHO_EDITOR_DEMO_ONLY
		UnmountEditor();
#endif
		return true;
	}
	if (Stage != EEngineStage::Tick)
	{
		return true;
	}
	if (ImGui::GetCurrentContext() == nullptr)
	{
		return true;
	}

#if MAHO_EDITOR_DEMO_ONLY
	static bool bShowDemoWindowDiag = true;
	ImGui::ShowDemoWindow(&bShowDemoWindowDiag);
	return true;
#else
	if (!GEngine)
	{
		return true;
	}
	FEngineBase& App = *GEngine;

	DrainEngineLogs(App);

	if (AgentChat)
	{
		AgentChat->Tick();
		std::vector<FAgentChatBubble> Remote;
		AgentChat->DrainRemoteBubbles(Remote);
		for (FAgentChatBubble& Bubble : Remote)
		{
			AppendAgentBubble(Bubble.Role, std::move(Bubble.Text));
		}
	}

	EnsureDefaultWallpaper(App);
	ProcessEditorFileDrops(App);
	// Wait until the editor UI has drawn and settled for a few frames.
	if (EditorUiPresentedFrames >= kEditorUiSettleFrames)
	{
		TickStartupCassetLoad();
	}
	TickManualContentImport();
	{
		FResourceSystem* Resources = Detail::GetResourceSystem();
		const bool bSaving = Resources && Resources->IsSavePackageBusy();
		if (bWasSavePackageBusy && !bSaving)
		{
			AppendOutput("Save finished");
			RefreshContentListing();
		}
		bWasSavePackageBusy = bSaving;
	}
	DrawImporterDialog();
	DrawDockSpace(App);
	DrawMainViewportPanel();
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(App);
		UIRegistry.DrawDockPanels(Ctx);
		UIRegistry.DrawModals(Ctx);
	}
	DrawOpenResourceBrowsers(App);
	DrawContentImportProgressOverlay();
#if MAHO_EDITOR_EXTRA_PANELS
	DrawFileDialogs();
#endif

#if MAHO_EDITOR_DEMO_CONTENT
	if (bShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&bShowDemoWindow);
	}
	if (bShowImPlotDemo)
	{
		ImPlot::ShowDemoWindow(&bShowImPlotDemo);
	}
#endif
#endif
	// Count full UI draw passes after mount (dock layout + content browser included).
	if (bEditorMounted && EditorUiPresentedFrames < kEditorUiSettleFrames)
	{
		++EditorUiPresentedFrames;
	}
	return true;
}

void FEditorLayer::DrawMenuItems(FEngineBase& App, float RowH)
{
	// Full-row-height menu buttons (BeginMenu in a MenuBar only hits on text height).
	auto DrawTopLevelMenu = [&](const char* Id, const char* Label, auto&& FillMenu)
	{
		const ImVec2 LabelSize = ImGui::CalcTextSize(Label);
		const float PadX = 10.0f;
		const ImVec2 BtnSize(LabelSize.x + PadX * 2.0f, RowH);

		ImGui::PushID(Id);
		const ImVec2 P0 = ImGui::GetCursorScreenPos();
		const bool bPopupOpen = ImGui::IsPopupOpen(Id);
		if (ImGui::InvisibleButton("##Hit", BtnSize))
		{
			ImGui::OpenPopup(Id);
		}
		const bool bHovered = ImGui::IsItemHovered();

		if (bHovered || bPopupOpen)
		{
			const ImU32 Col = ImGui::GetColorU32(bPopupOpen ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered);
			ImGui::GetWindowDrawList()->AddRectFilled(P0, ImVec2(P0.x + BtnSize.x, P0.y + BtnSize.y), Col);
		}

		ImGui::GetWindowDrawList()->AddText(
			ImVec2(P0.x + PadX, P0.y + (RowH - LabelSize.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text),
			Label);

		// Align popup to the menu button's left edge (not mouse cursor).
		ImGui::SetNextWindowPos(ImVec2(P0.x, P0.y + BtnSize.y));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
		// Theme sets Separator alpha=0 for invisible dock gutters; menus need a visible line.
		ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.22f));
		if (ImGui::BeginPopup(Id))
		{
			FillMenu();
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
		ImGui::PopID();
		ImGui::SameLine(0.0f, 0.0f);
	};

	DrawTopLevelMenu("MenuFile", "File", [&]()
	{
#if MAHO_EDITOR_DEMO_CONTENT
		if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open..."))
		{
			IGFD::FileDialogConfig Config;
			Config.path = "Content";
			ImGuiFileDialog::Instance()->OpenDialog("EditorOpenDlg", "Open File", ".*", Config);
		}
		if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save As..."))
		{
			IGFD::FileDialogConfig Config;
			Config.path = "Content";
			ImGuiFileDialog::Instance()->OpenDialog("EditorSaveDlg", "Save File", ".*", Config);
		}
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE "  Refresh Content Browser", "F5"))
		{
			RefreshContentListing();
			AppendOutput("Content browser refreshed.");
		}
		ImGui::Separator();
#endif
		if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET "  Exit"))
		{
			if (GEngine)
			{
				GEngine->OnRequestExit();
			}
		}
		FEditorUIDrawContext Ctx = MakeUIDrawContext(App);
		UIRegistry.DrawMenuPopup("File", Ctx);
	});

	DrawTopLevelMenu("MenuWindow", "Window", [&]()
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(App);
		UIRegistry.DrawDockPanelMenuToggles(Ctx);
		ImGui::Separator();
		UIRegistry.DrawMenuPopup("Window", Ctx);
	});

	for (const FEditorUICatalog& Catalog : UIRegistry.GetMenuCatalogs())
	{
		if (Catalog.Name == "File" || Catalog.Name == "Window")
		{
			continue;
		}
		const std::string MenuName = Catalog.Name;
		const std::string MenuId = std::string("MenuDyn_") + MenuName;
		DrawTopLevelMenu(MenuId.c_str(), MenuName.c_str(), [&, MenuName]()
		{
			FEditorUIDrawContext Ctx = MakeUIDrawContext(App);
			UIRegistry.DrawMenuPopup(MenuName, Ctx);
		});
	}
}

void FEditorLayer::DrawBrandBlock(float Size)
{
	const ImVec4 BrandBg = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, BrandBg);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::BeginChild(
		"##EditorBrand",
		ImVec2(Size, Size),
		ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

	const char* Glyph = ICON_FA_CAT;
	ImGui::SetWindowFontScale(1.75f);
	const ImVec2 Scaled = ImGui::CalcTextSize(Glyph);
	const ImVec2 Region = ImGui::GetContentRegionAvail();
	ImGui::SetCursorPos(ImVec2(
		(Region.x - Scaled.x) * 0.5f,
		(Region.y - Scaled.y) * 0.5f));
	ImGui::TextUnformatted(Glyph);
	ImGui::SetWindowFontScale(1.0f);

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void FEditorLayer::DrawToolbarPrimary()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
	if (GEngine)
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(*GEngine);
		UIRegistry.DrawToolbar(EEditorUIRegion::ToolbarPrimary, Ctx);
	}
	ImGui::PopStyleVar();
}

void FEditorLayer::DrawToolbarSecondary()
{
	const float BtnH = ImGui::GetContentRegionAvail().y;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
	if (GEngine)
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(*GEngine);
		Ctx.ToolbarButtonSize = BtnH;
		UIRegistry.DrawToolbar(EEditorUIRegion::ToolbarSecondary, Ctx);
	}
	ImGui::PopStyleVar(2);
}

void FEditorLayer::EnsureDefaultDockLayout(std::uint32_t DockspaceId)
{
	ImGui::DockBuilderRemoveNode(DockspaceId);
	ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(DockspaceId, ImGui::GetContentRegionAvail());

	// Layout: Outliner (left 18%) | Main Viewport (center) | Bottom tabs (30%)
	ImGuiID DockMain = DockspaceId;
	ImGuiID DockLeft = ImGui::DockBuilderSplitNode(DockMain, ImGuiDir_Left, 0.18f, nullptr, &DockMain);
	ImGuiID DockBottom = ImGui::DockBuilderSplitNode(DockMain, ImGuiDir_Down, 0.30f, nullptr, &DockMain);

	ImGui::DockBuilderDockWindow(kWinOutliner, DockLeft);
	ImGui::DockBuilderDockWindow(kWinInspector, DockLeft);
	ImGui::DockBuilderDockWindow(kWinMainViewport, DockMain);
	ImGui::DockBuilderDockWindow(kWinContent, DockBottom);
	ImGui::DockBuilderDockWindow(kWinOutput, DockBottom);
	ImGui::DockBuilderDockWindow(kWinAgent, DockBottom);
	ImGui::DockBuilderDockWindow(kWinWallpaper, DockBottom);
	ImGui::DockBuilderDockWindow(kWinSequenceGraph, DockBottom);
	if (ImGuiDockNode* Central = ImGui::DockBuilderGetNode(DockMain))
	{
		Central->SetLocalFlags(static_cast<ImGuiDockNodeFlags>(
			static_cast<int>(Central->LocalFlags)
			| static_cast<int>(ImGuiDockNodeFlags_NoTabBar)
			| static_cast<int>(ImGuiDockNodeFlags_NoUndocking)));
	}
	if (ImGuiDockNode* Left = ImGui::DockBuilderGetNode(DockLeft))
	{
		Left->SetLocalFlags(static_cast<ImGuiDockNodeFlags>(
			static_cast<int>(Left->LocalFlags)
			| static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
			| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton)));
	}
	if (ImGuiDockNode* Bottom = ImGui::DockBuilderGetNode(DockBottom))
	{
		Bottom->SetLocalFlags(static_cast<ImGuiDockNodeFlags>(
			static_cast<int>(Bottom->LocalFlags)
			| static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
			| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton)));
	}
#if MAHO_EDITOR_EXTRA_PANELS
	ImGuiID DockRight = ImGui::DockBuilderSplitNode(DockMain, ImGuiDir_Right, 0.28f, nullptr, &DockMain);
	ImGuiID DockBottomRight = ImGui::DockBuilderSplitNode(DockBottom, ImGuiDir_Right, 0.45f, nullptr, &DockBottom);
	ImGui::DockBuilderDockWindow(kWinBlueprint, DockRight);
	ImGui::DockBuilderDockWindow(kWinPlot, DockBottomRight);
	ImGui::DockBuilderDockWindow(kWinContent, DockBottom);
	ImGui::DockBuilderDockWindow(kWinOutput, DockBottom);
	ImGui::DockBuilderDockWindow(kWinAgent, DockBottom);
	ImGui::DockBuilderDockWindow(kWinSequenceGraph, DockBottom);
	if (ImGuiDockNode* Central = ImGui::DockBuilderGetNode(DockMain))
	{
		Central->SetLocalFlags(static_cast<ImGuiDockNodeFlags>(
			static_cast<int>(Central->LocalFlags)
			| static_cast<int>(ImGuiDockNodeFlags_NoTabBar)
			| static_cast<int>(ImGuiDockNodeFlags_NoUndocking)));
	}
#endif
	ImGui::DockBuilderFinish(DockspaceId);
}

void FEditorLayer::DrawDockSpace(FEngineBase& App)
{
	ImGuiViewport* Viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(Viewport->WorkPos);
	ImGui::SetNextWindowSize(Viewport->WorkSize);
	ImGui::SetNextWindowViewport(Viewport->ID);

	const float OuterPad = 8.0f;
	const ImVec4 DockChassis = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	const ImVec4 ChromeBg = DockChassis;
	const ImVec4 PlaceholderBg = ImVec4(38.0f / 255.0f, 39.0f / 255.0f, 43.0f / 255.0f, 1.0f);
	const ImVec4 MenuHover = ImVec4(52.0f / 255.0f, 54.0f / 255.0f, 60.0f / 255.0f, 1.0f);
	const ImVec4 MenuActive = ImVec4(66.0f / 255.0f, 70.0f / 255.0f, 78.0f / 255.0f, 1.0f);
	ImGuiWindowFlags RootFlags =
		ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, DockChassis);
	ImGui::Begin("##EditorRoot", nullptr, RootFlags);
	ImGui::PopStyleVar(4);

	ImGuiStyle& Style = ImGui::GetStyle();
	const ImVec2 ThemeFramePadding = Style.FramePadding;
	// Brand is the vertical reference; menu and toolbar are each exactly half its height.
	const float BrandSize = ImMax(56.0f, ImGui::GetFrameHeight() * 2.0f + ThemeFramePadding.y * 2.0f);
	const float MenuRowH = BrandSize * 0.5f;
	const float ToolbarHeight = BrandSize * 0.5f;
	// Light-gray reserved strip under brand+toolbar.
	const float PlaceholderH = MenuRowH;

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));

	// Brand spans menu + toolbar rows; menu and toolbar sit to its right.
	DrawBrandBlock(BrandSize);
	ImGui::SameLine(0.0f, 0.0f);

	// Kill MenuBar bottom hairline for the whole header (drawn with Border * FrameBorderSize).
	const float BackupFrameBorderSize = Style.FrameBorderSize;
	const ImVec4 BackupHeaderBorder = Style.Colors[ImGuiCol_Border];
	Style.FrameBorderSize = 0.0f;
	Style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ChromeBg);
	ImGui::BeginChild(
		"##EditorHeaderRight",
		ImVec2(0.0f, BrandSize),
		ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

	// Row 1: main menu bar — full-height buttons (half brand height).
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ChromeBg);
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, MenuHover);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, MenuActive);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::BeginChild(
		"##EditorMenuRow",
		ImVec2(0.0f, MenuRowH),
		ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
	DrawMenuItems(App, MenuRowH);
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(4);

	// Row 2: pinned toolbar — exact remaining half of the brand height.
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ChromeBg);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::BeginChild(
		"##EditorToolbar",
		ImVec2(0.0f, ToolbarHeight),
		ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
	{
		const float Y = (ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight()) * 0.5f;
		if (Y > 0.0f)
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Y);
		}
		DrawToolbarPrimary();
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	ImGui::EndChild(); // ##EditorHeaderRight
	ImGui::PopStyleColor();

	Style.FrameBorderSize = BackupFrameBorderSize;
	Style.Colors[ImGuiCol_Border] = BackupHeaderBorder;

	ImGui::PopStyleVar(7);

	// Toolbar 2: light-gray strip inset like the main dock (same OuterPad seam).
	ImGui::SetCursorPos(ImVec2(OuterPad, ImGui::GetCursorPosY() + OuterPad));
	{
		const float Toolbar2W = ImGui::GetContentRegionAvail().x - OuterPad;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, PlaceholderBg);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::BeginChild(
			"##EditorToolbar2",
			ImVec2(Toolbar2W, PlaceholderH),
			ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
		DrawToolbarSecondary();
		ImGui::EndChild();
		ImGui::PopStyleVar(4);
		ImGui::PopStyleColor();
	}

	// Fixed main docking space for all editor windows
	ImGui::SetCursorPos(ImVec2(OuterPad, ImGui::GetCursorPosY() + OuterPad));
	const ImVec2 DockAvail = ImGui::GetContentRegionAvail();
	const bool bHasWallpaper = WallpaperTexture.IsValid();
	ImGui::PushStyleColor(
		ImGuiCol_ChildBg,
		bHasWallpaper ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f) : DockChassis);
	ImGui::BeginChild(
		"##EditorDockHost",
		ImVec2(DockAvail.x - OuterPad, DockAvail.y - OuterPad),
		ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

	if (bHasWallpaper)
	{
		const ImVec2 HostMin = ImGui::GetWindowPos();
		const ImVec2 HostSize = ImGui::GetWindowSize();
		ImGui::GetWindowDrawList()->AddImage(
			reinterpret_cast<ImTextureID>(WallpaperTexture.Id),
			HostMin,
			ImVec2(HostMin.x + HostSize.x, HostMin.y + HostSize.y));
	}

	const ImVec4 BackupBorder = Style.Colors[ImGuiCol_Border];
	Style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

	const ImGuiID DockspaceId = ImGui::GetID("MahoEditorMainDock_v5");
	if (bBuildDefaultLayout)
	{
		EnsureDefaultDockLayout(DockspaceId);
		bBuildDefaultLayout = false;
	}
	ImGui::DockSpace(DockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_NoDockingOverCentralNode);

	Style.Colors[ImGuiCol_Border] = BackupBorder;
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::End();
	ImGui::PopStyleColor();
}

void FEditorLayer::DrawMainViewportPanel()
{
	// Locked into the central dock: no tab bar, cannot undock into a floating window.
	ImGuiWindowClass ViewportClass;
	ViewportClass.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoTabBar) | static_cast<int>(ImGuiDockNodeFlags_NoUndocking));
	ImGui::SetNextWindowClass(&ViewportClass);
	ImGui::Begin(kWinMainViewport, nullptr, ImGuiWindowFlags_NoCollapse);

	const ImVec2 Canvas = ImGui::GetContentRegionAvail();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Origin, ImVec2(Origin.x + Canvas.x, Origin.y + Canvas.y), IM_COL32(8, 9, 11, 255));

	FImGuiTextureHandle GameViewTex;
	std::uint32_t GameViewW = 0;
	std::uint32_t GameViewH = 0;
	if (GEngine)
	{
		if (FRenderSystem* Render = GEngine->GetExtension<FRenderSystem>())
		{
			GameViewTex = Render->GetGameViewImGuiTexture();
			GameViewW = Render->GetGameViewWidth();
			GameViewH = Render->GetGameViewHeight();
		}
	}

	// Grid / gizmo under the game view so they cannot cover the RT Image.
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(Origin.x, Origin.y, Canvas.x, Canvas.y);
	if (Canvas.x > 1.0f && Canvas.y > 1.0f)
	{
		PerspectiveRH(ProjectionMatrix, 45.0f * 3.14159265f / 180.0f, Canvas.x / Canvas.y, 0.1f, 100.0f);
		ImGuizmo::DrawGrid(ViewMatrix, ProjectionMatrix, ObjectMatrix, 10.0f);
		if (ViewportTool != EViewportTool::Select)
		{
			ImGuizmo::Manipulate(
				ViewMatrix,
				ProjectionMatrix,
				static_cast<ImGuizmo::OPERATION>(GizmoOperation),
				ImGuizmo::LOCAL,
				ObjectMatrix);
		}
	}

	if (GameViewTex.IsValid() && GameViewW > 0 && GameViewH > 0 && Canvas.x > 1.0f && Canvas.y > 1.0f)
	{
		const float Scale = ImMin(
			Canvas.x / static_cast<float>(GameViewW),
			Canvas.y / static_cast<float>(GameViewH));
		const ImVec2 ImageSize(
			static_cast<float>(GameViewW) * Scale,
			static_cast<float>(GameViewH) * Scale);
		const ImVec2 ImageOrigin(
			Origin.x + (Canvas.x - ImageSize.x) * 0.5f,
			Origin.y + (Canvas.y - ImageSize.y) * 0.5f);
		// DrawList path keeps this above prior ImGuizmo primitives in the same window.
		DrawList->AddImage(
			reinterpret_cast<ImTextureID>(GameViewTex.Id),
			ImageOrigin,
			ImVec2(ImageOrigin.x + ImageSize.x, ImageOrigin.y + ImageSize.y));
	}

	ImGui::SetCursorScreenPos(Origin);
	ImGui::Dummy(Canvas);

	// Accept asset drop from Content Browser → spawn entity in world
	if (ImGui::BeginDragDropTarget() && GEngine)
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
		{
			const char* CatalogKey = static_cast<const char*>(Payload->Data);
			if (CatalogKey && CatalogKey[0] != '\0')
			{
				if (FGameWorldLayer* WL = GEngine->GetExtension<FGameWorldLayer>())
				{
					FWorld& World = WL->GetWorld();
					FEntityManager& Mgr = World.GetEntityManager();

					ComponentMaskType Mask = MakeComponentMask<FTransformComponent, FStaticMeshComponent>();
					FEntityHandle NewEntity = Mgr.CreateEntity(Mask);

					FTransformComponent* T = Mgr.GetComponent<FTransformComponent>(NewEntity);
											if (T)
						{
							T->SetIdentity();
						}

					FStaticMeshComponent* SM = Mgr.GetComponent<FStaticMeshComponent>(NewEntity);
					if (SM)
					{
						std::strncpy(SM->MeshPath, CatalogKey, ECSComponentAssetPathMax - 1);
						SM->MeshPath[ECSComponentAssetPathMax - 1] = '\0';
					}

					// Auto-select so the Inspector immediately shows the new entity
					SelectedEntity = NewEntity;
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (GEngine)
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(*GEngine);
		UIRegistry.DrawViewportOverlays(Ctx);
	}
	ImGui::End();
}

void FEditorLayer::DrawSceneOutliner()
{
	if (!BeginEditorDockPanel(kWinOutliner, &bShowOutliner))
	{
		ImGui::End();
		return;
	}

	if (!GEngine)
	{
		ImGui::TextDisabled("App not ready.");
		ImGui::End();
		return;
	}

	FGameWorldLayer* WorldLayer = GEngine->GetExtension<FGameWorldLayer>();
	if (!WorldLayer)
	{
		ImGui::TextDisabled("No WorldLayer loaded.");
		ImGui::End();
		return;
	}

	FWorld& ECSWorld = WorldLayer->GetWorld();
	FEntityManager& Mgr = ECSWorld.GetEntityManager();

	// ── Delete key handling ──
		if (SelectedEntity.IsValid() && ImGui::IsKeyPressed(ImGuiKey_Delete) && ImGui::IsWindowFocused())
		{
			Mgr.DestroyEntity(SelectedEntity);
			SelectedEntity = FEntityHandle{};
		}

	// ── World root tree node ──
	ImGuiTreeNodeFlags RootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed;

	if (ImGui::TreeNodeEx("World", RootFlags))
	{
		// ── Entities ──
		ImGuiTreeNodeFlags EntitiesFlags = ImGuiTreeNodeFlags_DefaultOpen;
		int EntitiesId = 1000;

		if (ImGui::TreeNodeEx("Entities", EntitiesFlags, "%s", ICON_FA_CUBES " Entities"))
		{
			Mgr.ForEachEntity([&](FEntityHandle Handle)
			{
				const bool bIsCamera = Mgr.HasTag<Maho::FMainCameraTag>(Handle);

				char Label[128];
				std::snprintf(Label, sizeof(Label), "%sEntity %u (Gen %u)##Ent%u",
					bIsCamera ? ICON_FA_CAMERA " " : "", Handle.Index, Handle.Generation, ++EntitiesId);

				ImGuiTreeNodeFlags LeafFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
					| (Handle.Index == SelectedEntity.Index && Handle.Generation == SelectedEntity.Generation
						? ImGuiTreeNodeFlags_Selected : 0);

				ImGui::TreeNodeEx(Label, LeafFlags);
				if (ImGui::IsItemClicked())
				{
					SelectedEntity = Handle;
				}
			});
			ImGui::TreePop();
		}

		ImGui::TreePop();
	}
	ImGui::End();
}

void FEditorLayer::DrawInspectorPanel()
{
	if (!BeginEditorDockPanel(kWinInspector, &bShowInspector))
	{
		ImGui::End();
		return;
	}

	if (!GEngine)
	{
		ImGui::TextDisabled("App not ready.");
		ImGui::End();
		return;
	}

	FGameWorldLayer* WorldLayer = GEngine->GetExtension<FGameWorldLayer>();
	if (!WorldLayer)
	{
		ImGui::TextDisabled("No WorldLayer loaded.");
		ImGui::End();
		return;
	}

	FWorld& ECSWorld = WorldLayer->GetWorld();
	FEntityManager& Mgr = ECSWorld.GetEntityManager();

	if (!SelectedEntity.IsValid() || !Mgr.IsValid(SelectedEntity))
	{
		ImGui::TextDisabled("Select an entity in the Outliner.");
		ImGui::End();
		return;
	}

	ComponentMaskType Mask = Mgr.GetComponentMask(SelectedEntity);
	bool bIsPersistent = Mgr.HasTag<Maho::FMainCameraTag>(SelectedEntity);

	char Header[128];
	std::snprintf(Header, sizeof(Header), "%sEntity %u  (Gen %u)",
		bIsPersistent ? ICON_FA_THUMBTACK " " : "", SelectedEntity.Index, SelectedEntity.Generation);
	ImGui::TextUnformatted(Header);
	ImGui::Separator();

	// ═══════════════════════════════════════════
	// Add Component button
	// ═══════════════════════════════════════════
	if (ImGui::Button("+ Add Component"))
		ImGui::OpenPopup("AddComponentPopup");
	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		struct FCompEntry { const char* Name; std::size_t TypeId; };
		FCompEntry Entries[] = {
			{"Static Mesh",  GetComponentTypeId<FStaticMeshComponent>()},
			{"Skeleton",     GetComponentTypeId<FSkeletonComponent>()},
			{"Animation",    GetComponentTypeId<FAnimationComponent>()},
			{"Camera",       GetComponentTypeId<FCameraComponent>()},
			{"Material",     GetComponentTypeId<FMaterialComponent>()},
			{"Script",       GetComponentTypeId<FScriptComponent>()},
		};
		for (const auto& E : Entries)
		{
			bool bHas = (Mgr.GetComponentMask(SelectedEntity).test(E.TypeId));
			if (bHas)
			{
				ImGui::BeginDisabled();
				ImGui::MenuItem(E.Name);
				ImGui::EndDisabled();
			}
			else if (ImGui::MenuItem(E.Name))
			{
				// Add component deferred
				if (E.TypeId == GetComponentTypeId<FStaticMeshComponent>())
					Mgr.AddComponent(SelectedEntity, FStaticMeshComponent{});
				else if (E.TypeId == GetComponentTypeId<FSkeletonComponent>())
					Mgr.AddComponent(SelectedEntity, FSkeletonComponent{});
				else if (E.TypeId == GetComponentTypeId<FAnimationComponent>())
					Mgr.AddComponent(SelectedEntity, FAnimationComponent{});
				else if (E.TypeId == GetComponentTypeId<FCameraComponent>())
					Mgr.AddComponent(SelectedEntity, FCameraComponent{});
				else if (E.TypeId == GetComponentTypeId<FMaterialComponent>())
					Mgr.AddComponent(SelectedEntity, FMaterialComponent{});
				else if (E.TypeId == GetComponentTypeId<FScriptComponent>())
					Mgr.AddComponent(SelectedEntity, FScriptComponent{});
			}
		}
		ImGui::EndPopup();
	}

	ImGui::Separator();

	// ═══════════════════════════════════════════
	// FTransformComponent
	// ═══════════════════════════════════════════
	if (Mask.test(GetComponentTypeId<FTransformComponent>()))
	{
		if (ImGui::CollapsingHeader(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform", ImGuiTreeNodeFlags_DefaultOpen))
		{
			FTransformComponent* T = Mgr.GetComponent<FTransformComponent>(SelectedEntity);
			if (T)
			{
				float Pos[3] = { T->Position.x, T->Position.y, T->Position.z };
				if (ImGui::DragFloat3("Position", Pos, 0.1f))
				{
					T->Position = glm::vec3(Pos[0], Pos[1], Pos[2]);
				}
				float Scale[3] = { T->Scale.x, T->Scale.y, T->Scale.z };
				if (ImGui::DragFloat3("Scale", Scale, 0.01f, 0.01f, 100.0f))
				{
					T->Scale = glm::vec3(Scale[0], Scale[1], Scale[2]);
				}
			}
		}
	}

	// ═══════════════════════════════════════════
	// FStaticMeshComponent
	// ═══════════════════════════════════════════
	if (Mask.test(GetComponentTypeId<FStaticMeshComponent>()))
	{
		if (ImGui::CollapsingHeader(ICON_FA_CUBE " Static Mesh", ImGuiTreeNodeFlags_DefaultOpen))
		{
			FStaticMeshComponent* C = Mgr.GetComponent<FStaticMeshComponent>(SelectedEntity);
			if (C)
			{
				ImGui::InputText("Mesh Path", C->MeshPath, ECSComponentAssetPathMax);
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
					{
							const char* Path = static_cast<const char*>(Payload->Data);
							if (Path)
							{
								std::strncpy(C->MeshPath, Path, ECSComponentAssetPathMax - 1);
							}
					}
					ImGui::EndDragDropTarget();
				}
			}
		}
	}

	// ═══════════════════════════════════════════
	// FSkeletonComponent
	// ═══════════════════════════════════════════
	if (Mask.test(GetComponentTypeId<FSkeletonComponent>()))
	{
		if (ImGui::CollapsingHeader(ICON_FA_BONE " Skeleton"))
		{
			FSkeletonComponent* C = Mgr.GetComponent<FSkeletonComponent>(SelectedEntity);
			if (C)
			{
				ImGui::InputText("Skeleton Path", C->SkeletonPath, ECSComponentAssetPathMax);
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
					{
							const char* Path = static_cast<const char*>(Payload->Data);
							if (Path)
							{
								std::strncpy(C->SkeletonPath, Path, ECSComponentAssetPathMax - 1);
							}
					}
					ImGui::EndDragDropTarget();
				}
			}
		}
	}

	// ═══════════════════════════════════════════
	// FAnimationComponent
	// ═══════════════════════════════════════════
	if (Mask.test(GetComponentTypeId<FAnimationComponent>()))
	{
		if (ImGui::CollapsingHeader(ICON_FA_FILM " Animation"))
		{
			FAnimationComponent* C = Mgr.GetComponent<FAnimationComponent>(SelectedEntity);
			if (C)
			{
				ImGui::InputText("Clip Path", C->AnimationClipPath, ECSComponentAssetPathMax);
				ImGui::DragFloat("Time", &C->Time, 0.01f);
				ImGui::DragFloat("Speed", &C->Speed, 0.1f);
				ImGui::Checkbox("Loop", &C->bLoop);
				ImGui::Checkbox("Playing", &C->bPlaying);
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
					{
							const char* Path = static_cast<const char*>(Payload->Data);
							if (Path)
							{
								std::strncpy(C->AnimationClipPath, Path, ECSComponentAssetPathMax - 1);
							}
					}
					ImGui::EndDragDropTarget();
				}
			}
		}
	}

	// ═══════════════════════════════════════════
	// FCameraComponent
	// ═══════════════════════════════════════════
	if (Mask.test(GetComponentTypeId<FCameraComponent>()))
	{
		if (ImGui::CollapsingHeader(ICON_FA_CAMERA " Camera", ImGuiTreeNodeFlags_DefaultOpen))
		{
			FCameraComponent* C = Mgr.GetComponent<FCameraComponent>(SelectedEntity);
			if (C)
			{
				ImGui::DragFloat("FOV", &C->FOV, 1.0f, 1.0f, 179.0f);
				ImGui::DragFloat("Near Plane", &C->NearPlane, 0.01f, 0.001f, C->FarPlane - 0.001f);
				ImGui::DragFloat("Far Plane", &C->FarPlane, 1.0f, C->NearPlane + 0.01f, 10000.0f);
				ImGui::Checkbox("Main Camera", &C->bMainCamera);
				ImGui::Checkbox("Orthographic", &C->bOrthographic);
					if (C->bOrthographic)
					{
						ImGui::DragFloat("Ortho Size", &C->OrthoSize, 0.1f);
					}
			}
		}
	}

	// ═══════════════════════════════════════════
	// FMaterialComponent
	// ═══════════════════════════════════════════
	if (Mask.test(GetComponentTypeId<FMaterialComponent>()))
	{
		if (ImGui::CollapsingHeader(ICON_FA_PALETTE " Material"))
		{
			FMaterialComponent* C = Mgr.GetComponent<FMaterialComponent>(SelectedEntity);
			if (C)
			{
				ImGui::InputText("Shader Path", C->ShaderPath, ECSComponentAssetPathMax);
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
					{
							const char* Path = static_cast<const char*>(Payload->Data);
							if (Path)
							{
								std::strncpy(C->ShaderPath, Path, ECSComponentAssetPathMax - 1);
							}
					}
					ImGui::EndDragDropTarget();
				}

				if (C->OverrideCount > 0)
				{
					ImGui::SeparatorText("Property Overrides");
					for (std::uint32_t I = 0; I < C->OverrideCount; ++I)
					{
						ImGui::PushID(static_cast<int>(I));
						ImGui::TextUnformatted(C->Overrides[I].Name);
						ImGui::SameLine(80);
						char Label[32];
						std::snprintf(Label, sizeof(Label), "##Prop%d", I);
						ImGui::DragFloat4(Label, C->Overrides[I].Data, 0.01f);
						ImGui::PopID();
					}
				}

				if (C->TextureOverrideCount > 0)
				{
					ImGui::SeparatorText("Texture Overrides");
					for (std::uint32_t I = 0; I < C->TextureOverrideCount; ++I)
					{
						ImGui::PushID(static_cast<int>(I + 100));
						ImGui::TextUnformatted(C->TextureOverrides[I].Name);
						ImGui::SameLine(80);
						ImGui::TextUnformatted(C->TextureOverrides[I].Path);
						ImGui::PopID();
					}
				}
			}
		}
	}

	// ═══════════════════════════════════════════
	// FScriptComponent
	// ═══════════════════════════════════════════
	if (Mask.test(GetComponentTypeId<FScriptComponent>()))
	{
		if (ImGui::CollapsingHeader(ICON_FA_CODE " Script"))
		{
			FScriptComponent* C = Mgr.GetComponent<FScriptComponent>(SelectedEntity);
			if (C)
			{
				ImGui::InputText("Script Path", C->ScriptPath, ECSComponentAssetPathMax);
				ImGui::Checkbox("Enabled", &C->bEnabled);

				if (C->ParamCount > 0)
				{
					ImGui::SeparatorText("Parameters");
					for (std::uint32_t I = 0; I < C->ParamCount; ++I)
					{
						ImGui::PushID(static_cast<int>(I + 200));
						ImGui::TextUnformatted(C->Params[I].Key);
						ImGui::SameLine(100);
						char ValLabel[64];
						std::snprintf(ValLabel, sizeof(ValLabel), "##Param%d", I);
						ImGui::InputText(ValLabel, C->Params[I].Value, ECSComponentAssetPathMax);
						ImGui::PopID();
					}
				}
			}
		}
	}

	ImGui::End();
}

void FEditorLayer::DrawWallpaperPanel()
{
	if (!BeginEditorDockPanel(kWinWallpaper, &bShowWallpaperPanel))
	{
		bWallpaperDropRectValid = false;
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted("Desktop wallpaper");
	ImGui::TextDisabled("Drag an image file from the OS onto the preview below.");
	if (!WallpaperSourcePath.empty())
	{
		ImGui::TextWrapped("%s", WallpaperSourcePath.c_str());
	}
	if (ImGui::Button("Clear Wallpaper") && GEngine)
	{
		ClearWallpaper(*GEngine);
	}

	ImGui::Separator();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const ImVec2 PreviewSize(ImMax(Avail.x, 64.0f), ImMax(Avail.y, 64.0f));
	const ImVec2 Cursor = ImGui::GetCursorScreenPos();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(
		Cursor,
		ImVec2(Cursor.x + PreviewSize.x, Cursor.y + PreviewSize.y),
		IM_COL32(20, 21, 24, 255));

	if (WallpaperTexture.IsValid())
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(WallpaperTexture.Id), PreviewSize);
	}
	else
	{
		ImGui::InvisibleButton("##WallpaperDropTarget", PreviewSize);
		const ImVec2 TextSize = ImGui::CalcTextSize("Drop image here");
		DrawList->AddText(
			ImVec2(
				Cursor.x + (PreviewSize.x - TextSize.x) * 0.5f,
				Cursor.y + (PreviewSize.y - TextSize.y) * 0.5f),
			IM_COL32(160, 164, 172, 255),
			"Drop image here");
	}

	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	WallpaperDropMinX = Min.x;
	WallpaperDropMinY = Min.y;
	WallpaperDropMaxX = Max.x;
	WallpaperDropMaxY = Max.y;
	bWallpaperDropRectValid = (Max.x > Min.x) && (Max.y > Min.y);

	if (bWallpaperDropRectValid && ImGui::IsItemHovered())
	{
		DrawList->AddRect(Min, Max, IM_COL32(70, 148, 235, 220), 0.0f, 0, 2.0f);
	}

	ImGui::End();
}

void FEditorLayer::ClearWallpaper(FEngineBase& App)
{
	WallpaperSourcePath.clear();
	bWallpaperDropRectValid = false;
	if (!WallpaperTexture.IsValid())
	{
		return;
	}

	if (FRenderSystem* Render = App.GetExtension<FRenderSystem>())
	{
		Render->GetImGui().DestroyTexture(
			Render->GetRHIServer(),
			WallpaperTexture);
	}
	else
	{
		WallpaperTexture.Reset();
	}
}

std::string FEditorLayer::ResolveDefaultWallpaperPath()
{
	namespace fs = std::filesystem;
	constexpr const char* kFileName = "DefaultWallpaper.webp";

	std::error_code ErrorCode;
	const fs::path Candidates[] = {
		fs::current_path() / "Engine" / "Editor" / kFileName,
		fs::path(MAHO_ENGINE_ROOT) / "Maho" / "Content" / "Editor" / kFileName,
	};

#if defined(_WIN32)
	{
		HMODULE Module = nullptr;
		if (GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&FEditorLayer::ResolveDefaultWallpaperPath),
				&Module)
			&& Module != nullptr)
		{
			wchar_t Buffer[MAX_PATH]{};
			const DWORD Length = GetModuleFileNameW(Module, Buffer, MAX_PATH);
			if (Length > 0 && Length < MAX_PATH)
			{
				const fs::path BesideBinary =
					fs::path(Buffer).parent_path() / "Engine" / "Editor" / kFileName;
				if (fs::exists(BesideBinary, ErrorCode) && !ErrorCode)
				{
					return BesideBinary.string();
				}
			}
		}
	}
#endif

	for (const fs::path& Candidate : Candidates)
	{
		const fs::path Normalized = Candidate.lexically_normal();
		if (fs::exists(Normalized, ErrorCode) && !ErrorCode)
		{
			return Normalized.string();
		}
	}
	return {};
}

void FEditorLayer::EnsureDefaultWallpaper(FEngineBase& App)
{
	if (bDefaultWallpaperAttempted || WallpaperTexture.IsValid())
	{
		return;
	}
	bDefaultWallpaperAttempted = true;

	const std::string Path = ResolveDefaultWallpaperPath();
	if (Path.empty())
	{
		MAHO_CORE_WARN("Editor: default wallpaper missing (Engine/Editor/DefaultWallpaper.webp)");
		return;
	}

	TryApplyWallpaperFromPath(App, Path);
}

bool FEditorLayer::TryApplyWallpaperFromPath(FEngineBase& App, const std::string& Path)
{
	const std::string Ext = TextureImageCodec::GetExtensionLower(Path);
	if (!TextureImageCodec::IsRasterExtension(Ext) && !TextureImageCodec::IsKtx2Extension(Ext))
	{
		AppendOutput("Wallpaper: unsupported image type '" + Ext + "'", spdlog::level::warn);
		return false;
	}

	std::ifstream File(PathFromUtf8(Path), std::ios::binary);
	if (!File)
	{
		AppendOutput("Wallpaper: failed to open '" + Path + "'", spdlog::level::err);
		return false;
	}
	File.seekg(0, std::ios::end);
	const std::streamoff EndPos = File.tellg();
	if (EndPos <= 0)
	{
		AppendOutput("Wallpaper: empty file '" + Path + "'", spdlog::level::err);
		return false;
	}
	File.seekg(0, std::ios::beg);
	std::vector<std::uint8_t> Bytes(static_cast<std::size_t>(EndPos));
	if (!File.read(reinterpret_cast<char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size())))
	{
		AppendOutput("Wallpaper: failed to read '" + Path + "'", spdlog::level::err);
		return false;
	}

	FDecodedImage Decoded;
	if (!TextureImageCodec::DecodeFromMemory(Bytes.data(), Bytes.size(), Path, Decoded))
	{
		AppendOutput("Wallpaper: decode failed '" + Path + "'", spdlog::level::err);
		return false;
	}
	if (Decoded.Dimension != ETextureDimension::Tex2D
		|| Decoded.Format != ETexturePixelFormat::RGBA8
		|| Decoded.Width == 0
		|| Decoded.Height == 0
		|| Decoded.Pixels.empty())
	{
		AppendOutput("Wallpaper: need RGBA8 2D image '" + Path + "'", spdlog::level::err);
		return false;
	}

	FRenderSystem* Render = App.GetExtension<FRenderSystem>();
	if (!Render)
	{
		AppendOutput("Wallpaper: RenderSystem unavailable", spdlog::level::err);
		return false;
	}

	FImGuiSystem& ImGuiSys = Render->GetImGui();
	FRHIServer& RHIServer = Render->GetRHIServer();
	if (WallpaperTexture.IsValid())
	{
		ImGuiSys.DestroyTexture(RHIServer, WallpaperTexture);
	}

	FImGuiTextureHandle NewHandle;
	if (!ImGuiSys.CreateRgba8Texture(
			RHIServer,
			Decoded.Width,
			Decoded.Height,
			Decoded.Pixels.data(),
			Decoded.Pixels.size(),
			NewHandle))
	{
		AppendOutput("Wallpaper: GPU upload failed '" + Path + "'", spdlog::level::err);
		return false;
	}

	WallpaperTexture = NewHandle;
	WallpaperSourcePath = Path;
	AppendOutput("Wallpaper applied: " + Path);
	return true;
}

void FEditorLayer::ProcessEditorFileDrops(FEngineBase& App)
{
	FPlatformSystem* Platform = App.GetExtension<FPlatformSystem>();
	if (!Platform || !Platform->GetWindow())
	{
		return;
	}

	std::vector<std::string> Dropped;
	Platform->GetWindow()->DrainDroppedFilePaths(Dropped);
	if (Dropped.empty())
	{
		return;
	}

	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	const bool bOverWallpaper =
		bWallpaperDropRectValid
		&& Mouse.x >= WallpaperDropMinX
		&& Mouse.y >= WallpaperDropMinY
		&& Mouse.x <= WallpaperDropMaxX
		&& Mouse.y <= WallpaperDropMaxY;
	const bool bOverContent =
		bContentBrowserDropRectValid
		&& Mouse.x >= ContentBrowserDropMinX
		&& Mouse.y >= ContentBrowserDropMinY
		&& Mouse.x <= ContentBrowserDropMaxX
		&& Mouse.y <= ContentBrowserDropMaxY;

	if (bOverWallpaper)
	{
		for (const std::string& Path : Dropped)
		{
			if (TryApplyWallpaperFromPath(App, Path))
			{
				break;
			}
		}
		return;
	}

	if (!bOverContent || ImporterDialog.bOpen || bManualImportActive)
	{
		return;
	}

	for (const std::string& Path : Dropped)
	{
		if (CanImportSourcePathLocal(Path))
		{
			OpenImporterDialog(Path);
			break;
		}
	}
}

namespace
{

[[nodiscard]] const char* ResourceTypeLabel(EResourceType Type)
{
	switch (Type)
	{
	case EResourceType::Texture2D:
		return "Texture2D";
	case EResourceType::Texture3D:
		return "Texture3D";
	case EResourceType::TextureCube:
		return "TextureCube";
	case EResourceType::TextureCubeArray:
		return "TextureCubeArray";
	case EResourceType::Texture2DArray:
		return "Texture2DArray";
	case EResourceType::Mesh:
		return "StaticMesh";
	case EResourceType::Material:
		return "Material";
	case EResourceType::Skeleton:
		return "Skeleton";
	case EResourceType::Animation:
		return "Animation";
	case EResourceType::AnimationGraph:
		return "AnimationGraph";
	case EResourceType::Prefab:
		return "Prefab";
	default:
		return "Unknown";
	}
}

[[nodiscard]] const char* ResourceTypeGlyph(EResourceType Type)
{
	switch (Type)
	{
	case EResourceType::Texture2D:
	case EResourceType::Texture3D:
	case EResourceType::TextureCube:
	case EResourceType::TextureCubeArray:
	case EResourceType::Texture2DArray:
		return ICON_FA_FILE_IMAGE;
	case EResourceType::Mesh:
		return ICON_FA_CUBE;
	case EResourceType::Material:
		return ICON_FA_PALETTE;
	case EResourceType::Skeleton:
		return ICON_FA_BONE;
	case EResourceType::Animation:
	case EResourceType::AnimationGraph:
		return ICON_FA_PERSON;
	case EResourceType::Prefab:
		return ICON_FA_DIAGRAM_PROJECT;
	default:
		return ICON_FA_FILE;
	}
}

[[nodiscard]] ImU32 ResourceTypeGlyphColor(EResourceType Type)
{
	switch (Type)
	{
	case EResourceType::Texture2D:
	case EResourceType::Texture3D:
	case EResourceType::TextureCube:
	case EResourceType::TextureCubeArray:
	case EResourceType::Texture2DArray:
		return IM_COL32(230, 140, 220, 255);
	case EResourceType::Mesh:
		return IM_COL32(120, 190, 255, 255);
	case EResourceType::Material:
		return IM_COL32(255, 180, 100, 255);
	case EResourceType::Skeleton:
		return IM_COL32(200, 210, 180, 255);
	case EResourceType::Animation:
	case EResourceType::AnimationGraph:
		return IM_COL32(140, 230, 170, 255);
	case EResourceType::Prefab:
		return IM_COL32(180, 160, 255, 255);
	default:
		return IM_COL32(199, 209, 224, 255);
	}
}

[[nodiscard]] const char* LoadStateLabel(EResourceLoadState State)
{
	switch (State)
	{
	case EResourceLoadState::Pending:
		return "Pending";
	case EResourceLoadState::Ready:
		return "Ready";
	case EResourceLoadState::Failed:
		return "Failed";
	default:
		return "Invalid";
	}
}

[[nodiscard]] std::string StripFileExtension(std::string Path)
{
	const std::size_t Slash = Path.find_last_of('/');
	const std::size_t Dot = Path.find_last_of('.');
	if (Dot != std::string::npos && (Slash == std::string::npos || Dot > Slash))
	{
		Path.resize(Dot);
	}
	return Path;
}

/** Parent of "/Game/Textures/Brick" → "/Game/Textures"; of "/Game" → "". */
[[nodiscard]] std::string VirtualPathParent(const std::string& Path)
{
	if (Path.empty() || Path == "/")
	{
		return {};
	}
	const std::size_t Slash = Path.find_last_of('/');
	if (Slash == std::string::npos || Slash == 0)
	{
		return {};
	}
	return Path.substr(0, Slash);
}

} // namespace

bool FEditorLayer::IsContentBrowserInputLocked() const
{
	FResourceSystem* Resources = Detail::GetResourceSystem();
	const bool bSaving = Resources && Resources->IsSavePackageBusy();
	return bContentBrowserRefreshing || bManualImportActive || bSaving;
}

void FEditorLayer::CollectChildFolders(
	const std::string& ParentVirtualPath,
	std::vector<std::string>& OutFolders) const
{
	OutFolders.clear();
	FResourceSystem* Resources = Detail::GetResourceSystem();
	if (!Resources)
	{
		return;
	}

	// Package path "/Game/Textures/Brick" → folders under /Game are "Textures";
	// the leaf "Brick" is the package (asset lives in parent folder), not a folder.
	const std::string Parent = FPaths::NormalizePackagePath(ParentVirtualPath);
	const std::string Prefix = Parent + "/";
	std::unordered_set<std::string> Unique;
	Resources->ForEachRegisteredResource(
		[&](const std::string& CatalogKey, FResource& /*Resource*/)
		{
			const std::string PackageName = FPaths::GetPackageName(CatalogKey);
			if (PackageName.rfind(Prefix, 0) != 0)
			{
				return;
			}
			const std::string Rest = PackageName.substr(Prefix.size());
			const std::size_t Slash = Rest.find('/');
			if (Slash == std::string::npos)
			{
				return;
			}
			const std::string Segment = Rest.substr(0, Slash);
			if (!Segment.empty())
			{
				Unique.insert(Prefix + Segment);
			}
		});

	OutFolders.assign(Unique.begin(), Unique.end());
	std::sort(OutFolders.begin(), OutFolders.end());
}

void FEditorLayer::DrawContentBrowser()
{
	ImGuiWindowClass ContentClass;
	ContentClass.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
		| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton));
	ImGui::SetNextWindowClass(&ContentClass);
	if (!ImGui::Begin(kWinContent, nullptr, ImGuiWindowFlags_NoCollapse))
	{
		bContentBrowserDropRectValid = false;
		ImGui::End();
		return;
	}

	{
		const ImVec2 Min = ImGui::GetWindowPos();
		const ImVec2 Size = ImGui::GetWindowSize();
		ContentBrowserDropMinX = Min.x;
		ContentBrowserDropMinY = Min.y;
		ContentBrowserDropMaxX = Min.x + Size.x;
		ContentBrowserDropMaxY = Min.y + Size.y;
		bContentBrowserDropRectValid = true;
	}

	RefreshContentListing();

	const bool bLocked = IsContentBrowserInputLocked();
	ImGui::BeginDisabled(bLocked);

	const bool bAtMountRoot =
		CurrentVirtualPath == "/Game" || CurrentVirtualPath == "/Engine";
	ImGui::BeginDisabled(bAtMountRoot);
	if (ImGui::SmallButton(ICON_FA_ARROW_UP "##ContentUp"))
	{
		const std::size_t Slash = CurrentVirtualPath.find_last_of('/');
		if (Slash != std::string::npos && Slash > 0)
		{
			SelectContentFolder(CurrentVirtualPath.substr(0, Slash));
		}
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Up to parent folder");
	}
	ImGui::SameLine(0.0f, 6.0f);

	{
		std::string Accumulated;
		bool bFirst = true;
		std::size_t Index = 0;
		while (Index < CurrentVirtualPath.size())
		{
			if (CurrentVirtualPath[Index] == '/')
			{
				++Index;
				continue;
			}
			const std::size_t Next = CurrentVirtualPath.find('/', Index);
			const std::string Segment = CurrentVirtualPath.substr(
				Index,
				Next == std::string::npos ? std::string::npos : Next - Index);
			Accumulated += "/";
			Accumulated += Segment;

			if (!bFirst)
			{
				ImGui::SameLine(0.0f, 2.0f);
				ImGui::TextDisabled("/");
				ImGui::SameLine(0.0f, 2.0f);
			}
			bFirst = false;

			ImGui::PushID(static_cast<int>(Accumulated.size()));
			const bool bIsCurrent = (Accumulated == CurrentVirtualPath);
			if (bIsCurrent)
			{
				ImGui::TextUnformatted(Segment.c_str());
			}
			else if (ImGui::SmallButton(Segment.c_str()))
			{
				SelectContentFolder(Accumulated);
			}
			ImGui::PopID();

			if (Next == std::string::npos)
			{
				break;
			}
			Index = Next + 1;
		}
	}

	ImGui::SameLine();
	if (ImGui::SmallButton(ICON_FA_ARROWS_ROTATE "##ContentRefresh"))
	{
		RefreshContentListing();
	}
	if (bLocked)
	{
		ImGui::SameLine();
		ImGui::TextDisabled(bManualImportActive ? "(importing…)" : "(refreshing…)");
	}

	const ImVec4 DeepBg = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	const float TreeWidth = ImMax(180.0f, ImGui::GetContentRegionAvail().x * 0.22f);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, DeepBg);
	ImGui::BeginChild(
		"##ContentTree",
		ImVec2(TreeWidth, 0.0f),
		ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_HorizontalScrollbar);
	DrawContentBrowserTree();
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::SameLine(0.0f, 0.0f);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, DeepBg);
	ImGui::BeginChild(
		"##ContentTiles",
		ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	DrawContentBrowserTiles();
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::EndDisabled();
	ImGui::End();
}

void FEditorLayer::DrawContentBrowserTree()
{
	for (const FPathMount& Mount : FPaths::GetMountPoints())
	{
		ImGuiTreeNodeFlags RootFlags =
			ImGuiTreeNodeFlags_OpenOnArrow
			| ImGuiTreeNodeFlags_OpenOnDoubleClick
			| ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_DefaultOpen;
		if (CurrentVirtualPath == Mount.VirtualRoot)
		{
			RootFlags |= ImGuiTreeNodeFlags_Selected;
		}

		ImGui::PushID(Mount.VirtualRoot.c_str());
		const bool bOpen = ImGui::TreeNodeEx(
			"##Root",
			RootFlags,
			ICON_FA_FOLDER_TREE "  %s",
			Mount.VirtualRoot.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			SelectContentFolder(Mount.VirtualRoot);
		}
		if (bOpen)
		{
			DrawVirtualFolderTree(Mount.VirtualRoot);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void FEditorLayer::DrawVirtualFolderTree(const std::string& VirtualPath)
{
	std::vector<std::string> Children;
	CollectChildFolders(VirtualPath, Children);
	for (const std::string& ChildVirtual : Children)
	{
		const std::size_t Slash = ChildVirtual.find_last_of('/');
		const std::string ChildName =
			(Slash == std::string::npos) ? ChildVirtual : ChildVirtual.substr(Slash + 1);

		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow
			| ImGuiTreeNodeFlags_OpenOnDoubleClick
			| ImGuiTreeNodeFlags_SpanAvailWidth;
		if (CurrentVirtualPath == ChildVirtual)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		ImGui::PushID(ChildVirtual.c_str());
		const bool bOpen = ImGui::TreeNodeEx(
			"##Dir",
			Flags,
			ICON_FA_FOLDER "  %s",
			ChildName.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			SelectContentFolder(ChildVirtual);
		}
		if (bOpen)
		{
			DrawVirtualFolderTree(ChildVirtual);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void FEditorLayer::DrawContentBrowserTiles()
{
	const float Tile = 88.0f;
	const float Pad = 8.0f;
	const float LabelGap = 4.0f;
	const float Cell = Tile + Pad;
	const float AvailX = ImGui::GetContentRegionAvail().x;
	int Columns = static_cast<int>(AvailX / Cell);
	if (Columns < 1)
	{
		Columns = 1;
	}

	auto DrawFolderTile = [&](const std::string& VirtualPath)
	{
		const std::size_t Slash = VirtualPath.find_last_of('/');
		const std::string EntryName =
			(Slash == std::string::npos) ? VirtualPath : VirtualPath.substr(Slash + 1);
		const bool bSelected = SelectedVirtualEntry == VirtualPath;
		const char* Glyph = ICON_FA_FOLDER;
		const ImU32 GlyphColor = IM_COL32(242, 199, 89, 255);
		const ImU32 FaceColor = bSelected ? IM_COL32(51, 71, 102, 255) : IM_COL32(31, 33, 38, 255);
		const ImU32 FaceHover = IM_COL32(56, 61, 71, 255);

		ImGui::PushID(VirtualPath.c_str());
		ImGui::BeginGroup();
		const ImVec2 StartPos = ImGui::GetCursorPos();
		const ImVec2 Screen0 = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##Tile", ImVec2(Tile, Tile));
		const bool bHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
		{
			SelectedVirtualEntry = VirtualPath;
		}
		if (bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			SelectContentFolder(VirtualPath);
		}

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(
			Screen0,
			ImVec2(Screen0.x + Tile, Screen0.y + Tile),
			bHovered ? FaceHover : FaceColor,
			4.0f);
		ImFont* Font = ImGui::GetFont();
		const float IconPx = Tile * 0.72f;
		const ImVec2 GlyphSize = Font->CalcTextSizeA(IconPx, FLT_MAX, 0.0f, Glyph);
		DrawList->AddText(
			Font,
			IconPx,
			ImVec2(
				Screen0.x + (Tile - GlyphSize.x) * 0.5f,
				Screen0.y + (Tile - GlyphSize.y) * 0.5f),
			GlyphColor,
			Glyph);

		const ImVec2 LabelSize = ImGui::CalcTextSize(EntryName.c_str());
		ImGui::SetCursorPos(ImVec2(
			StartPos.x + (Tile - LabelSize.x) * 0.5f,
			StartPos.y + Tile + LabelGap));
		ImGui::TextUnformatted(EntryName.c_str());
		ImGui::SetCursorPos(ImVec2(StartPos.x, StartPos.y + Tile + LabelGap + LabelSize.y));
		ImGui::Dummy(ImVec2(Tile, 0.0f));
		ImGui::EndGroup();
		ImGui::PopID();
	};

	auto DrawAssetTile = [&](const FContentAssetEntry& Entry)
	{
		const bool bSelected = SelectedVirtualEntry == Entry.CatalogKey;
		const char* Glyph = ResourceTypeGlyph(Entry.Type);
		ImU32 GlyphColor = ResourceTypeGlyphColor(Entry.Type);
		if (Entry.LoadState == EResourceLoadState::Pending)
		{
			GlyphColor = IM_COL32(140, 140, 150, 255);
		}
		else if (Entry.LoadState == EResourceLoadState::Failed)
		{
			GlyphColor = IM_COL32(220, 90, 90, 255);
		}
		const ImU32 FaceColor = bSelected ? IM_COL32(51, 71, 102, 255) : IM_COL32(31, 33, 38, 255);
		const ImU32 FaceHover = IM_COL32(56, 61, 71, 255);

		ImGui::PushID(Entry.CatalogKey.c_str());
		ImGui::BeginGroup();
		const ImVec2 StartPos = ImGui::GetCursorPos();
		const ImVec2 Screen0 = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##Tile", ImVec2(Tile, Tile));
		const bool bHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
		{
			SelectedVirtualEntry = Entry.CatalogKey;
		}
		if (bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			OpenResourceBrowser(Entry.CatalogKey);
		}
		if (ImGui::BeginPopupContextItem("##AssetCtx"))
		{
			SelectedVirtualEntry = Entry.CatalogKey;
			if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save"))
			{
				if (SaveContentAsset(Entry.CatalogKey))
				{
					AppendOutput("Save queued: " + Entry.CatalogKey);
				}
				else
				{
					AppendOutput("Save failed: " + Entry.CatalogKey, spdlog::level::err);
				}
			}
			ImGui::EndPopup();
		}
		if (bHovered)
		{
			ImGui::SetTooltip(
				"%s\n%s · %s%s",
				Entry.CatalogKey.c_str(),
				LoadStateLabel(Entry.LoadState),
				Entry.DisplayName.c_str(),
				Entry.bDirty ? "\n(unsaved)" : "");
		}

		// Drag-drop source: allow dragging assets to Inspector fields
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			const std::string& SoftPathStr = Entry.CatalogKey;
			ImGui::SetDragDropPayload("ASSET_PATH", SoftPathStr.c_str(), SoftPathStr.size() + 1);
			ImGui::Text("%s %s", ResourceTypeGlyph(Entry.Type), Entry.DisplayName.c_str());
			ImGui::EndDragDropSource();
		}

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(
			Screen0,
			ImVec2(Screen0.x + Tile, Screen0.y + Tile),
			bHovered ? FaceHover : FaceColor,
			4.0f);
		ImFont* Font = ImGui::GetFont();
		const float IconPx = Tile * 0.72f;
		const ImVec2 GlyphSize = Font->CalcTextSizeA(IconPx, FLT_MAX, 0.0f, Glyph);
		DrawList->AddText(
			Font,
			IconPx,
			ImVec2(
				Screen0.x + (Tile - GlyphSize.x) * 0.5f,
				Screen0.y + (Tile - GlyphSize.y) * 0.5f),
			GlyphColor,
			Glyph);

		const std::string Label =
			Entry.bDirty ? (Entry.DisplayName + " *") : Entry.DisplayName;
		const ImVec2 LabelSize = ImGui::CalcTextSize(Label.c_str());
		ImGui::SetCursorPos(ImVec2(
			StartPos.x + (Tile - LabelSize.x) * 0.5f,
			StartPos.y + Tile + LabelGap));
		ImGui::TextUnformatted(Label.c_str());
		ImGui::SetCursorPos(ImVec2(StartPos.x, StartPos.y + Tile + LabelGap + LabelSize.y));
		ImGui::Dummy(ImVec2(Tile, 0.0f));
		ImGui::EndGroup();
		ImGui::PopID();
	};

	int Index = 0;
	for (const std::string& Folder : FolderVirtualEntries)
	{
		if (Index > 0 && (Index % Columns) != 0)
		{
			ImGui::SameLine(0.0f, Pad);
		}
		DrawFolderTile(Folder);
		++Index;
	}
	for (const FContentAssetEntry& Asset : AssetEntries)
	{
		if (Index > 0 && (Index % Columns) != 0)
		{
			ImGui::SameLine(0.0f, Pad);
		}
		DrawAssetTile(Asset);
		++Index;
	}

	if (FolderVirtualEntries.empty() && AssetEntries.empty())
	{
		ImGui::TextDisabled("Empty  %s", CurrentVirtualPath.c_str());
		ImGui::TextDisabled("Showing registered FResource assets only.");
		const std::string Disk = FPaths::ConvertVirtualPathToFilename(CurrentVirtualPath);
		if (!Disk.empty())
		{
			ImGui::TextDisabled("Mount disk: %s", Disk.c_str());
		}
		ImGui::TextDisabled("Drag source files (png/fbx/pmx/…) here to import.");
		if (bManualImportActive)
		{
			ImGui::TextDisabled("Import still running…");
		}
	}
}

void FEditorLayer::EnsureContentMounts()
{
	FPaths::EnsureMountDirectories();
}

void FEditorLayer::SelectContentFolder(const std::string& VirtualPath)
{
	CurrentVirtualPath = VirtualPath.empty() ? "/Game" : FPaths::NormalizePackagePath(VirtualPath);
	SelectedVirtualEntry.clear();
	RefreshContentListing();
}

void FEditorLayer::RefreshContentListing()
{
	bContentBrowserRefreshing = true;
	FolderVirtualEntries.clear();
	AssetEntries.clear();

	CollectChildFolders(CurrentVirtualPath, FolderVirtualEntries);

	FResourceSystem* Resources = Detail::GetResourceSystem();
	if (Resources)
	{
		const std::string Folder = FPaths::NormalizePackagePath(CurrentVirtualPath);
		std::size_t ResourceCount = 0;
		Resources->ForEachRegisteredResource(
			[&](const std::string& CatalogKey, FResource& Resource)
			{
				++ResourceCount;
				const std::string PackageName = FPaths::GetPackageName(CatalogKey);
				if (PackageName.empty())
				{
					return;
				}
				// Package "/Game/Foo" lists asset "Foo" under folder "/Game".
				if (VirtualPathParent(PackageName) != Folder)
				{
					return;
				}
				const std::string AssetName =
					CatalogKey.size() > PackageName.size() + 1
						? CatalogKey.substr(PackageName.size() + 1)
						: std::string();
				FContentAssetEntry Entry;
				Entry.CatalogKey = CatalogKey;
				Entry.DisplayName = AssetName;
				Entry.Type = Resource.GetType();
				Entry.LoadState = Resource.GetLoadState();
				Entry.bDirty = Resource.IsDirty();
				AssetEntries.push_back(std::move(Entry));
			});

		static std::size_t sLastResourceCount = ~std::size_t{0};
		if (ResourceCount != sLastResourceCount)
		{
			sLastResourceCount = ResourceCount;
			AppendOutput(
				"ContentBrowser: " + std::to_string(ResourceCount)
				+ " resources in catalog, showing " + std::to_string(AssetEntries.size())
				+ " under '" + Folder + "'");
		}
	}

	std::sort(
		AssetEntries.begin(),
		AssetEntries.end(),
		[](const FContentAssetEntry& A, const FContentAssetEntry& B)
		{
			return A.DisplayName < B.DisplayName;
		});
	bContentBrowserRefreshing = false;
}

void FEditorLayer::StartStartupCassetLoad()
{
	if (bStartupCassetLoadStarted)
	{
		return;
	}

	FResourceSystem* Resources = Detail::GetResourceSystem();
	if (!Resources || !Resources->IsInitialized())
	{
		return;
	}

	bStartupCassetLoadStarted = true;
	bStartupCassetLoadActive = true;
	bStartupCassetScanDone = false;
	StartupCassetPaths.clear();
	StartupCassetSoftPaths.clear();
	StartupCassetNextIndex = 0;
	StartupCassetLoaded = 0;
}

void FEditorLayer::TickStartupCassetLoad()
{
	if (EditorUiPresentedFrames < kEditorUiSettleFrames)
	{
		return;
	}
	if (!bStartupCassetLoadStarted)
	{
		StartStartupCassetLoad();
	}
	if (!bStartupCassetLoadActive)
	{
		return;
	}

	FResourceSystem* Resources = Detail::GetResourceSystem();
	if (!Resources || !Resources->IsInitialized())
	{
		return;
	}

	if (!bStartupCassetScanDone)
	{
			const std::string ExtRaw = FPaths::GetPackageExtension();
			// Strip leading '.' for suffix comparison (e.g. ".casset" → "casset").
			const std::string Ext = (ExtRaw.size() > 1 && ExtRaw[0] == '.')
				? ExtRaw.substr(1)
				: ExtRaw;

		std::size_t MountCount = 0;
		for (const FPathMount& Mount : FPaths::GetMountPoints())
		{
			++MountCount;
			std::error_code ErrorCode;
			const std::filesystem::path DiskRoot = PathFromUtf8(Mount.DiskRoot);
			if (!std::filesystem::is_directory(DiskRoot, ErrorCode) || ErrorCode)
			{
				continue;
			}

			for (const std::filesystem::directory_entry& Entry :
				std::filesystem::recursive_directory_iterator(DiskRoot, ErrorCode))
			{
				if (ErrorCode)
				{
					break;
				}
				if (!Entry.is_regular_file(ErrorCode) || ErrorCode)
				{
					continue;
				}
				const std::string FileName = PathToUtf8(Entry.path().filename());
				if (FileName.size() < Ext.size())
				{
					continue;
				}
				const std::string Suffix = FileName.substr(FileName.size() - Ext.size());
				bool bMatch = true;
				for (std::size_t I = 0; I < Ext.size(); ++I)
				{
					const char A = Suffix[I];
					const char B = Ext[I];
					const char La = (A >= 'A' && A <= 'Z') ? static_cast<char>(A - 'A' + 'a') : A;
					const char Lb = (B >= 'A' && B <= 'Z') ? static_cast<char>(B - 'A' + 'a') : B;
					if (La != Lb)
					{
						bMatch = false;
						break;
					}
				}
				if (!bMatch)
				{
					continue;
				}

				const std::string Absolute = PathToUtf8(std::filesystem::absolute(Entry.path(), ErrorCode));
				if (ErrorCode || Absolute.empty())
				{
					continue;
				}
				StartupCassetPaths.push_back(Absolute);
			}
		}
		bStartupCassetScanDone = true;
		AppendOutput(
			"Scanned " + std::to_string(MountCount) + " mount(s), found "
			+ std::to_string(StartupCassetPaths.size()) + " .casset file(s)");
		if (StartupCassetPaths.empty())
		{
			bStartupCassetLoadActive = false;
			RefreshContentListing();
			AppendOutput("No .casset packages found under Content mounts.");
			return;
		}

		StartupCassetSoftPaths.clear();
		std::size_t Kicked = 0;
		for (const std::string& Absolute : StartupCassetPaths)
		{
			const std::string PackagePath = FPaths::ConvertFilenameToPackageName(Absolute);
			std::string ObjectName = PathToUtf8(PathFromUtf8(Absolute).stem());
			if (PackagePath.empty() || ObjectName.empty())
			{
				continue;
			}

			FResourceImportConfig Config;
			Config.PackagePath = PackagePath;
			Config.ObjectName = std::move(ObjectName);
			Config.SourcePath = Absolute;
			Config.Mode = EResourceIOMode::Async;
			std::string Soft;
			if (Resources->Import(std::make_unique<FCassetPackageImporter>(), std::move(Config), Soft))
			{
				StartupCassetSoftPaths.push_back(std::move(Soft));
				++Kicked;
			}
		}
		StartupCassetLoaded = 0;
		AppendOutput(
			"Queued " + std::to_string(Kicked) + "/" + std::to_string(StartupCassetPaths.size())
			+ " .casset package(s) for async load…");
		return;
	}

	StartupCassetLoaded = 0;
	std::size_t StartupCassetFailed = 0;
	bool bAnyPending = false;
	for (const std::string& Soft : StartupCassetSoftPaths)
	{
		const EResourceLoadState State = Resources->GetLoadState(Soft);
		if (State == EResourceLoadState::Pending)
		{
			bAnyPending = true;
		}
		else if (State == EResourceLoadState::Ready)
		{
			++StartupCassetLoaded;
		}
		else
		{
			++StartupCassetFailed;
		}
	}

	if ((StartupCassetLoaded % 2) == 0 || !bAnyPending)
	{
		RefreshContentListing();
	}

	if (!bAnyPending)
	{
		bStartupCassetLoadActive = false;
		RefreshContentListing();
		if (StartupCassetFailed > 0)
		{
			AppendOutput(
				"Loaded " + std::to_string(StartupCassetLoaded) + "/"
				+ std::to_string(StartupCassetSoftPaths.size()) + " .casset package(s), "
				+ std::to_string(StartupCassetFailed) + " failed.");
		}
		else
		{
			AppendOutput(
				"Loaded " + std::to_string(StartupCassetLoaded) + "/"
				+ std::to_string(StartupCassetSoftPaths.size()) + " .casset package(s).");
		}
	}
}

void FEditorLayer::OpenImporterDialog(const std::string& SourcePath)
{
	if (!CanImportSourcePathLocal(SourcePath))
	{
		AppendOutput("Importer: unsupported source '" + SourcePath + "'", spdlog::level::warn);
		return;
	}

	const std::string Stem = PathToUtf8(PathFromUtf8(SourcePath).stem());
	std::string PackagePath = FPaths::NormalizePackagePath(CurrentVirtualPath + "/" + Stem);
	if (PackagePath.empty())
	{
		PackagePath = "/Game/" + Stem;
	}

	ImporterDialog = {};
	ImporterDialog.bOpen = true;
	ImporterDialog.SourcePath = SourcePath;
	ImporterDialog.PackagePath = PackagePath;
	ImporterDialog.ObjectName = Stem;
	ImporterDialog.TypeHint = InferImportTypeLocal(SourcePath);
	std::snprintf(
		ImporterDialog.PackagePathEdit,
		sizeof(ImporterDialog.PackagePathEdit),
		"%s",
		PackagePath.c_str());
	std::snprintf(
		ImporterDialog.ObjectNameEdit,
		sizeof(ImporterDialog.ObjectNameEdit),
		"%s",
		Stem.c_str());
}

void FEditorLayer::DrawImporterDialog()
{
	if (!ImporterDialog.bOpen)
	{
		return;
	}

	ImGui::OpenPopup("Import Asset");
	const ImVec2 Center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(Center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Import Asset", &ImporterDialog.bOpen, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Source");
		ImGui::TextWrapped("%s", ImporterDialog.SourcePath.c_str());
		ImGui::Text("Inferred type: %s", ResourceTypeLabel(ImporterDialog.TypeHint));
		ImGui::Separator();
		ImGui::InputText("Package path", ImporterDialog.PackagePathEdit, sizeof(ImporterDialog.PackagePathEdit));
		ImGui::InputText("Object name", ImporterDialog.ObjectNameEdit, sizeof(ImporterDialog.ObjectNameEdit));
		ImGui::TextDisabled("Writes to memory until you Save a .casset from the Content Browser.");

		if (ImGui::Button("Import", ImVec2(120.0f, 0.0f)))
		{
			ConfirmImporterDialog();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		{
			ImporterDialog.bOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (!ImporterDialog.bOpen)
	{
		ImporterDialog = {};
	}
}

void FEditorLayer::ConfirmImporterDialog()
{
	FResourceSystem* Resources = Detail::GetResourceSystem();
	if (!Resources)
	{
		ImporterDialog.bOpen = false;
		return;
	}

	ImporterDialog.PackagePath = FPaths::NormalizePackagePath(ImporterDialog.PackagePathEdit);
	ImporterDialog.ObjectName = ImporterDialog.ObjectNameEdit;
	ImporterDialog.bOpen = false;

	if (ImporterDialog.PackagePath.empty() || ImporterDialog.ObjectName.empty())
	{
		AppendOutput("Importer: package path / object name required", spdlog::level::err);
		return;
	}

	FManualImportJob Job;
	Job.SourcePath = ImporterDialog.SourcePath;
	Job.PackagePath = ImporterDialog.PackagePath;
	Job.ObjectName = ImporterDialog.ObjectName;
	Job.TypeHint = ImporterDialog.TypeHint;
	ManualImportJobs.clear();
	ManualImportJobs.push_back(std::move(Job));
	ManualImportKickIndex = 0;
	ManualImportCompleted = 0;
	ManualImportFailed = 0;
	ManualImportCurrentName.clear();
	bManualImportActive = true;
	AppendOutput(
		"Import queued [type=" + std::to_string(static_cast<int>(ImporterDialog.TypeHint))
		+ "]: " + ImporterDialog.SourcePath);
}

void FEditorLayer::TickManualContentImport()
{
	if (!bManualImportActive)
	{
		return;
	}

	FResourceSystem* Resources = Detail::GetResourceSystem();
	if (!Resources)
	{
		bManualImportActive = false;
		return;
	}

	constexpr std::size_t KicksPerFrame = 4;
	std::size_t KickedThisFrame = 0;
	while (ManualImportKickIndex < ManualImportJobs.size() && KickedThisFrame < KicksPerFrame)
	{
		FManualImportJob& Job = ManualImportJobs[ManualImportKickIndex];
		++ManualImportKickIndex;
		if (Job.bKicked)
		{
			continue;
		}

		const std::string CatalogKey = Job.PackagePath + "." + Job.ObjectName;
		if (Resources->Find(CatalogKey))
		{
			AppendOutput("Import skipped — already registered: " + CatalogKey, spdlog::level::warn);
			Job.SoftPath = CatalogKey;
			Job.bKicked = true;
			continue;
		}

		FResourceImportConfig Config;
		Config.PackagePath = Job.PackagePath;
		Config.ObjectName = Job.ObjectName;
		Config.SourcePath = Job.SourcePath;
		Config.TypeHint = Job.TypeHint;
		Config.Mode = EResourceIOMode::Async;
		std::string OutSoftPath;
		Job.bKicked = EnqueueTypedImport(*Resources, std::move(Config), Job.TypeHint, OutSoftPath);
		Job.SoftPath = OutSoftPath;
		Job.bKicked = true;
		ManualImportCurrentName = PathToUtf8(PathFromUtf8(Job.SourcePath).filename());
		if (!Job.SoftPath.empty())
		{
			AppendOutput("Import kicked: " + Job.SoftPath
				+ " <- " + Job.SourcePath);
		}
		else
		{
			AppendOutput("Import kick FAILED: " + Job.SourcePath, spdlog::level::err);
		}
		++KickedThisFrame;
	}

	ManualImportCompleted = 0;
	ManualImportFailed = 0;
	bool bAnyPending = false;
	for (FManualImportJob& Job : ManualImportJobs)
	{
		if (!Job.bKicked)
		{
			bAnyPending = true;
			continue;
		}
		if (Job.SoftPath.empty())
		{
			++ManualImportFailed;
			AppendOutput("Import FAILED (invalid SoftPath): " + Job.SourcePath, spdlog::level::err);
			continue;
		}
		const EResourceLoadState State = Resources->GetLoadState(Job.SoftPath);
		switch (State)
		{
		case EResourceLoadState::Pending:
			bAnyPending = true;
			if (ManualImportCurrentName.empty())
			{
				ManualImportCurrentName = PathToUtf8(PathFromUtf8(Job.SourcePath).filename());
			}
			break;
		case EResourceLoadState::Ready:
			++ManualImportCompleted;
			AppendOutput("Import OK: " + Job.SourcePath, spdlog::level::info);
			break;
		case EResourceLoadState::Failed:
		case EResourceLoadState::Invalid:
		default:
			++ManualImportFailed;
			AppendOutput(
				"Import FAILED (state=" + std::to_string(static_cast<int>(State))
				+ "): " + Job.SourcePath,
				spdlog::level::err);
			break;
		}
	}

	if (ManualImportKickIndex < ManualImportJobs.size())
	{
		bAnyPending = true;
	}

	if (!bAnyPending)
	{
		bManualImportActive = false;
		ManualImportCurrentName.clear();
		RefreshContentListing();
		AppendOutput(
			"Import finished: " + std::to_string(ManualImportCompleted) + " ok"
			+ (ManualImportFailed > 0
				? ", " + std::to_string(ManualImportFailed) + " failed"
				: "")
			+ " / " + std::to_string(ManualImportJobs.size()));
	}
}

void FEditorLayer::DrawContentImportProgressOverlay()
{
	FResourceSystem* Resources = Detail::GetResourceSystem();
	const bool bShowImport = bManualImportActive && !ManualImportJobs.empty();
	const bool bShowCasset = bStartupCassetLoadActive && bStartupCassetScanDone && !StartupCassetSoftPaths.empty();
	const bool bShowSave = Resources && Resources->IsSavePackageBusy();
	if (!bShowImport && !bShowCasset && !bShowSave)
	{
		return;
	}

	ImGuiViewport* Viewport = ImGui::GetMainViewport();
	if (!Viewport)
	{
		return;
	}

	const ImVec2 Pad(16.0f, 16.0f);
	ImGui::SetNextWindowPos(
		ImVec2(Viewport->WorkPos.x + Viewport->WorkSize.x - Pad.x, Viewport->WorkPos.y + Viewport->WorkSize.y - Pad.y),
		ImGuiCond_Always,
		ImVec2(1.0f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.92f);
	ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoFocusOnAppearing
		| ImGuiWindowFlags_NoNav;
	if (ImGui::Begin("##ContentImportProgress", nullptr, Flags))
	{
		if (bShowSave)
		{
			ImGui::TextUnformatted("Saving .casset…");
			ImGui::ProgressBar(Resources->GetSavePackageProgress(), ImVec2(260.0f, 0.0f));
			const std::string& Status = Resources->GetSavePackageStatusText();
			if (!Status.empty())
			{
				ImGui::TextDisabled("%s", Status.c_str());
			}
		}
		if (bShowCasset)
		{
			const float Total = static_cast<float>(StartupCassetSoftPaths.size());
			const float Done = static_cast<float>(StartupCassetLoaded);
			const float Fraction = Total > 0.0f ? (Done / Total) : 0.0f;
			ImGui::TextUnformatted("Loading .casset…");
			ImGui::ProgressBar(Fraction, ImVec2(260.0f, 0.0f));
			ImGui::Text(
				"%zu / %zu",
				StartupCassetLoaded,
				StartupCassetSoftPaths.size());
		}
		if (bShowImport)
		{
			const float Total = static_cast<float>(ManualImportJobs.size());
			const float Done = static_cast<float>(ManualImportCompleted);
			const float Fraction = Total > 0.0f ? (Done / Total) : 0.0f;
			ImGui::TextUnformatted("Importing…");
			ImGui::ProgressBar(Fraction, ImVec2(260.0f, 0.0f));
			ImGui::Text("%zu / %zu", ManualImportCompleted, ManualImportJobs.size());
			if (!ManualImportCurrentName.empty())
			{
				ImGui::TextDisabled("%s", ManualImportCurrentName.c_str());
			}
		}
	}
	ImGui::End();
}

bool FEditorLayer::SaveContentAsset(const std::string& CatalogKey)
{
	FResourceSystem* Resources = Detail::GetResourceSystem();
	if (!Resources)
	{
		return false;
	}

	TRef<FResource> Resource = Resources->Find(CatalogKey);
	if (!Resource)
	{
		return false;
	}

	return Resources->EnqueueSavePackage(FPaths::GetPackageName(CatalogKey), true);
}

void FEditorLayer::OpenResourceBrowser(const std::string& CatalogKey)
{
	for (FResourceBrowserWindow& Window : OpenResourceBrowsers)
	{
		if (Window.CatalogKey == CatalogKey)
		{
			Window.bOpen = true;
			ResourceBrowserFocusKey = CatalogKey;
			return;
		}
	}

	FResourceSystem* Resources = Detail::GetResourceSystem();
	TRef<FResource> Resource = Resources ? Resources->Find(CatalogKey) : TRef<FResource>{};
	FResourceBrowserWindow Window;
	Window.CatalogKey = CatalogKey;
	Window.Type = Resource ? Resource->GetType() : EResourceType::Unknown;
	Window.bOpen = true;
	OpenResourceBrowsers.push_back(std::move(Window));
	ResourceBrowserFocusKey = CatalogKey;
}

void FEditorLayer::ReleaseResourceBrowserPreview(FResourceBrowserWindow& Window, FEngineBase& App)
{
	if (!Window.PreviewTexture.IsValid())
	{
		return;
	}
	FRenderSystem* Render = App.GetExtension<FRenderSystem>();
	if (Render)
	{
		Render->GetImGui().DestroyTexture(
			Render->GetRHIServer(),
			Window.PreviewTexture);
	}
	Window.PreviewTexture.Reset();
	Window.PreviewGeneration = 0;
}

void FEditorLayer::DrawOpenResourceBrowsers(FEngineBase& App)
{
	if (OpenResourceBrowsers.empty())
	{
		return;
	}

	FResourceSystem* Resources = Detail::GetResourceSystem();

	ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);
	bool bHostOpen = true;
	if (!ImGui::Begin("Asset Inspector###ResourceBrowserHost", &bHostOpen))
	{
		ImGui::End();
		if (!bHostOpen)
		{
			for (FResourceBrowserWindow& Window : OpenResourceBrowsers)
			{
				ReleaseResourceBrowserPreview(Window, App);
			}
			OpenResourceBrowsers.clear();
			ResourceBrowserFocusKey.clear();
		}
		return;
	}

	if (!bHostOpen)
	{
		ImGui::End();
		for (FResourceBrowserWindow& Window : OpenResourceBrowsers)
		{
			ReleaseResourceBrowserPreview(Window, App);
		}
		OpenResourceBrowsers.clear();
		ResourceBrowserFocusKey.clear();
		return;
	}

	const ImGuiTabBarFlags TabBarFlags =
		ImGuiTabBarFlags_Reorderable
		| ImGuiTabBarFlags_FittingPolicyScroll
		| ImGuiTabBarFlags_AutoSelectNewTabs;
	if (ImGui::BeginTabBar("##ResourceBrowserTabs", TabBarFlags))
	{
		for (std::size_t Index = 0; Index < OpenResourceBrowsers.size();)
		{
			FResourceBrowserWindow& Window = OpenResourceBrowsers[Index];
			if (!Window.bOpen)
			{
				ReleaseResourceBrowserPreview(Window, App);
				OpenResourceBrowsers.erase(OpenResourceBrowsers.begin() + static_cast<std::ptrdiff_t>(Index));
				continue;
			}

			const std::string PackageName = FPaths::GetPackageName(Window.CatalogKey);
			const std::string AssetName =
				Window.CatalogKey.size() > PackageName.size() + 1
					? Window.CatalogKey.substr(PackageName.size() + 1)
					: Window.CatalogKey;
			const std::string TabLabel =
				std::string(ResourceTypeGlyph(Window.Type)) + "  "
				+ AssetName
				+ "###RB_" + Window.CatalogKey;

			ImGuiTabItemFlags TabFlags = ImGuiTabItemFlags_None;
			if (!ResourceBrowserFocusKey.empty() && ResourceBrowserFocusKey == Window.CatalogKey)
			{
				TabFlags |= ImGuiTabItemFlags_SetSelected;
				ResourceBrowserFocusKey.clear();
			}

			bool bTabOpen = true;
			if (ImGui::BeginTabItem(TabLabel.c_str(), &bTabOpen, TabFlags))
			{
				TRef<FResource> Resource =
					Resources ? Resources->Find(Window.CatalogKey) : TRef<FResource>{};
				if (!Resource)
				{
					ImGui::TextDisabled("Resource no longer in catalog.");
				}
				else
				{
					ImGui::TextUnformatted(Window.CatalogKey.c_str());
					ImGui::Text(
						"Type: %d   State: %s",
						static_cast<int>(Resource->GetType()),
						LoadStateLabel(Resource->GetLoadState()));
					ImGui::TextWrapped("Source: %s", Resource->GetSourcePath().c_str());
					ImGui::Separator();
					if (Resource->GetLoadState() != EResourceLoadState::Ready)
					{
						ImGui::TextDisabled("Waiting until Ready…");
					}
					else
					{
						DrawResourceBrowserBody(Window, *Resource, App);
					}
				}
				ImGui::EndTabItem();
			}

			if (!bTabOpen)
			{
				Window.bOpen = false;
				ReleaseResourceBrowserPreview(Window, App);
				OpenResourceBrowsers.erase(OpenResourceBrowsers.begin() + static_cast<std::ptrdiff_t>(Index));
				continue;
			}
			++Index;
		}
		ImGui::EndTabBar();
	}

	ImGui::End();

	if (OpenResourceBrowsers.empty())
	{
		ResourceBrowserFocusKey.clear();
	}
}

void FEditorLayer::DrawResourceBrowserBody(FResourceBrowserWindow& Window, FResource& Resource, FEngineBase& App)
{
	const EResourceType Type = Resource.GetType();
	if (Type == EResourceType::Texture2D
		|| Type == EResourceType::Texture3D
		|| Type == EResourceType::TextureCube
		|| Type == EResourceType::TextureCubeArray
		|| Type == EResourceType::Texture2DArray)
	{
		FTexture* Texture = dynamic_cast<FTexture*>(&Resource);
		if (!Texture)
		{
			ImGui::TextDisabled("Not a FTexture.");
			return;
		}
		ImGui::Text(
			"%u x %u  format=%d  gen=%llu",
			Texture->GetWidth(),
			Texture->GetHeight(),
			static_cast<int>(Texture->GetPixelFormat()),
			static_cast<unsigned long long>(Texture->GetContentGeneration()));

		if (Texture->GetDimension() == ETextureDimension::Tex2D
			&& Texture->GetPixelFormat() == ETexturePixelFormat::RGBA8
			&& !Texture->GetPixels().empty())
		{
			if (Window.PreviewGeneration != Texture->GetContentGeneration() || !Window.PreviewTexture.IsValid())
			{
				ReleaseResourceBrowserPreview(Window, App);
				FRenderSystem* Render = App.GetExtension<FRenderSystem>();
				if (Render)
				{
					const bool bOk = Render->GetImGui().CreateRgba8Texture(
						Render->GetRHIServer(),
						Texture->GetWidth(),
						Texture->GetHeight(),
						Texture->GetPixels().data(),
						Texture->GetPixels().size(),
						Window.PreviewTexture);
					if (bOk)
					{
						Window.PreviewGeneration = Texture->GetContentGeneration();
					}
				}
			}
			if (Window.PreviewTexture.IsValid())
			{
				const float MaxW = ImGui::GetContentRegionAvail().x;
				const float MaxH = ImGui::GetContentRegionAvail().y;
				float W = static_cast<float>(Texture->GetWidth());
				float H = static_cast<float>(Texture->GetHeight());
				const float Scale = (std::min)(MaxW / W, MaxH / H);
				if (Scale > 0.0f && Scale < 1.0f)
				{
					W *= Scale;
					H *= Scale;
				}
				ImGui::Image(reinterpret_cast<ImTextureID>(Window.PreviewTexture.Id), ImVec2(W, H));
			}
		}
		else
		{
			ImGui::TextDisabled("Preview supports RGBA8 Tex2D only.");
		}
		return;
	}

	if (Type == EResourceType::Mesh)
	{
		if (FStaticMesh* Mesh = dynamic_cast<FStaticMesh*>(&Resource))
		{
			ImGui::Text("Vertices: %zu", Mesh->GetPositions().size() / 3u);
			ImGui::Text("Indices: %zu", Mesh->GetIndices().size());
			ImGui::TextWrapped("Material: %s", Mesh->GetMaterial().c_str());
		}
		return;
	}

	if (Type == EResourceType::Material)
	{
		if (FMaterial* Material = dynamic_cast<FMaterial*>(&Resource))
		{
			ImGui::TextWrapped("BaseColor: %s", Material->GetBaseColorTexture().c_str());
			ImGui::TextWrapped("Normal: %s", Material->GetNormalTexture().c_str());
			ImGui::TextWrapped("MR: %s", Material->GetMetallicRoughnessTexture().c_str());
			ImGui::TextWrapped("Occlusion: %s", Material->GetOcclusionTexture().c_str());
			ImGui::TextWrapped("Emissive: %s", Material->GetEmissiveTexture().c_str());
			ImGui::Text(
				"Metallic=%.2f Roughness=%.2f",
				Material->MetallicFactor,
				Material->RoughnessFactor);
			ImGui::Text(
				"EmissiveFactor=%.2f %.2f %.2f",
				Material->EmissiveFactor[0],
				Material->EmissiveFactor[1],
				Material->EmissiveFactor[2]);
		}
		return;
	}

	if (Type == EResourceType::Skeleton)
	{
		if (FSkeleton* Skeleton = dynamic_cast<FSkeleton*>(&Resource))
		{
			ImGui::Text("Bones: %zu", Skeleton->GetBones().size());
			if (ImGui::BeginChild("##BoneList", ImVec2(0, 0), ImGuiChildFlags_Borders))
			{
				for (std::size_t I = 0; I < Skeleton->GetBones().size(); ++I)
				{
					const FSkeletonBone& Bone = Skeleton->GetBones()[I];
					ImGui::Text("[%zu] %s  parent=%d", I, Bone.Name.c_str(), Bone.ParentIndex);
				}
			}
			ImGui::EndChild();
		}
		return;
	}

	if (Type == EResourceType::Animation)
	{
		if (FAnimation* Animation = dynamic_cast<FAnimation*>(&Resource))
		{
			ImGui::Text("Duration: %.3f s", Animation->GetDurationSeconds());
			ImGui::TextWrapped("Skeleton: %s", Animation->GetSkeleton().c_str());
			ImGui::Text("Tracks: %zu", Animation->GetTracks().size());
			if (ImGui::BeginChild("##AnimTracks", ImVec2(0, 0), ImGuiChildFlags_Borders))
			{
				for (const FAnimationTrack& Track : Animation->GetTracks())
				{
					ImGui::Text("%s  (%zu keys)", Track.TargetBoneName.c_str(), Track.Keys.size());
				}
			}
			ImGui::EndChild();
		}
		return;
	}

	if (Type == EResourceType::AnimationGraph)
	{
		if (FAnimationGraph* Graph = dynamic_cast<FAnimationGraph*>(&Resource))
		{
			if (ImGui::BeginChild("##AnimGraphJson", ImVec2(0, 0), ImGuiChildFlags_Borders))
			{
				ImGui::TextUnformatted(
					Graph->GetDocumentJson().empty() ? "(empty)" : Graph->GetDocumentJson().c_str());
			}
			ImGui::EndChild();
		}
		return;
	}

	if (Type == EResourceType::Prefab)
	{
		if (FPrefab* Prefab = dynamic_cast<FPrefab*>(&Resource))
		{
			if (ImGui::BeginChild("##PrefabJson", ImVec2(0, 0), ImGuiChildFlags_Borders))
			{
				ImGui::TextUnformatted(
					Prefab->GetDocumentJson().empty() ? "(empty)" : Prefab->GetDocumentJson().c_str());
			}
			ImGui::EndChild();
		}
		return;
	}

	ImGui::TextDisabled("No specialized browser for this type yet.");
}

void FEditorLayer::DrawOutputPanel(FEngineBase& App)
{
	ImGuiWindowClass OutputClass;
	OutputClass.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
		| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton));
	ImGui::SetNextWindowClass(&OutputClass);
	if (!ImGui::Begin(kWinOutput, nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	const float Footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
	ImGui::BeginChild("##OutputScroll", ImVec2(0.0f, -Footer), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGuiListClipper Clipper;
	Clipper.Begin(static_cast<int>(OutputLines.size()));
	while (Clipper.Step())
	{
		for (int Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
		{
			const FOutputLine& Line = OutputLines[static_cast<std::size_t>(Index)];
			ImGui::PushStyleColor(ImGuiCol_Text, OutputColorForLevel(Line.Level));
			ImGui::TextUnformatted(Line.Text.c_str());
			ImGui::PopStyleColor();
		}
	}
	if (bAutoScrollOutput && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
	{
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::InputText(
			"##ConsoleInput",
			ConsoleInput,
			IM_ARRAYSIZE(ConsoleInput),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll))
	{
		const std::string Line = ConsoleInput;
		ConsoleInput[0] = '\0';
		ExecuteConsoleLine(App, Line);
		ImGui::SetKeyboardFocusHere(-1);
	}
	ImGui::End();
}

void FEditorLayer::DrawAgentPanel()
{
	ImGuiWindowClass AgentClass;
	AgentClass.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
		| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton));
	ImGui::SetNextWindowClass(&AgentClass);
	if (!ImGui::Begin(kWinAgent, nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	const std::string Status = AgentChat ? AgentChat->GetStatusText() : "offline";
	const bool bConnected = AgentChat && AgentChat->IsConnected();
	const bool bBusy = AgentChat && AgentChat->IsBusy();
	const bool bMock = AgentChat && AgentChat->IsMockMode();

	ImGui::TextDisabled("Status: %s%s%s",
		Status.c_str(),
		bConnected ? " | connected" : " | disconnected",
		bMock ? " | mock" : "");
	ImGui::Separator();

	const float Footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
	ImGui::BeginChild("##AgentScroll", ImVec2(0.0f, -Footer), ImGuiChildFlags_AlwaysUseWindowPadding);
	for (const FAgentBubble& Bubble : AgentBubbles)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, AgentColorForRole(Bubble.Role));
		ImGui::TextUnformatted(AgentRoleLabel(Bubble.Role));
		ImGui::PopStyleColor();
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(Bubble.Text.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
	}
	if (bBusy)
	{
		ImGui::TextDisabled("Agent is thinking...");
	}
	if (bAutoScrollAgent && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
	{
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	ImGui::BeginDisabled(bBusy || !AgentChat);
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::InputText(
			"##AgentInput",
			AgentInput,
			IM_ARRAYSIZE(AgentInput),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll))
	{
		const std::string Line = AgentInput;
		AgentInput[0] = '\0';
		SendAgentMessage(Line);
		ImGui::SetKeyboardFocusHere(-1);
	}
	ImGui::EndDisabled();
	ImGui::End();
}

void FEditorLayer::EnsureSequenceGraphNodeLayout()
{
	EnsureSequenceGraphNodeLayout({});
}

void FEditorLayer::EnsureSequenceGraphNodeLayout(const std::vector<IEngineExtension*>& Extensions)
{
	if (bSequenceGraphLayoutApplied || !SequenceGraphEditorContext)
	{
		return;
	}

	if (SequenceGraphViewMode == 0)
	{
		const float OriginX = 40.0f;
		const float OriginY = 40.0f;
		const float ColW = 260.0f;
		const float RowH = 110.0f;
		int BandCounts[3] = {};

		for (std::size_t Index = 0; Index < Extensions.size(); ++Index)
		{
			IEngineExtension* Extension = Extensions[Index];
			if (!Extension)
			{
				continue;
			}

			int Band = static_cast<int>(Extension->GetPriority());
			if (Band < 0 || Band > 2)
			{
				Band = 0;
			}

			const float X = OriginX + static_cast<float>(Band) * ColW;
			const float Y = OriginY + static_cast<float>(BandCounts[Band]) * RowH;
			++BandCounts[Band];

			ed::SetNodePosition(
				ed::NodeId(SeqGraphIds::ExtNodeBase + static_cast<int>(Index)),
				ImVec2(X, Y));
		}
	}
	else
	{
		const float X = 0.0f;
		const float RowH = 80.0f;
		for (int i = 0; i < 8; ++i)
		{
			ed::SetNodePosition(
				ed::NodeId(SeqGraphIds::LifeNodeBase + i),
				ImVec2(X, static_cast<float>(i) * RowH));
		}
	}

	ed::NavigateToContent(0.15f);
	bSequenceGraphLayoutApplied = true;
}

void FEditorLayer::DrawSequenceGraphPanel(FEngineBase& App)
{
	if (!BeginEditorDockPanel(kWinSequenceGraph, &bShowSequenceGraphPanel))
	{
		ImGui::End();
		return;
	}

	if (!bSequenceGraphEditorInited || !SequenceGraphEditorContext)
	{
		ImGui::TextDisabled("Sequence Graph node editor context unavailable.");
		ImGui::End();
		return;
	}

	ImGui::TextDisabled(
		"State %s | Frame %llu | dt %.3f | fixedDt %.3f",
		AppStateLabel(App.GetState()),
		static_cast<unsigned long long>(App.GetFrameIndex()),
		App.GetDeltaSeconds(),
		App.GetFixedDeltaSeconds());
	ImGui::SameLine();
	ImGui::Dummy(ImVec2(12.0f, 0.0f));
	ImGui::SameLine();
	if (ImGui::RadioButton("Extension depends", SequenceGraphViewMode == 0))
	{
		SequenceGraphViewMode = 0;
		bSequenceGraphLayoutApplied = false;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("FEngineBase::Run lifecycle", SequenceGraphViewMode == 1))
	{
		SequenceGraphViewMode = 1;
		bSequenceGraphLayoutApplied = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Layout"))
	{
		bSequenceGraphLayoutApplied = false;
	}

	std::vector<IEngineExtension*> GraphExtensions;
	std::vector<FExtensionDepEdgeView> GraphEdges;
	if (SequenceGraphViewMode == 0)
	{
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::BeginCombo("##SeqGraphStage", EngineStageLabel(static_cast<EEngineStage>(SequenceGraphStage))))
		{
			const int StageCount = static_cast<int>(EEngineStage::COUNT);
			for (int Stage = 0; Stage < StageCount; ++Stage)
			{
				const bool bSelected = (SequenceGraphStage == Stage);
				if (ImGui::Selectable(EngineStageLabel(static_cast<EEngineStage>(Stage)), bSelected))
				{
					SequenceGraphStage = Stage;
					bSequenceGraphLayoutApplied = false;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		GraphExtensions = App.SnapshotExtensions();
		GraphEdges = App.SnapshotExtensionDeps(static_cast<EEngineStage>(SequenceGraphStage));

		if (GraphExtensions.size() != SequenceGraphLayoutExtCount
			|| SequenceGraphStage != SequenceGraphLayoutStage)
		{
			SequenceGraphLayoutExtCount = GraphExtensions.size();
			SequenceGraphLayoutStage = SequenceGraphStage;
			bSequenceGraphLayoutApplied = false;
		}
	}

	ImGui::Separator();
	if (SequenceGraphViewMode == 0)
	{
		ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "System");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.70f, 0.95f, 0.55f, 1.0f), "Layer");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.45f, 1.0f), "Overlay");
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Strong");
		ImGui::SameLine();
		ImGui::TextDisabled("Dep → Self");
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextColored(ImVec4(0.85f, 0.45f, 0.95f, 1.0f), "Weak");
		ImGui::SameLine();
		ImGui::TextDisabled("optional (exit stages)");
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextDisabled(
			"%zu extension(s), %zu edge(s) @ %s",
			GraphExtensions.size(),
			GraphEdges.size(),
			EngineStageLabel(static_cast<EEngineStage>(SequenceGraphStage)));
	}
	else
	{
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.40f, 1.0f), "Lifecycle");
		ImGui::SameLine();
		ImGui::TextDisabled("boot → tick → shutdown");
	}

	ed::SetCurrentEditor(static_cast<ed::EditorContext*>(SequenceGraphEditorContext));
	{
		ed::Style& NodeStyle = ed::GetStyle();
		const ImVec4 PanelBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		NodeStyle.Colors[ed::StyleColor_Bg] = PanelBg;
		NodeStyle.Colors[ed::StyleColor_Grid] = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
	}

	ed::Begin("SequenceGraphCanvas");

	int LinkSerial = 30000;

	if (SequenceGraphViewMode == 0)
	{
		for (std::size_t Index = 0; Index < GraphExtensions.size(); ++Index)
		{
			IEngineExtension* Extension = GraphExtensions[Index];
			if (!Extension)
			{
				continue;
			}

			const int NodeIndex = static_cast<int>(Index);
			const EExtensionPriority Band = Extension->GetPriority();
			const char* ExtName = Extension->GetName();

			ed::BeginNode(ed::NodeId(SeqGraphIds::ExtNodeBase + NodeIndex));
			ImGui::TextColored(ExtensionPriorityColor(Band), "%s", ExtensionPriorityLabel(Band));
			ImGui::TextUnformatted(ExtName ? ExtName : "?");
			ed::BeginPin(ed::PinId(SeqGraphIds::ExtInPinBase + NodeIndex), ed::PinKind::Input);
			ImGui::Text(ICON_FA_ARROW_LEFT " in");
			ed::EndPin();
			ImGui::SameLine();
			ed::BeginPin(ed::PinId(SeqGraphIds::ExtOutPinBase + NodeIndex), ed::PinKind::Output);
			ImGui::Text("out " ICON_FA_ARROW_RIGHT);
			ed::EndPin();
			ed::EndNode();
		}

		const ImVec4 StrongColor(0.35f, 0.75f, 1.0f, 0.95f);
		const ImVec4 WeakColor(0.85f, 0.45f, 0.95f, 0.90f);
		for (const FExtensionDepEdgeView& Edge : GraphEdges)
		{
			const int From = FindExtensionIndex(GraphExtensions, Edge.Predecessor);
			const int To = FindExtensionIndex(GraphExtensions, Edge.Successor);
			if (From < 0 || To < 0 || From == To)
			{
				continue;
			}

			const bool bWeak = (Edge.Strength == EExtensionDepStrength::Weak);
			ed::Link(
				ed::LinkId(LinkSerial++),
				ed::PinId(SeqGraphIds::ExtOutPinBase + From),
				ed::PinId(SeqGraphIds::ExtInPinBase + To),
				bWeak ? WeakColor : StrongColor,
				bWeak ? 1.5f : 2.5f);
		}
	}
	else
	{
		struct FLifeStep
		{
			const char* Title;
			const char* Detail;
		};
		const FLifeStep Steps[] = {
			{"Configure", "Game fills FConfig"},
			{"FPaths + Ini", "Roots / DefaultEngine.ini"},
			{"Generate App", "codegen RegisterExtension (+ plugins)"},
			{"InitExtensions", "PreInit/Init/PostInit by Priority+depends"},
			{"Attach", "All extensions Attach"},
			{"PostInitialize", "optional game hook after Attach"},
			{"TickGroups", "BeginFrame…PostRender topo per stage"},
			{"Shutdown", "PrepareExit/Detach/Shutdown topo"},
		};
		const int StepCount = static_cast<int>(sizeof(Steps) / sizeof(Steps[0]));

		for (int i = 0; i < StepCount; ++i)
		{
			ed::BeginNode(ed::NodeId(SeqGraphIds::LifeNodeBase + i));
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.40f, 1.0f), "%s", Steps[i].Title);
			ImGui::TextDisabled("%s", Steps[i].Detail);
			if (i > 0)
			{
				ed::BeginPin(ed::PinId(SeqGraphIds::LifeInPinBase + i), ed::PinKind::Input);
				ImGui::Text(ICON_FA_ARROW_LEFT " in");
				ed::EndPin();
			}
			if (i + 1 < StepCount)
			{
				ed::BeginPin(ed::PinId(SeqGraphIds::LifeOutPinBase + i), ed::PinKind::Output);
				ImGui::Text("out " ICON_FA_ARROW_RIGHT);
				ed::EndPin();
			}
			ed::EndNode();
		}

		for (int i = 0; i + 1 < StepCount; ++i)
		{
			ed::Link(
				ed::LinkId(LinkSerial++),
				ed::PinId(SeqGraphIds::LifeOutPinBase + i),
				ed::PinId(SeqGraphIds::LifeInPinBase + i + 1),
				ImVec4(1.0f, 0.82f, 0.35f, 0.95f),
				2.0f);
		}
	}

	if (!bSequenceGraphLayoutApplied)
	{
		if (SequenceGraphViewMode == 0)
		{
			EnsureSequenceGraphNodeLayout(GraphExtensions);
		}
		else
		{
			EnsureSequenceGraphNodeLayout();
		}
	}

	ed::End();
	ed::SetCurrentEditor(nullptr);

	ImGui::End();
}

void FEditorLayer::DrawBlueprintPanel()
{
	if (!BeginEditorDockPanel(kWinBlueprint, &bShowBlueprintPanel))
	{
		ImGui::End();
		return;
	}
#if MAHO_EDITOR_DEMO_CONTENT
	if (!bBlueprintInited || !NodeEditorContext)
	{
		ImGui::TextDisabled("Node editor context unavailable.");
		ImGui::End();
		return;
	}

	ed::SetCurrentEditor(static_cast<ed::EditorContext*>(NodeEditorContext));
	// Match Maho panel WindowBg so the canvas does not read as a lighter child sheet.
	{
		ed::Style& NodeStyle = ed::GetStyle();
		const ImVec4 PanelBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		NodeStyle.Colors[ed::StyleColor_Bg] = PanelBg;
		NodeStyle.Colors[ed::StyleColor_Grid] = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
	}
	ed::Begin("BlueprintCanvas");

	ed::BeginNode(ed::NodeId(1));
	ImGui::Text("Event BeginPlay");
	ed::BeginPin(ed::PinId(11), ed::PinKind::Output);
	ImGui::Text("exec " ICON_FA_ARROW_RIGHT);
	ed::EndPin();
	ed::EndNode();

	ed::BeginNode(ed::NodeId(2));
	ImGui::Text("Print String");
	ed::BeginPin(ed::PinId(21), ed::PinKind::Input);
	ImGui::Text(ICON_FA_ARROW_LEFT " exec");
	ed::EndPin();
	ed::EndNode();

	if (ed::Link(ed::LinkId(100), ed::PinId(11), ed::PinId(21)))
	{
	}

	ed::End();
	ed::SetCurrentEditor(nullptr);
#endif
	ImGui::End();
}

void FEditorLayer::DrawPlotPanel()
{
	if (!BeginEditorDockPanel(kWinPlot, &bShowPlotPanel))
	{
		ImGui::End();
		return;
	}
#if MAHO_EDITOR_DEMO_CONTENT
	static float Values[90] = {};
	static int Offset = 0;
	Values[Offset] = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
	Offset = (Offset + 1) % IM_ARRAYSIZE(Values);

	if (ImPlot::BeginPlot("Signal", ImVec2(-1, -1)))
	{
		ImPlot::PlotLine("sin", Values, IM_ARRAYSIZE(Values), 1.0, 0, ImPlotLineFlags_None, Offset);
		ImPlot::EndPlot();
	}
#endif
	ImGui::End();
}

void FEditorLayer::DrawFileDialogs()
{
#if MAHO_EDITOR_DEMO_CONTENT
	const ImVec2 MaxSize = ImVec2(900.0f, 600.0f);
	const ImVec2 MinSize = ImVec2(500.0f, 300.0f);
	if (ImGuiFileDialog::Instance()->Display("EditorOpenDlg", ImGuiWindowFlags_NoCollapse, MinSize, MaxSize))
	{
		if (ImGuiFileDialog::Instance()->IsOk())
		{
			AppendOutput(std::string("Open: ") + ImGuiFileDialog::Instance()->GetFilePathName());
		}
		ImGuiFileDialog::Instance()->Close();
	}
	if (ImGuiFileDialog::Instance()->Display("EditorSaveDlg", ImGuiWindowFlags_NoCollapse, MinSize, MaxSize))
	{
		if (ImGuiFileDialog::Instance()->IsOk())
		{
			AppendOutput(std::string("Save: ") + ImGuiFileDialog::Instance()->GetFilePathName());
		}
		ImGuiFileDialog::Instance()->Close();
	}
#endif
}

void FEditorLayer::AppendOutput(std::string Line, spdlog::level::level_enum Level)
{
	FOutputLine Entry;
	Entry.Text = std::move(Line);
	Entry.Level = Level;
	OutputLines.push_back(std::move(Entry));
	while (OutputLines.size() > MaxOutputLines)
	{
		OutputLines.pop_front();
	}
}

void FEditorLayer::StartAgentChat()
{
	namespace fs = std::filesystem;

	FAgentChatStartOptions Options;
	Options.ProjectCwd = fs::current_path().string();
	Options.Port = 8765;
	Options.bSpawnBridge = true;

	bool bAutoConnect = true;
	FConfigFile EditorIni;
	std::vector<std::filesystem::path> IniCandidates;
	IniCandidates.push_back(fs::current_path() / "Config" / "DefaultEditor.ini");
#if defined(_WIN32)
	{
		wchar_t ModulePathW[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, ModulePathW, MAX_PATH) > 0)
		{
			IniCandidates.push_back(fs::path(ModulePathW).parent_path() / "Config" / "DefaultEditor.ini");
		}
	}
#endif
	bool bIniLoaded = false;
	for (const fs::path& Candidate : IniCandidates)
	{
		if (EditorIni.Load(Candidate.string()))
		{
			bIniLoaded = true;
			AppendAgentBubble(
				EAgentChatRole::System,
				std::string("Reading editor config: ") + Candidate.string());
			break;
		}
	}
	if (bIniLoaded)
	{
		(void)EditorIni.TryGetString("Agent", "ApiKey", Options.ApiKey);
		(void)EditorIni.TryGetInt("Agent", "BridgePort", Options.Port);
		(void)EditorIni.TryGetBool("Agent", "bAutoConnect", bAutoConnect);
		// Trim whitespace / quotes around the key.
		while (!Options.ApiKey.empty()
			&& (Options.ApiKey.front() == ' ' || Options.ApiKey.front() == '"' || Options.ApiKey.front() == '\''))
		{
			Options.ApiKey.erase(Options.ApiKey.begin());
		}
		while (!Options.ApiKey.empty()
			&& (Options.ApiKey.back() == ' ' || Options.ApiKey.back() == '"' || Options.ApiKey.back() == '\''))
		{
			Options.ApiKey.pop_back();
		}
	}

	if (!bAutoConnect)
	{
		AppendAgentBubble(
			EAgentChatRole::System,
			"Agent auto-connect disabled ([Agent] bAutoConnect=False in DefaultEditor.ini).");
		return;
	}

	const fs::path EngineRoot(MAHO_ENGINE_ROOT);
	const fs::path BridgeA = EngineRoot / "Tools" / "AgentBridge";
	const fs::path BridgeB = fs::current_path() / "Tools" / "AgentBridge";
	const fs::path BridgeC = fs::current_path().parent_path() / "Maho" / "Tools" / "AgentBridge";
	if (fs::exists(BridgeA / "server.mjs"))
	{
		Options.BridgeDirectory = BridgeA.string();
	}
	else if (fs::exists(BridgeB / "server.mjs"))
	{
		Options.BridgeDirectory = BridgeB.string();
	}
	else if (fs::exists(BridgeC / "server.mjs"))
	{
		Options.BridgeDirectory = BridgeC.string();
	}
	else
	{
		Options.BridgeDirectory = BridgeA.string();
	}

	AgentChat = std::make_unique<FAgentChatClient>();
	AgentChat->Start(Options);

	std::string Hello = "Connecting to Agent bridge...";
	if (!Options.ApiKey.empty())
	{
		Hello += "\nApiKey loaded from Config/DefaultEditor.ini.";
	}
	else
	{
		Hello += "\nNo ApiKey in Config/DefaultEditor.ini — bridge will use mock mode "
			"(or CURSOR_API_KEY from the environment).";
	}
	AppendAgentBubble(EAgentChatRole::System, std::move(Hello));
}

void FEditorLayer::AppendAgentBubble(EAgentChatRole Role, std::string Text)
{
	FAgentBubble Bubble;
	Bubble.Role = Role;
	Bubble.Text = std::move(Text);
	AgentBubbles.push_back(std::move(Bubble));
	while (AgentBubbles.size() > MaxAgentBubbles)
	{
		AgentBubbles.pop_front();
	}
}

void FEditorLayer::SendAgentMessage(std::string Text)
{
	while (!Text.empty() && (Text.back() == '\n' || Text.back() == '\r' || Text.back() == ' '))
	{
		Text.pop_back();
	}
	if (Text.empty() || !AgentChat)
	{
		return;
	}
	AppendAgentBubble(EAgentChatRole::User, Text);
	AgentChat->SendUserMessage(std::move(Text));
}

void FEditorLayer::DrainEngineLogs(FEngineBase& App)
{
	std::vector<FCapturedLogLine> Captured;
	App.GetLog().DrainCapturedLines(Captured);
	for (FCapturedLogLine& Line : Captured)
	{
		AppendOutput(std::move(Line.Text), Line.Level);
	}
}

void FEditorLayer::ExecuteConsoleLine(FEngineBase& App, const std::string& RawLine)
{
	const std::string Line = TrimAscii(RawLine);
	if (Line.empty())
	{
		return;
	}

	ConsoleHistory.push_back(Line);
	AppendOutput(std::string("> ") + Line);

	std::string Lower = Line;
	std::transform(Lower.begin(), Lower.end(), Lower.begin(),
		[](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});

	if (Lower == "help" || Lower == "?")
	{
		AppendOutput("  Dump | <Name> | <Name> <Value>");
		return;
	}

	if (Lower == "dump")
	{
		FConsole& Console = App.GetConsole();
		const std::vector<std::string> Names = Console.GetNames();
		AppendOutput("Registered CVars (" + std::to_string(Names.size()) + "):");
		for (const std::string& CVarName : Names)
		{
			std::string Value;
			if (Console.TryGetString(CVarName.c_str(), Value))
			{
				AppendOutput("  " + CVarName + " = " + Value);
			}
			else
			{
				AppendOutput("  " + CVarName);
			}
		}
		Console.Dump();
		return;
	}

	std::string CVarName;
	std::string Value;
	{
		std::istringstream Stream(Line);
		Stream >> CVarName;
		std::getline(Stream, Value);
		Value = TrimAscii(Value);
	}

	FConsole& Console = App.GetConsole();
	IConsoleVariable* Variable = Console.Find(CVarName.c_str());
	if (!Variable)
	{
		AppendOutput("Unknown CVar: " + CVarName);
		return;
	}
	if (Value.empty())
	{
		AppendOutput(CVarName + " = " + Variable->GetString());
		return;
	}
	if (!Console.SetFromString(CVarName.c_str(), Value.c_str(), Maho::EConsoleVariableSetBy::Console))
	{
		AppendOutput("Failed to set " + CVarName);
		return;
	}
	AppendOutput(CVarName + " = " + Variable->GetString());
}


FEditorUIRegistry* TryGetEditorUIRegistry(FEngineBase& App)
{
	FEditorLayer* Editor = App.GetExtension<FEditorLayer>();
	if (!Editor)
	{
		return nullptr;
	}
	return &Editor->GetUIRegistry();
}

} // namespace Maho
