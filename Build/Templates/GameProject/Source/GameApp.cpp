#include <Maho.h>
#include <EntryPoint.h>
{{PLUGIN_INCLUDES}}

#include <memory>

class {{APP_CLASS}} : public {{ENGINE_BASE_CLASS}}
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
		if (!{{ENGINE_BASE_CLASS}}::PreInitialize())
		{
			return false;
		}

		using Maho::EExtensionPriority;
{{EXTENSION_REGISTRATIONS}}

		return true;
	}
};

Maho::FEngineBase* Maho::CreateEngine()
{
	return new {{APP_CLASS}}();
}
