#pragma once

#include "ArchiveApi.h"
#include <Engine.h>

namespace Maho
{

namespace Archive
{

/** Serialization archive extension. Pre-app singleton (driven by ESingletonStage). */
class MAHO_ARCHIVE_API FArchive final : public TExtension<ESingletonStage, FArchive>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FArchive>;
	FArchive() = default;
};

} // namespace Archive

} // namespace Maho
