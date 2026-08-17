#pragma once

#include <Maho.h>

namespace Maho
{

/**
 * Project-defined dynamic app driver (codegen'd into the thin Main.cpp).
 * Loads the AssemblyImporter + the project's two aggregate assemblies, then
 * runs the engine's main loop. Each platform entry shim calls this.
 */
int RunDynamic(int Argc, char** Argv);

} // namespace Maho
