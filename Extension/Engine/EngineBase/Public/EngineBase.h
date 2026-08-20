#pragma once

#include "EngineBaseApi.h"
#include <Maho.h>
#include <Engine/PluginTemplates.h>

#include <type_traits>

namespace Maho
{

namespace EngineBase
{

// The minimal engine template — an installable application root with a
// parallel drive, no tools and no layers. Users pick this from the Create
// Project dropdown as the default starting point.
class FEngineBase
	: public Maho::TEngine<>
{
public:
	/** The assembly factory — the ONLY way an IAssembly is created. */
	static Maho::IAssembly* CreateExtension();

	int Main(int Argc, char** Argv) override;
};

// Compile-time contract: an IAssembly MUST provide CreateExtension.
static_assert(
	Maho::FAssemblyExport<FEngineBase>,
	"EngineBase: an IAssembly must provide static CreateExtension()");

// IAssembly / TSingleton are mutually exclusive.
static_assert(
	!std::is_base_of_v<Maho::IAssembly, FEngineBase>
	|| !std::is_base_of_v<Maho::TSingleton<FEngineBase>, FEngineBase>,
	"EngineBase: cannot inherit both IAssembly and TSingleton");

} // namespace EngineBase

} // namespace Maho
