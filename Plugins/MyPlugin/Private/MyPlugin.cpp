#include "MyPlugin.h"

namespace Maho
{

FLayerBase* FMyPlugin::CreateExtension()
{
	return new FMyPlugin();
}

} // namespace Maho
