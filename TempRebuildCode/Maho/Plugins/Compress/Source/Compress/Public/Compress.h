#pragma once

#include "CompressApi.h"
#include <Engine.h>

namespace Maho
{

namespace Compress
{

/** Compression extension (zlib/zstd). Pre-app singleton (driven by ESingletonStage). */
class MAHO_COMPRESS_API FCompress final : public TExtension<ESingletonStage, FCompress>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FCompress>;
	FCompress() = default;
};

} // namespace Compress

} // namespace Maho
