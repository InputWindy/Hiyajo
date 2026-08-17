#pragma once

#include "{{NAME}}Api.h"
#include <{{NAME}}.gen.h>
#include <Engine.h>

namespace Maho
{

namespace {{NAMESPACE}}
{

/** {{DESCRIPTION}} {{STAGE_LABEL}} */
class MAHO_{{EXPORT_NAME}}_API {{CLASS}}
	: public TExtension<{{STAGE}}, {{CLASS}}>
{{INHERITS_BASES}}	, public {{CLASS}}Dependencies
{
public:
{{GET_USING}}	[[nodiscard]] bool ExecuteStage({{STAGE}} Stage) override;

protected:
	friend TSingleton<{{CLASS}}>;
	{{CLASS}}() = default;
};

} // namespace {{NAMESPACE}}

} // namespace Maho
