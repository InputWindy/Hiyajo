#pragma once

#include "TestPluginApi.h"
#include <TestPlugin.gen.h>
#include <Engine.h>

namespace Maho
{

namespace TestPlugin
{

/** CoreMinimal pre-app toolkit (driven by EToolStage) */
class MAHO_TESTPLUGIN_API FTestPlugin
	: public TExtension<EToolStage, FTestPlugin>
	, public Maho::Archive::FArchiveSystem
	, public Maho::Asset::FAssetRegistry
	, public Maho::Audio::FAudio
	, public Maho::CommandParser::FCommandParser
	, public Maho::Compress::FCompress
	, public Maho::Config::FConfig
	, public Maho::ConsoleVariable::FConsoleVariable
	, public Maho::Json::FJson
	, public Maho::Log::FLogger
	, public Maho::Math::FMath
	, public Maho::Name::FNamePool
	, public Maho::Paths::FPaths
	, public Maho::Physics::FPhysics
	, public Maho::Text::FTextManager
	, public Maho::Timer::FTimer
	, public Maho::Unicode::FUnicode
	, public FTestPluginDependencies
{
public:
	using TSingleton<FTestPlugin>::Get;
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

protected:
	friend TSingleton<FTestPlugin>;
	FTestPlugin() = default;
};

} // namespace TestPlugin

} // namespace Maho
