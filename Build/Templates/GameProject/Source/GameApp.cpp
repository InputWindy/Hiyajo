#include <Maho.h>
#include <EntryPoint.h>

#include <memory>

class {{APP_CLASS}} : public Maho::FApp
{
protected:
	virtual void Configure(Maho::FConfig& OutConfig) override
	{
		OutConfig.ApplicationName = "{{PROJECT_NAME}}";
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

	virtual bool PreInitialize() override
	{
		using Maho::EExtensionPriority;

		RegisterExtension<Maho::FPlatformSystem>(EExtensionPriority::System);
		RegisterExtension<Maho::FRenderSystem>(EExtensionPriority::System);
		RegisterExtension<Maho::FResourceSystem>(EExtensionPriority::System);
		RegisterExtension<Maho::FScriptSystem>(EExtensionPriority::Overlay);

		return true;
	}

	virtual bool PostInitialize() override
	{
		auto* RenderSystem = GetExtension<Maho::FRenderSystem>();
		if (RenderSystem)
		{
			auto& Server = RenderSystem->GetRenderServer();
			Server.RegisterFeature<Maho::FTriangleBasePassFeature>();
		}
		return true;
	}
};

Maho::FApp* Maho::CreateApplication()
{
	return new {{APP_CLASS}}();
}
