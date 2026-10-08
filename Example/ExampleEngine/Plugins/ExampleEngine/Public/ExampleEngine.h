#pragma once

#include "ExampleEngineApi.h"
#include <Maho.h>
#include <Log.h>

#include <cstdint>

namespace Maho
{

// ExampleEngine - the application root (an FEngineBase). PreMain installs the
// engine service layers (Log/Config/Platform/Resource/Script/Render); the
// render subsystem (FRender) drives the Scene/DrawTriangleFeature features on
// its own render thread, and FPlatform requests exit when the window closes.
class FExampleEngine : public FEngineBase
{
MAHO_DECLARE_ENGINE(FExampleEngine);

public:
	void PreMain() override;

	void PostMain() override;

	/** THE HOST'S FRAME SOURCE: the canonical frame number, which lives in the Platform plugin
	 *  (`GetFrameIndex`) and is advanced ONCE per driven frame by `FPlatform::IBeginFrame`. The
	 *  engine core cannot read it (it must not include a plugin header), so the host -- this class
	 *  -- is where it enters the engine: `Main` calls this exactly ONCE per frame and hands the
	 *  same number to every sequence it drives, so a frame's sequences cannot land in different
	 *  rounds. See FEngineBase::NextFrameNumber. */
	[[nodiscard]] std::uint64_t NextFrameNumber() override;
};

} // namespace Maho
