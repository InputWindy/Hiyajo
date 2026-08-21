#pragma once

#include "EngineBaseApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

#include <type_traits>

namespace Maho
{

namespace EngineBase
{

// The minimal Layer — an installable application root with a parallel drive,
// no tools and no child layers. Engine and Layer are unified into one template
// (TLayer): this is the default starting point picked from Create Project.
class FEngineBase
	: public Maho::TLayer<>
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
