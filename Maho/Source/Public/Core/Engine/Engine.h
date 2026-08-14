#pragma once

#include <Core/Misc/Export.h>
#include <Core/Extension/Platform/PlatformWindow.h>

#include <string>

namespace Maho
{

/**
 * App / module configuration owned by FEngineBase (not a separate FEngine instance).
 * Paths may be relative until FPaths::Initialize absolutizes them from project/engine roots.
 */
struct FConfig
{
	std::string ApplicationName = "MahoApp";

	/** Absolute or relative roots filled by FPaths::Initialize (and/or Configure). */
	std::string ProjectDir;
	std::string EngineDir;

	std::string EngineShadersDir = "Engine/Shaders";
	std::string ProjectShadersDir = "Shaders";
	std::string EnginePluginsDir = "Engine/Plugins";
	std::string ProjectPluginsDir = "Plugins";
	std::string ProjectContentDir = "Content";
	/** UE-style: regenerable derived data (shader cache, etc.). */
	std::string CachedDir = "Cached";
	/** UE-style: logs / config / crashes / screenshots. */
	std::string SavedDir = "Saved";
	/** Project Config/ directory (DefaultEngine.ini, etc.). */
	std::string ProjectConfigDir = "Config";
	/** Project Scripts/ directory (Lua game logic). */
	std::string ProjectScriptsDir = "Scripts";

	/** Main window. Ignored when bCreateMainWindow is false. */
	EPlatform Platform = EPlatform::Glfw;
	int WindowWidth = 1280;
	int WindowHeight = 720;
	bool bCreateMainWindow = true;
	bool bResizableWindow = true;

	/** Clear color used by FRenderSystem default clear (Boot caches from config). */
	float ClearColorR = 0.08f;
	float ClearColorG = 0.10f;
	float ClearColorB = 0.16f;
	float ClearColorA = 1.0f;
};

} // namespace Maho
