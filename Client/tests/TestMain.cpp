#include "TestHarness.h"

// Kept out of TestHarness.cpp so the harness can also be linked into a binary that needs its
// own main - the Qt controller tests have to construct a QCoreApplication first.
int main(int argc, char* argv[])
{
	return testing::runAll(argc, argv);
}
