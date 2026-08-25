#pragma once

#include "WorldApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <AI.h>
#include "World.gen.h"

namespace Maho
{

// World — a Layer node. MAHO_EXTEND_DEPS declares ordering deps → World.gen.h.
// World depends on FAI → level after AI.
class FWorld
	: public FLayer<>
	, public IPlugin<IMain, IExit>
{
public:
	MAHO_EXTEND_DEPS(FWorld, FDefaultSlot, (FNoParent, FAI));

MAHO_DECLARE_LAYER(FWorld, "World.dll");

	// The run entry — a layer that OWNS a loop writes while/tick/exit here;
	// a driven layer returns after one frame of work.
	int Main() override { return 0; }
	void Exit() override {}

	// Install what this layer manages (recursive). Args are the launch ones.
	void Initialize(int Argc, char** Argv) override { (void)Argc; (void)Argv; }
	void Shutdown() override {}
};

} // namespace Maho
