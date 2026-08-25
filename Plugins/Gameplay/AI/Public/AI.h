#pragma once

#include "AIApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include "AI.gen.h"

namespace Maho
{

// AI — a Layer node. MAHO_EXTEND_DEPS declares ordering deps (code-gen scans it
// → AI.gen.h MAHO_DEPS_FAI_FDefaultSlot). Root: AI has no deps → level 0.
class FAI
	: public FLayer<>
	, public IPlugin<IMain, IExit>
{
public:
	MAHO_EXTEND_DEPS((FDefaultSlot, FNoParent));
	using FDepends = TTypeList<FDefaultSlot, TTypeList<MAHO_DEPS_FAI_FDefaultSlot>>;

MAHO_DECLARE_LAYER(FAI, "AI.dll");

	// The run entry — a layer that OWNS a loop writes while/tick/exit here;
	// a driven layer returns after one frame of work.
	int Main() override { return 0; }
	void Exit() override {}

	// Install what this layer manages (recursive). Args are the launch ones.
	void Initialize(int Argc, char** Argv) override { (void)Argc; (void)Argv; }
	void Shutdown() override {}
};

} // namespace Maho
