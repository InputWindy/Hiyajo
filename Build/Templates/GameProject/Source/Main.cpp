#include <Maho.h>
#include <AssemblyImporter.h>
#include <{{ENTRY_POINT_INCLUDE}}>

// Thin launcher driver: install the AssemblyImporter (linked), import the two
// aggregate assemblies, and run the engine's main loop. No project logic here.
// The platform entry shim (included above) calls this.
int Maho::RunDynamic(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

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

	Maho::IExtension<Maho::EEngineStage>* Engine = Importer.GetEngine();
	if (Engine == nullptr)
	{
		return 1;
	}
	Maho::IRunable* Runable = dynamic_cast<Maho::IRunable*>(Engine);
	if (Runable == nullptr)
	{
		return 1;
	}
	Runable->MainLoop();

	Importer.ExecuteStage(Maho::EEngineStage::Shutdown);
	return 0;
}
