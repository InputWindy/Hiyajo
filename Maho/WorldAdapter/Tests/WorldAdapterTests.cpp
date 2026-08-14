#include "TestFramework.h"

int main()
{
	FTestRunner Runner;
	RunProtocolTests(Runner);
	RunCoreTests(Runner);
	RunHttpTests(Runner);
	std::cout << Runner.GetTotal() - Runner.GetFailed() << "/" << Runner.GetTotal() << " tests passed\n";
	return Runner.GetFailed() == 0 ? 0 : 1;
}
