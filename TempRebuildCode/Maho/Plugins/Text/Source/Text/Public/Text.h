#pragma once

#include "TextApi.h"
#include <Engine.h>

namespace Maho
{

/** Text / UTF-8 encoding extension. Pre-app singleton (driven by ESingletonStage). */
class MAHO_TEXT_API FText final : public TExtension<ESingletonStage, FText>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FText>;
	FText() = default;
};

} // namespace Maho
