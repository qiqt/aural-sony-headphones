#pragma once

// A dependency-free test harness. The engine has no test framework and pulling one in for a
// handful of protocol tests would be more build surface than the tests are worth.
//
// Usage:
//   TEST(suite_name_of_the_case) { CHECK(x); CHECK_EQ(a, b); }
//
// The runner executes every registered case, or only those whose name starts with the
// prefix given on the command line (CTest registers one entry per suite that way).

#include <chrono>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace testing {

struct TestCase
{
	std::string name;
	std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
	static std::vector<TestCase> cases;
	return cases;
}

struct Registrar
{
	Registrar(const char* name, std::function<void()> fn)
	{
		registry().push_back(TestCase{ name, std::move(fn) });
	}
};

class Failure : public std::runtime_error
{
public:
	explicit Failure(const std::string& what) : std::runtime_error(what) {}
};

template <class T>
inline std::string describe(const T& value)
{
	std::ostringstream os;
	if constexpr (std::is_enum_v<T>)
		os << static_cast<long long>(static_cast<std::underlying_type_t<T>>(value));
	else
		os << value;
	return os.str();
}

inline std::string describe(bool value) { return value ? "true" : "false"; }

// Wait until pred() is true. Returns false if the timeout expires first.
//
// This is a bounded poll, not a "sleep long enough and hope" delay: every assertion is on
// the predicate, so a slow machine only makes the test slower, never flaky. The timeout
// exists purely so a broken build fails instead of hanging.
template <class Pred>
inline bool waitFor(Pred pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (pred())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return pred();
}

int runAll(int argc, char* argv[]);

} // namespace testing

#define TEST(name)                                                            \
	static void name();                                                       \
	static ::testing::Registrar registrar_##name(#name, name);                \
	static void name()

#define CHECK(cond)                                                           \
	do {                                                                      \
		if (!(cond))                                                          \
			throw ::testing::Failure(std::string(__FILE__) + ":" +            \
				std::to_string(__LINE__) + ": CHECK(" #cond ") failed");      \
	} while (0)

#define CHECK_MSG(cond, msg)                                                  \
	do {                                                                      \
		if (!(cond))                                                          \
			throw ::testing::Failure(std::string(__FILE__) + ":" +            \
				std::to_string(__LINE__) + ": " + (msg));                     \
	} while (0)

#define CHECK_EQ(actual, expected)                                            \
	do {                                                                      \
		auto&& _a = (actual);                                                 \
		auto&& _e = (expected);                                               \
		if (!(_a == _e))                                                      \
			throw ::testing::Failure(std::string(__FILE__) + ":" +            \
				std::to_string(__LINE__) + ": " #actual " == " #expected      \
				" failed (actual " + ::testing::describe(_a) +                \
				", expected " + ::testing::describe(_e) + ")");               \
	} while (0)

#define CHECK_THROWS(expr, exceptionType)                                     \
	do {                                                                      \
		bool _threw = false;                                                  \
		try { (void)(expr); }                                                 \
		catch (const exceptionType&) { _threw = true; }                       \
		if (!_threw)                                                          \
			throw ::testing::Failure(std::string(__FILE__) + ":" +            \
				std::to_string(__LINE__) + ": expected " #expr               \
				" to throw " #exceptionType);                                 \
	} while (0)
