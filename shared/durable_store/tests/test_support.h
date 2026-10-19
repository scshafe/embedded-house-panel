#pragma once
#include <cstdlib>
#include <iostream>
#include <string>
namespace test
{
inline int failures = 0;
inline void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}
inline int finish(const char *name)
{
	std::cout << name << ": " << failures << " failures\n";
	return failures ? 1 : 0;
}
} // namespace test
