#pragma once

#include <Core/EngineBase.h>

namespace Maho
{

/**
 * Null engine shell: FEngineBase with no extensions registered.
 *
 * Use as the smallest possible engine — no game world, no render world, no
 * plugins. PreInitialize registers nothing; Tick() only drives any extension
 * mounted at runtime (none by default).
 */
class FNullEngine : public FEngineBase
{
protected:
	bool PreInitialize() override
	{
		return true;
	}

	void Tick() override
	{
		// No world / render framing; still drive runtime-mounted extensions.
		DispatchStageToExtensions(EEngineStage::Tick);
	}
};

} // namespace Maho
