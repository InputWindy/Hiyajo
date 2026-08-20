#include "EngineBase.h"

namespace Maho
{

namespace EngineBase
{

Maho::IAssembly* FEngineBase::CreateExtension()
{
	return new FEngineBase();
}

int FEngineBase::Main(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;
	return 0;
}

} // namespace EngineBase

} // namespace Maho

#if defined(_WIN32)
#	define MAHO_GAME_EXPORT __declspec(dllexport)
#else
#	define MAHO_GAME_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{
	MAHO_GAME_EXPORT Maho::IAssembly* CreateExtension()
	{
		return Maho::EngineBase::FEngineBase::CreateExtension();
	}
}
