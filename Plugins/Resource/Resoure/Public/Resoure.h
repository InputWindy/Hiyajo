#pragma once

#include "ResoureApi.h"
#include <Maho.h>
#include <Engine/Layer.h>

namespace Maho
{

// Resoure — a Layer node: FLayer<children...> + the interfaces it
// implements. Interfaces are hand-spelled as IPlugin<> template args;
// the DLL factory + module path come from MAHO_DECLARE_LAYER. Install and
// uninstall hooks are spelled OUT here — you write the lifecycle logic.
class FResoure
	: public FLayer<>
	, public IPlugin<IMain, IExit>
{
MAHO_DECLARE_LAYER(FResoure, "Resoure.dll");

	// The run entry — a layer that OWNS a loop writes while/tick/exit here;
	// a driven layer returns after one frame of work.
	int Main() override { return 0; }
	void Exit() override {}

	// Install what this layer manages (recursive). Args are the launch ones.
	void Initialize(int Argc, char** Argv) override { (void)Argc; (void)Argv; }
	void Shutdown() override {}
};

} // namespace Maho
