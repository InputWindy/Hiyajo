#pragma once

#include "AudioApi.h"
#include <Engine.h>

namespace Maho
{

namespace Audio
{

/** Audio playback library extension (device + sources). Pre-app toolkit (driven by EToolStage). */
class MAHO_AUDIO_API FAudio final : public TExtension<EToolStage, FAudio>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FAudio>;
	FAudio() = default;
};

} // namespace Audio

} // namespace Maho
