#pragma once

#include <ResourceIO.h>

namespace Maho
{

/** Project-side .casset package hydrate (MCAS binary BulkData -> LoadPackageFromBinary). */
class FCassetPackageImporter final : public IResourceImporter
{
public:
	[[nodiscard]] EAssetType GetType() const override { return EAssetType::Data; }
	[[nodiscard]] bool MatchesSourcePath(const std::string& SourcePath) const override;
	[[nodiscard]] bool ApplyBulkData(
		FResourceSystem& Manager,
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk) override;
};

} // namespace Maho
