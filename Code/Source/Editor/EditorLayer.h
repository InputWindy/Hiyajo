#pragma once

#include "Editor/AgentChatClient.h"
#include "Editor/EditorUIRegistry.h"
#include <ResourceSystem.h>
#include <ECS/SystemGroup.h>
#include <ECS/EntityManager.h>
#include <ECS/EntityHandle.h>
#include "Game/FGameWorldLayer.h"
#include "Game/Components/StaticMeshComponent.h"
#include "Game/Components/SkeletonComponent.h"
#include "Game/Components/AnimationComponent.h"
#include "Game/Components/CameraComponent.h"
#include "Game/Components/MaterialComponent.h"
#include "Game/Components/ScriptComponent.h"
#include "Game/Components/MainCameraTag.h"
#include "Game/Components/TransformComponent.h"
#include <Core/Engine/EngineExtension.h>
#include <Core/Misc/Log.h>
#include <Core/Engine/EngineStage.h>
#include <UI/ImGuiSystem.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Maho
{

class FAgentChatClient;

/**
 * Engine editor shell (UE-inspired, ImGui docking + extensions).
 * Games RegisterExtension with Priority Overlay when GAME_WITH_EDITOR is enabled.
 * Chrome geometry stays here; region contributions live in FEditorUIRegistry.
 */
class FEditorLayer final : public FLayer
{
public:
	enum class EPlayState : std::uint8_t
	{
		Stopped,
		Playing,
		Paused
	};

	enum class EViewportTool : std::uint8_t
	{
		Select,
		Translate,
		Rotate,
		Scale
	};

	FEditorLayer();
	~FEditorLayer() override;

	bool ExecuteStage(EEngineStage Stage) override;

	[[nodiscard]] FEditorUIRegistry& GetUIRegistry()
	{
		return UIRegistry;
	}
	[[nodiscard]] const FEditorUIRegistry& GetUIRegistry() const
	{
		return UIRegistry;
	}
	[[nodiscard]] bool IsDummyUIEnabled() const
	{
		return bShowDummyUI;
	}

private:
	struct FContentAssetEntry
	{
		std::string CatalogKey;
		std::string DisplayName;
		EResourceType Type = EResourceType::Unknown;
		EResourceLoadState LoadState = EResourceLoadState::Invalid;
		bool bDirty = false;
	};

	struct FManualImportJob
	{
		std::string SourcePath;
		std::string PackagePath;
		std::string ObjectName;
		EAssetType TypeHint = EAssetType::Unknown;
		std::string SoftPath;
		bool bKicked = false;
	};

	struct FImporterDialogState
	{
		bool bOpen = false;
		std::string SourcePath;
		std::string PackagePath;
		std::string ObjectName;
		EAssetType TypeHint = EAssetType::Unknown;
		char PackagePathEdit[512] = {};
		char ObjectNameEdit[256] = {};
	};

	struct FResourceBrowserWindow
	{
		std::string CatalogKey;
		EResourceType Type = EResourceType::Unknown;
		bool bOpen = true;
		FImGuiTextureHandle PreviewTexture;
		std::uint64_t PreviewGeneration = 0;
	};

	/** When set, next DrawOpenResourceBrowsers selects this catalog tab. */
	std::string ResourceBrowserFocusKey;

	void MountEditor();
	void UnmountEditor();
	void RegisterBuiltinUIContributions();
	void RegisterDummyUIContributions();
	[[nodiscard]] FEditorUIDrawContext MakeUIDrawContext(FEngineBase& App);

	void DrawMenuItems(FEngineBase& App, float RowH);
	void DrawBrandBlock(float Size);
	void DrawToolbarPrimary();
	void DrawToolbarSecondary();
	void DrawDockSpace(FEngineBase& App);
	void DrawMainViewportPanel();
	void DrawContentBrowser();
	void DrawContentBrowserTree();
	void DrawContentBrowserTiles();
	void DrawOutputPanel(FEngineBase& App);
	void DrawAgentPanel();
	void DrawBlueprintPanel();
	void DrawSequenceGraphPanel(FEngineBase& App);
	void DrawPlotPanel();
	void DrawTransientDetailsPanel();
	void DrawSceneOutliner();
	void DrawInspectorPanel();
	void DrawWallpaperPanel();
	void DrawFileDialogs();
	void EnsureSequenceGraphNodeLayout();
	void EnsureSequenceGraphNodeLayout(const std::vector<IEngineExtension*>& Extensions);

	void ProcessEditorFileDrops(FEngineBase& App);
	[[nodiscard]] bool TryApplyWallpaperFromPath(FEngineBase& App, const std::string& Path);
	void ClearWallpaper(FEngineBase& App);
	void EnsureDefaultWallpaper(FEngineBase& App);
	[[nodiscard]] static std::string ResolveDefaultWallpaperPath();

	void EnsureContentMounts();
	void SelectContentFolder(const std::string& VirtualPath);
	void RefreshContentListing();
	void DrawVirtualFolderTree(const std::string& VirtualPath);
	[[nodiscard]] bool IsContentBrowserInputLocked() const;
	void CollectChildFolders(const std::string& ParentVirtualPath, std::vector<std::string>& OutFolders) const;

	void StartStartupCassetLoad();
	void TickStartupCassetLoad();
	void OpenImporterDialog(const std::string& SourcePath);
	void DrawImporterDialog();
	void ConfirmImporterDialog();
	void TickManualContentImport();
	void DrawContentImportProgressOverlay();
	[[nodiscard]] bool SaveContentAsset(const std::string& CatalogKey);
	void OpenResourceBrowser(const std::string& CatalogKey);
	void DrawOpenResourceBrowsers(FEngineBase& App);
	void DrawResourceBrowserBody(FResourceBrowserWindow& Window, FResource& Resource, FEngineBase& App);
	void ReleaseResourceBrowserPreview(FResourceBrowserWindow& Window, FEngineBase& App);

	void AppendOutput(std::string Line, spdlog::level::level_enum Level = spdlog::level::info);
	void DrainEngineLogs(FEngineBase& App);
	void ExecuteConsoleLine(FEngineBase& App, const std::string& Line);
	void EnsureDefaultDockLayout(std::uint32_t DockspaceId);

	void StartAgentChat();
	void AppendAgentBubble(EAgentChatRole Role, std::string Text);
	void SendAgentMessage(std::string Text);

	FEditorUIRegistry UIRegistry;

	EPlayState PlayState = EPlayState::Stopped;
	bool bShowDemoWindow = false;
	bool bShowImPlotDemo = false;
	bool bShowDummyUI = true;
	bool bShowContentBrowser = true;
	bool bShowOutputPanel = true;
	bool bShowAgentPanel = true;
	bool bShowBlueprintPanel = true;
	bool bShowSequenceGraphPanel = true;
	bool bShowPlotPanel = true;
	bool bShowWallpaperPanel = true;
	bool bShowOutliner = true;
	bool bShowInspector = true;
	FEntityHandle SelectedEntity{};
	bool bShowDummyDockA = false;
	bool bShowDummyDockB = false;
	bool bAutoScrollOutput = true;
	bool bAutoScrollAgent = true;
	bool bBuildDefaultLayout = true;
	int SequenceGraphViewMode = 0;
	int SequenceGraphStage = static_cast<int>(EEngineStage::Tick);
	std::size_t SequenceGraphLayoutExtCount = 0;
	int SequenceGraphLayoutStage = -1;

	std::string CurrentVirtualPath = "/Game";
	std::string SelectedVirtualEntry;
	std::vector<std::string> FolderVirtualEntries;
	std::vector<FContentAssetEntry> AssetEntries;
	bool bContentBrowserRefreshing = false;

	bool bStartupCassetLoadStarted = false;
	bool bStartupCassetLoadActive = false;
	bool bStartupCassetScanDone = false;
	/** Frames of full editor UI drawn since mount; casset load waits until this reaches kEditorUiSettleFrames. */
	std::uint32_t EditorUiPresentedFrames = 0;
	static constexpr std::uint32_t kEditorUiSettleFrames = 2;
	std::vector<std::string> StartupCassetPaths;
	std::vector<std::string> StartupCassetSoftPaths;
	std::size_t StartupCassetNextIndex = 0;
	std::size_t StartupCassetLoaded = 0;
	bool bManualImportActive = false;
	std::vector<FManualImportJob> ManualImportJobs;
	std::size_t ManualImportKickIndex = 0;
	std::size_t ManualImportCompleted = 0;
	std::size_t ManualImportFailed = 0;
	std::string ManualImportCurrentName;
	bool bWasSavePackageBusy = false;
	FImporterDialogState ImporterDialog;

	float ContentBrowserDropMinX = 0.0f;
	float ContentBrowserDropMinY = 0.0f;
	float ContentBrowserDropMaxX = 0.0f;
	float ContentBrowserDropMaxY = 0.0f;
	bool bContentBrowserDropRectValid = false;

	std::vector<FResourceBrowserWindow> OpenResourceBrowsers;

	struct FOutputLine
	{
		std::string Text;
		spdlog::level::level_enum Level = spdlog::level::info;
	};

	std::deque<FOutputLine> OutputLines;
	char ConsoleInput[512] = {};
	std::vector<std::string> ConsoleHistory;
	static constexpr std::size_t MaxOutputLines = 2000;

	struct FAgentBubble
	{
		EAgentChatRole Role = EAgentChatRole::System;
		std::string Text;
	};
	std::deque<FAgentBubble> AgentBubbles;
	char AgentInput[2048] = {};
	std::unique_ptr<FAgentChatClient> AgentChat;
	static constexpr std::size_t MaxAgentBubbles = 500;

	float ViewMatrix[16] = {};
	float ProjectionMatrix[16] = {};
	float ObjectMatrix[16] = {};
	EViewportTool ViewportTool = EViewportTool::Select;
	int GizmoOperation = 7;

	void* NodeEditorContext = nullptr;
	bool bBlueprintInited = false;

	void* SequenceGraphEditorContext = nullptr;
	bool bSequenceGraphEditorInited = false;
	bool bSequenceGraphLayoutApplied = false;
	bool bEditorMounted = false;

	FImGuiTextureHandle WallpaperTexture;
	std::string WallpaperSourcePath;
	float WallpaperDropMinX = 0.0f;
	float WallpaperDropMinY = 0.0f;
	float WallpaperDropMaxX = 0.0f;
	float WallpaperDropMaxY = 0.0f;
	bool bWallpaperDropRectValid = false;
	bool bDefaultWallpaperAttempted = false;
};

} // namespace Maho
