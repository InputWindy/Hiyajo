#pragma once

#include <Core/Export.h>

namespace Maho
{

/**
 * Runnable contract — anything with a Main entry. Compatible with TSingleton
 * (a Renderer can be a singleton AND runnable); it says nothing about how the
 * object is created.
 */
class MAHO_API IRunable
{
public:
	virtual ~IRunable() = default;

	virtual int Main(int Argc, char** Argv) = 0;
};

} // namespace Maho
