#pragma once

/**
 * Platform entry point for Maho game executables.
 * Include this header in exactly one .cpp of the game project.
 *
 * FEngineBase ctor assigns Maho::GEngine. MAHO_* / CVar / Timer resolve through GEngine.
 *
 * On Windows the game is typically linked as a GUI app (WIN32_EXECUTABLE) so no
 * console black box appears; MAHO_LOG lines go to the editor Output Log instead.
 */

#include <Core/EngineBase.h>
#include <Core/Misc/Fatal.h>

#include <cstdio>
#include <exception>
#include <string>

namespace
{

int MahoMain(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

	Maho::InstallFatalHandlers();

	Maho::FEngineBase* App = nullptr;
	try
	{
		App = Maho::CreateEngine();
		if (!App)
		{
			Maho::ReportFatal("CreateEngine returned null");
		}

		App->Run();
		delete App;
		App = nullptr;
		return 0;
	}
	catch (const std::exception& Exception)
	{
		delete App;
		App = nullptr;
		const std::string Message = std::string("Unhandled exception: ") + Exception.what();
		Maho::ReportFatal(Message.c_str());
	}
	catch (...)
	{
		delete App;
		App = nullptr;
		Maho::ReportFatal("Unhandled unknown exception");
	}
}

} // namespace

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <Windows.h>
int WINAPI WinMain(HINSTANCE /*Instance*/, HINSTANCE /*Prev*/, LPSTR /*CmdLine*/, int /*Show*/)
{
	// Detach any inherited / debugger console so the OS black box stays hidden.
	FreeConsole();

	return MahoMain(__argc, __argv);
}
#endif

int main(int Argc, char** Argv)
{
	return MahoMain(Argc, Argv);
}
