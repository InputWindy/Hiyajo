#include <Maho.h>
#include <{{ENTRY_POINT_INCLUDE}}>

{{PLUGIN_INCLUDES}}

// Code-gen: extension template lists filled from the .cproject plugin config.
using FEnabledToolExtensions = {{TOOL_EXTENSIONS}};
using FEnabledEngineExtensions = {{ENGINE_EXTENSIONS}};

// ─────────────────────────────────────────────────────────────
// Pre-app toolkit (serial drive, EToolStage).
// ctor: ParseCommandLine → Init; dtor: Shutdown.
// ─────────────────────────────────────────────────────────────
class {{TOOLKIT_CLASS}} final
	: public Maho::FToolkitBase
	, public FEnabledToolExtensions
{
public:
	{{TOOLKIT_CLASS}}(int Argc, char** Argv)
	{
		ParseCommandLine(Argc, Argv);
		Init();
	}

	~{{TOOLKIT_CLASS}}() override
	{
		Shutdown();
	}

protected:
	void ParseCommandLine(int Argc, char** Argv) override
	{
{{PARSE_COMMAND_LINE}}
	}

	void Init() override
	{
		Execute<Maho::EToolStage::Init, FList>();
	}

	void Shutdown() override
	{
		Execute<Maho::EToolStage::Shutdown, FList, Maho::FReverseTopology>();
	}
};

// ─────────────────────────────────────────────────────────────
// Engine (parallel drive, EEngineStage).
// ctor: ParseCommandLine; each stage drives its assembled FList.
// ─────────────────────────────────────────────────────────────
class {{APP_CLASS}} final
	: public Maho::FEngineBase
	, public FEnabledEngineExtensions
{
public:
	{{APP_CLASS}}(int Argc, char** Argv)
	{
		ParseCommandLine(Argc, Argv);
	}

protected:
	void ParseCommandLine(int Argc, char** Argv) override
	{
{{PARSE_COMMAND_LINE}}
	}

	void PreInit() override      { Execute<Maho::EEngineStage::PreInit, FList>(); }
	void Init() override         { Execute<Maho::EEngineStage::Init, FList>(); }
	void PostInit() override     { Execute<Maho::EEngineStage::PostInit, FList>(); }
	void PreTick() override      { Execute<Maho::EEngineStage::PreTick, FList>(); }
	void Tick() override         { Execute<Maho::EEngineStage::Tick, FList>(); }
	void PostTick() override     { Execute<Maho::EEngineStage::PostTick, FList>(); }
	void PreShutdown() override  { Execute<Maho::EEngineStage::PreShutdown, FList, Maho::FReverseTopology>(); }
	void Shutdown() override     { Execute<Maho::EEngineStage::Shutdown, FList, Maho::FReverseTopology>(); }
	void PostShutdown() override { Execute<Maho::EEngineStage::PostShutdown, FList, Maho::FReverseTopology>(); }
};

Maho::FToolkitBase* Maho::CreateToolkit(int Argc, char** Argv)
{
	return new {{TOOLKIT_CLASS}}(Argc, Argv);
}

Maho::IRunable* Maho::CreateEngine(int Argc, char** Argv)
{
	return new {{APP_CLASS}}(Argc, Argv);
}
