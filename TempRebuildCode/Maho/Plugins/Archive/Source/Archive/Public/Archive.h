#pragma once

#include "ArchiveApi.h"
#include <Engine.h>

namespace Maho
{

namespace Archive
{

/** Serialization archive extension. Pre-app toolkit (driven by EToolStage). */
class MAHO_ARCHIVE_API FArchive final : public TExtension<EToolStage, FArchive>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FArchive>;
	FArchive() = default;
};

} // namespace Archive

} // namespace Maho
