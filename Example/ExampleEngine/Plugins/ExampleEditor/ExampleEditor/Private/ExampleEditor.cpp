#include "ExampleEditor.h"

namespace Maho
{

// ExampleEditor - implementation. Override your mounted stages here.

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_EXAMPLEEDITOR_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FExampleEditor::CreateLayer();
}
