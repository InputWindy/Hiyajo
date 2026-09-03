#pragma once

#include "ExampleEditorApi.h"
#include <Maho.h>
#include <Engine/Layer.h>
#include <Render.h>

namespace Maho
{

// ExampleEditor - an engine layer. Add the stage interfaces you implement
// to the FLayer<...> template list, e.g. FLayer<IInit, IShutdown> or
// FLayer<IBeginFrame, ITick, IEndFrame, IExit>. Each mounted stage must be
// overridden in this class.
class FExampleEditor : public FLayer<>
{
MAHO_DECLARE_LAYER(FExampleEditor, "ExampleEditor.dll");
};

} // namespace Maho
