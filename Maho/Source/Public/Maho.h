#pragma once

// Umbrella header for game projects linking Maho.
// Entry point: also #include <EntryPoint.h> in exactly one game .cpp.
//
// Example:
// ```
//   #include <Maho.h>
//   #include <EntryPoint.h>
//
//   class FMyGameApp : public Maho::FGameEngine;
//   Maho::FEngineBase* Maho::CreateEngine() { return new FMyGameApp(); }
// ```
#include <Core/Misc/Export.h>
#include <Core/Misc/Log.h>
#include <Core/Misc/Fatal.h>
#include <Core/Misc/Timer.h>
#include <Core/Misc/ConfigFile.h>
#include <Core/Misc/Json.h>
#include <Core/Misc/ConsoleVariable.h>
#include <Core/Misc/Console.h>
#include <Core/Misc/Delegate.h>
#include <Core/Misc/AsyncTask.h>
#include <Core/Engine/Engine.h>
#include <Core/Misc/Paths.h>
#include <Core/Engine/EngineExtension.h>
#include <Core/EngineBase.h>
#include <Core/Engine/GameEngine.h>
#include <Core/Extension/Platform/Platform.h>
#include <Core/Extension/Resource/ResourceSystem.h>
#include <Core/Extension/Script/ScriptSystem.h>
#include <Core/Extension/World/ECS/SystemGroup.h>
#include <Core/Extension/Platform/PlatformWindow.h>
#include <Render/RHI/RHI.h>
#include <Render/RHI/RHIServer.h>
#include <Render/RenderSystem.h>
#include <Render/RenderPipelineStage.h>
#include <Render/RenderFeature.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/Server/ServerTask.h>
#include <Core/Server/TaskContext.h>
#include <Core/Extension/World/ECS/DOTS.h>
#include <Render/UI/ImGuiSystem.h>
#include <Render/UI/ImGuiTheme.h>

#if defined(MAHO_WITH_IMGUI)
#	include <imgui.h>
#	include <Render/UI/ImGuiExtensions.h>
#endif
