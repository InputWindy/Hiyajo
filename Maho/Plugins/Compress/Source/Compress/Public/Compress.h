#pragma once

#include "CompressApi.h"
#include <Engine.h>

namespace Maho
{

namespace Compress
{

/** Compression extension (zlib/zstd). Pre-app toolkit (driven by EToolStage). */
class MAHO_COMPRESS_API FCompress final : public TExtension<EToolStage, FCompress>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FCompress>;
	FCompress() = default;
};

} // namespace Compress

} // namespace Maho
