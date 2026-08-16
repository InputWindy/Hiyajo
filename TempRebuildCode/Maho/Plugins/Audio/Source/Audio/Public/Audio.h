#pragma once

#include "AudioApi.h"
#include <Engine.h>

namespace Maho
{

namespace Audio
{

/** Audio playback library extension (device + sources). Pre-app singleton (driven by ESingletonStage). */
class MAHO_AUDIO_API FAudio final : public TExtension<ESingletonStage, FAudio>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FAudio>;
	FAudio() = default;
};

} // namespace Audio

} // namespace Maho
