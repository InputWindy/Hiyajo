#pragma once

// Umbrella header for game projects linking Maho.
// Entry point: also #include <EntryPoint.h> in exactly one game .cpp.
//
// Example:
// ```
//   #include <Maho.h>
//   #include <EntryPoint.h>
//
//   class FMyGameApp : public Maho::FApp;
//   Maho::FApp* Maho::CreateApplication() { return new FMyGameApp(); }
// ```
#include <Core/Export.h>
#include <Core/System/Log.h>
#include <Core/System/Fatal.h>
#include <Core/System/Timer.h>
#include <Core/System/ConfigFile.h>
#include <Core/Json.h>
#include <Core/System/ConsoleVariable.h>
#include <Core/System/Console.h>
#include <Core/Delegate.h>
#include <Core/Concurrent/AsyncTask.h>
#include <Core/Engine.h>
#include <Core/System/Paths.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Application/App.h>
#include <Core/Extension/Platform/Platform.h>
#include <Core/Extension/Render/Render.h>
#include <Core/System/PlatformWindow.h>
#include <Render/RHI/RHI.h>
#include <Render/RHI/RHIServer.h>
#include <Render/RenderServer.h>
#include <Render/RenderPipelineStage.h>
#include <Render/SceneUpdatePacket.h>
#include <Render/Sequencer/RenderFeature.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/Server/ServerTask.h>
#include <Core/Server/TaskContext.h>
#include <Render/UI/ImGuiSystem.h>
#include <Render/UI/ImGuiTheme.h>

#if defined(MAHO_WITH_IMGUI)
#	include <imgui.h>
#	include <Render/UI/ImGuiExtensions.h>
#endif
