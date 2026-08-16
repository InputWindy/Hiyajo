#include <{{NAME}}.h>

namespace Maho
{

namespace {{NAMESPACE}}
{

bool {{CLASS}}::ExecuteStage({{STAGE}} Stage)
{
	// TODO: per-stage behavior.
	(void)Stage;
	return true;
}

} // namespace {{NAMESPACE}}

} // namespace Maho

{{FACTORY_BLOCK}}