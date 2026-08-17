#include <Maho.h>
#include <AssemblyImporter.h>

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

// Thin launcher: install the AssemblyImporter (linked), import the two
// aggregate assemblies, and run the engine's main loop. No project logic here.

static int RunDynamic()
{
	Maho::InstallFatalHandlers();

	auto& Importer = Maho::AssemblyImporter::FAssemblyImporter::Get();
	if (!Importer.ImportToolkit("{{PROJECT_NAME}}Toolkit.dll"))
	{
		return 1;
	}
	if (!Importer.ImportEngine("{{PROJECT_NAME}}Engine.dll"))
	{
		return 1;
	}

	Maho::IRunable* Engine = Importer.GetEngine();
	if (Engine == nullptr)
	{
		return 1;
	}
	Engine->MainLoop();

	Importer.ExecuteStage(Maho::EEngineStage::Shutdown);
	return 0;
}

int WINAPI WinMain(HINSTANCE /*Instance*/, HINSTANCE /*Prev*/, LPSTR /*CmdLine*/, int /*Show*/)
{
	FreeConsole();
	return RunDynamic();
}

int main(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;
	return RunDynamic();
}
