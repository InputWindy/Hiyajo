#pragma once

#include "CompressApi.h"
#include <Core/Core.h>

namespace Maho
{

namespace Compress
{

/** Compression extension (zlib/zstd). Pre-app toolkit (driven by EToolStage). */
class MAHO_COMPRESS_API FCompress : public TExtension<EToolStage, FCompress>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

protected:
	friend TSingleton<FCompress>;
	FCompress() = default;
};

} // namespace Compress

} // namespace Maho
