#pragma once

#include <Core/Export.h>
#include <Core/TypeList.h>

#include <type_traits>

namespace Maho
{

template <typename... TCapabilities>
class MAHO_API IPlugin : public virtual TCapabilities...
{
public:
	virtual ~IPlugin() = default;
};

template <typename... TStageTypes>
class MAHO_API IPipeline : public virtual TStageTypes...
{
public:
	virtual ~IPipeline() = default;

	using TStages = TTypeList<TStageTypes...>;
};

}