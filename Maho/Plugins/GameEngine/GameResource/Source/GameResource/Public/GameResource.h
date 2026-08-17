#pragma once

#include "GameResourceApi.h"
#include <GameResource.gen.h>
#include <Engine.h>

namespace Maho
{

namespace GameResource
{

/** 拓展游戏引擎所需的资源类型和导入导出逻辑 engine extension (driven by EEngineStage) */
class MAHO_GAMERESOURCE_API FGameResource
	: public TExtension<EEngineStage, FGameResource>
	, public Maho::Resource::FResourceSystem
	, public FGameResourceDependencies
{
public:
	using TSingleton<FGameResource>::Get;
	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

protected:
	friend TSingleton<FGameResource>;
	FGameResource() = default;
};

} // namespace GameResource

} // namespace Maho
