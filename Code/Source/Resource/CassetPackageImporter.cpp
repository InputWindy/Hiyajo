#include "Resource/CassetPackageImporter.h"

#include <Core/Misc/Log.h>

#include <cctype>
#include <unordered_set>

namespace Maho
{

bool FCassetPackageImporter::MatchesSourcePath(const std::string& SourcePath) const
{
	const std::size_t Dot = SourcePath.find_last_of('.');
	if (Dot == std::string::npos)
	{
		return SourcePath.find(".casset") != std::string::npos;
	}
	std::string Ext = SourcePath.substr(Dot);
	for (char& Ch : Ext)
	{
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	}
	return Ext == ".casset";
}

bool FCassetPackageImporter::ApplyBulkData(
	FResourceSystem& Manager,
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk)
{
	const FPackageCodec* Codec = Manager.GetPackageCodec();
	if (!Codec || !Codec->IsBinary(Bulk.Bytes.data(), Bulk.Bytes.size()))
	{
		MAHO_CORE_ERROR("FCassetPackageImporter: not a casset file '{}'", Config.SourcePath);
		return false;
	}

	std::unordered_set<std::string> LoadingFilePaths;
	std::string PkgName = Manager.LoadPackageFromBinary(
		Config.SourcePath,
		Bulk.Bytes.data(),
		Bulk.Bytes.size(),
		LoadingFilePaths);

	return !PkgName.empty();
}

} // namespace Maho
