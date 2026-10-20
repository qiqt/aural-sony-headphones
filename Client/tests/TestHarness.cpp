#include "TestHarness.h"

#include <algorithm>

namespace testing {

int runAll(int argc, char* argv[])
{
	std::string filter;
	if (argc > 1)
		filter = argv[1];

	auto& cases = registry();
	std::sort(cases.begin(), cases.end(), [](const TestCase& a, const TestCase& b) {
		return a.name < b.name;
	});

	int run = 0, failed = 0;
	for (const auto& testCase : cases)
	{
		if (!filter.empty() && testCase.name.rfind(filter, 0) != 0)
			continue;

		++run;
		try
		{
			testCase.fn();
			std::cout << "[ ok ] " << testCase.name << std::endl;
		}
		catch (const Failure& failure)
		{
			++failed;
			std::cout << "[FAIL] " << testCase.name << "\n       " << failure.what() << std::endl;
		}
		catch (const std::exception& exc)
		{
			++failed;
			std::cout << "[FAIL] " << testCase.name << "\n       unexpected exception: "
				<< exc.what() << std::endl;
		}
	}

	if (run == 0)
	{
		std::cout << "no tests matched filter '" << filter << "'" << std::endl;
		return 1;
	}

	std::cout << (failed == 0 ? "PASSED " : "FAILED ") << (run - failed) << "/" << run
		<< " tests" << std::endl;
	return failed == 0 ? 0 : 1;
}

} // namespace testing
