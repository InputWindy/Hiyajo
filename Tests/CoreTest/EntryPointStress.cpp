// Compile check: EntryPoint.h's Main() — the bug was auto Create getting a
// double pointer (GetProc<T> returned T*). Now uses GetProcAs<TFunction>.
#include <EntryPoint.h>

using namespace Maho;

int main()
{
	// just ensure the header compiles (the driver needs a real DLL to run)
	return 0;
}
