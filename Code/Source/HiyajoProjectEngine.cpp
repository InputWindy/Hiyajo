#include <Maho.h>
#include <EntryPoint.h>

#include <Platform.h>
#include <ResourceSystem.h>
#include <ScriptSystem.h>

#include "Game/FGameWorldLayer.h"
#include "Render/Forward/ForwardRendererFeature.h"
#include "Resource/ResourceFactories.h"

#if defined(GAME_WITH_EDITOR) && defined(MAHO_WITH_IMGUI)
#	include "Editor/EditorLayer.h"
#endif

#include <memory>

class FHiyajoProjectEngine : public Maho::FGameClientEngine
{
protected:
	virtual void Configure(Maho::FConfig& OutConfig) override
	{
		OutConfig.ApplicationName = "Hiyajo-Project";
		// Relative dirs — FPaths::Initialize turns them into absolute under Project/Engine roots.
		OutConfig.EngineShadersDir = "Engine/Shaders";
		OutConfig.ProjectShadersDir = "Shaders";
		OutConfig.EnginePluginsDir = "Engine/Plugins";
		OutConfig.ProjectPluginsDir = "Plugins";
		OutConfig.ProjectContentDir = "Content";
		OutConfig.CachedDir = "Cached";
		OutConfig.SavedDir = "Saved";
		OutConfig.ProjectConfigDir = "Config";
		OutConfig.ProjectScriptsDir = "Scripts";
	}

	Maho::FSystemGroup* CreateWorld() override
	{
		return new FGameWorldLayer();
	}

	virtual bool PreInitialize() override
	{
		if (!Maho::FGameClientEngine::PreInitialize())
		{
			return false;
		}

		using Maho::EExtensionPriority;

		RegisterExtension<Maho::FPlatformSystem>(EExtensionPriority::System);
		RegisterExtension<Maho::FResourceSystem>(EExtensionPriority::System);
		RegisterExtension<Maho::FScriptSystem>(EExtensionPriority::Overlay);

#if defined(GAME_WITH_EDITOR) && defined(MAHO_WITH_IMGUI)
		// RegisterExtension<Maho::FEditorLayer>(EExtensionPriority::Overlay);
#endif

		return true;
	}

	virtual bool PostInitialize() override
	{
		auto* RenderSystem = GetExtension<Maho::FRenderSystem>();
		if (RenderSystem)
		{
			RenderSystem->RegisterFeature<Maho::FForwardRendererFeature>();
		}
		Maho::RegisterResourceFactories();
		return true;
	}
};

Maho::FEngineBase* Maho::CreateEngine()
{
	return new FHiyajoProjectEngine();
}
