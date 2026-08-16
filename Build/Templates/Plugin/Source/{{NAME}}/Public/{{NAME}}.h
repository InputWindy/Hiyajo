#pragma once

#include "{{NAME}}Api.h"
#include <Engine.h>

namespace Maho
{

namespace {{NAMESPACE}}
{

/** {{DESCRIPTION}} {{STAGE_LABEL}} */
class MAHO_{{EXPORT_NAME}}_API {{CLASS}} final : public TExtension<{{STAGE}}, {{CLASS}}>
{
public:
	[[nodiscard]] bool ExecuteStage({{STAGE}} Stage) override;

private:
	friend TSingleton<{{CLASS}}>;
	{{CLASS}}() = default;
};

} // namespace {{NAMESPACE}}

} // namespace Maho
